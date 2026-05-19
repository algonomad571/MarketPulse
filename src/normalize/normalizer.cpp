#include "normalizer.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <functional>

// Normalizer: Event processing component
// 
// ASSUMPTIONS:
// - Symbol registry is thread-safe (uses internal mutex)
// - Output queue has sufficient capacity (dynamic growth)
// - Event processing errors are non-fatal (log and continue)
// 
// FAILURE MODES:
// - Normalization exceptions → logged, metrics incremented, continue
// - Enqueue failures → extremely rare with dynamic queues, logged if occurs
// - Queue full → enqueue returns false (tracked via metrics)
// 
// LIMITATIONS:
// - No timeout on enqueue operations
// - Failed events are dropped (not retried)
// - Deterministic symbol partitioning improves ordering but reduces load-balancing flexibility
// - Work is still partitioned by worker count, so skewed symbols can concentrate load on one worker
// 
// RECOVERY:
// - Monitor normalizer_failures_total
// - Check normalizer_enqueue_failures_total (should be 0)
// - Review error logs for specific failure patterns

namespace md {

namespace {
    constexpr int64_t PRICE_SCALE = 100000000LL; // 1e8 for price scaling
    constexpr uint64_t SIZE_SCALE = 100000000ULL; // 1e8 for size scaling
}

Normalizer::Normalizer(std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> input_queue,
                       std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> output_queue,
                       std::shared_ptr<SymbolRegistry> symbol_registry,
                                             uint32_t num_threads,
                                             uint32_t queue_high_watermark,
                                             uint32_t queue_low_watermark)
        : input_queue_(input_queue), output_queue_(output_queue),
            symbol_registry_(symbol_registry), num_threads_(num_threads),
            worker_count_(std::max<uint32_t>(1, num_threads)),
            queue_backpressure_("normalizer_to_distributor", queue_high_watermark, queue_low_watermark),
            partition_backpressure_("ordering_partitions", queue_high_watermark, queue_low_watermark) {
    partition_queues_.reserve(worker_count_);
    for (uint32_t i = 0; i < worker_count_; ++i) {
        partition_queues_.push_back(std::make_shared<moodycamel::ConcurrentQueue<RawEvent>>(100000));
    }

    MetricsCollector::instance().set_gauge("ordering_partitions", static_cast<double>(worker_count_));
    MetricsCollector::instance().set_gauge("partition_queue_depth", 0);
}

Normalizer::~Normalizer() {
    stop();
}

void Normalizer::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    
    worker_threads_.clear();
    worker_threads_.reserve(worker_count_);

    for (uint32_t i = 0; i < worker_count_; ++i) {
        worker_threads_.emplace_back(
            std::make_unique<std::jthread>([this, i](std::stop_token token) {
                worker_thread(i, token);
            })
        );
    }

    routing_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        routing_thread(token);
    });

    spdlog::info("Normalizer started with {} ordering partitions", worker_count_);
}

void Normalizer::stop() {
    if (!running_.exchange(false)) {
        return; // not running
    }

    if (routing_thread_ && routing_thread_->joinable()) {
        routing_thread_->request_stop();
        routing_thread_->join();
    }
    
    for (auto& thread : worker_threads_) {
        if (thread && thread->joinable()) {
            thread->request_stop();
            thread->join();
        }
    }
    worker_threads_.clear();
    routing_thread_.reset();
    
    spdlog::info("Normalizer stopped");
}

uint32_t Normalizer::worker_for_symbol(std::string_view symbol) const {
    return static_cast<uint32_t>(std::hash<std::string_view>{}(symbol) % worker_count_);
}

void Normalizer::update_partition_metrics() const {
    size_t max_depth = 0;
    for (const auto& queue : partition_queues_) {
        if (queue) {
            max_depth = std::max(max_depth, queue->size_approx());
        }
    }
    MetricsCollector::instance().set_gauge("partition_queue_depth", static_cast<double>(max_depth));
}

void Normalizer::routing_thread(std::stop_token token) {
    const size_t batch_size = 100;
    std::vector<RawEvent> events_batch;
    events_batch.reserve(batch_size);

    while (running_.load() || input_queue_->size_approx() > 0) {
        if (token.stop_requested() && input_queue_->size_approx() == 0) {
            break;
        }

        events_batch.clear();
        RawEvent event;
        while (events_batch.size() < batch_size && input_queue_->try_dequeue(event)) {
            events_batch.push_back(std::move(event));
        }

        if (events_batch.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        for (auto& queued_event : events_batch) {
            const uint32_t worker_index = worker_for_symbol(queued_event.symbol);
            spdlog::info("[Ordering] Symbol={} Worker={}", queued_event.symbol, worker_index);

            partition_backpressure_.wait_for_capacity([this, worker_index]() {
                return partition_queues_[worker_index]->size_approx();
            }, &running_);

            partition_queues_[worker_index]->enqueue(std::move(queued_event));
            update_partition_metrics();
        }

        MetricsCollector::instance().increment_counter("normalizer_events_total", events_batch.size());
    }
}

void Normalizer::worker_thread(uint32_t worker_index, std::stop_token token) {
    const size_t batch_size = 100;
    std::vector<RawEvent> events_batch;
    events_batch.reserve(batch_size);
    auto last_log_time = std::chrono::steady_clock::now();
    uint64_t processed_since_log = 0;
    auto& partition_queue = partition_queues_[worker_index];
    
    while (running_.load() || partition_queue->size_approx() > 0) {
        if (token.stop_requested() && partition_queue->size_approx() == 0) {
            break;
        }

        events_batch.clear();
        RawEvent event;
        while (events_batch.size() < batch_size && partition_queue->try_dequeue(event)) {
            events_batch.push_back(std::move(event));
        }

        size_t dequeued = events_batch.size();
        
        if (dequeued == 0) {
            // No events available, sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Process batch
        for (size_t i = 0; i < dequeued; ++i) {
            MEASURE_LATENCY("normalizer_event_latency_ns");
            
            try {
                Frame frame = normalize_event(events_batch[i]);
                
                queue_backpressure_.wait_for_capacity([this]() {
                    return output_queue_->size_approx();
                }, &running_);
                bool enqueued = output_queue_->enqueue(std::move(frame));
                if (!enqueued) {
                    spdlog::error("Failed to enqueue normalized frame (queue full?)");
                    stats_.errors.fetch_add(1);
                    MetricsCollector::instance().increment_counter("normalizer_enqueue_failures_total");
                } else {
                    stats_.frames_output.fetch_add(1);
                }
            } catch (const std::exception& e) {
                spdlog::warn("Failed to normalize event: {}", e.what());
                stats_.errors.fetch_add(1);
                MetricsCollector::instance().increment_counter("normalizer_errors_total");
                MetricsCollector::instance().increment_counter("normalizer_failures_total");
            }
            
            stats_.events_processed.fetch_add(1);
        }

        processed_since_log += dequeued;
        update_partition_metrics();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_log_time >= std::chrono::seconds(5)) {
            spdlog::info(
                "Flow[Normalizer] worker={} processed={} in_approx={} out_approx={} total_processed={} total_output={} errors={}",
                worker_index,
                processed_since_log,
                partition_queue->size_approx(),
                output_queue_->size_approx(),
                stats_.events_processed.load(),
                stats_.frames_output.load(),
                stats_.errors.load());
            processed_since_log = 0;
            last_log_time = now;
        }
    }
}

Frame Normalizer::normalize_event(const RawEvent& event) {
    uint32_t symbol_id = symbol_registry_->get_or_add(event.symbol);
    
    switch (event.type) {
        case RawEvent::L1: {
            L1Body body;
            body.ts_ns = event.timestamp_ns;
            body.symbol_id = symbol_id;
            body.bid_px = static_cast<int64_t>(event.bid_price * PRICE_SCALE);
            body.bid_sz = static_cast<uint64_t>(event.bid_size * SIZE_SCALE);
            body.ask_px = static_cast<int64_t>(event.ask_price * PRICE_SCALE);
            body.ask_sz = static_cast<uint64_t>(event.ask_size * SIZE_SCALE);
            body.seq = event.sequence;
            
            return Frame(body);
        }
        
        case RawEvent::L2: {
            L2Body body;
            body.ts_ns = event.timestamp_ns;
            body.symbol_id = symbol_id;
            body.side = static_cast<uint8_t>(event.side);
            body.action = static_cast<uint8_t>(event.action);
            body.level = event.level;
            body.price = static_cast<int64_t>(event.price * PRICE_SCALE);
            body.size = static_cast<uint64_t>(event.size * SIZE_SCALE);
            body.seq = event.sequence;
            
            return Frame(body);
        }
        
        case RawEvent::Trade: {
            TradeBody body;
            body.ts_ns = event.timestamp_ns;
            body.symbol_id = symbol_id;
            body.price = static_cast<int64_t>(event.trade_price * PRICE_SCALE);
            body.size = static_cast<uint64_t>(event.trade_size * SIZE_SCALE);
            body.aggressor_side = event.aggressor_side;
            body.seq = event.sequence;
            
            return Frame(body);
        }
        
        default:
            throw std::runtime_error("Unknown event type");
    }
}

} // namespace md