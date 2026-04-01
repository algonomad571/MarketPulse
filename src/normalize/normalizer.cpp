#include "normalizer.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>

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
// - Multiple threads share queues (lock-free, no ordering guarantee)
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
                       uint32_t num_threads)
    : input_queue_(input_queue), output_queue_(output_queue), 
      symbol_registry_(symbol_registry), num_threads_(num_threads) {
}

Normalizer::~Normalizer() {
    stop();
}

void Normalizer::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    
    worker_threads_.clear();
    worker_threads_.reserve(num_threads_);
    
    for (uint32_t i = 0; i < num_threads_; ++i) {
        worker_threads_.emplace_back(
            std::make_unique<std::jthread>([this](std::stop_token token) {
                worker_thread();
            })
        );
    }
    
    spdlog::info("Normalizer started with {} threads", num_threads_);
}

void Normalizer::stop() {
    if (!running_.exchange(false)) {
        return; // not running
    }
    
    for (auto& thread : worker_threads_) {
        if (thread && thread->joinable()) {
            thread->request_stop();
            thread->join();
        }
    }
    worker_threads_.clear();
    
    spdlog::info("Normalizer stopped");
}

void Normalizer::worker_thread() {
    const size_t batch_size = 100;
    std::vector<RawEvent> events_batch;
    events_batch.reserve(batch_size);
    auto last_log_time = std::chrono::steady_clock::now();
    uint64_t processed_since_log = 0;
    
    while (running_.load()) {
        events_batch.clear();
        RawEvent event;
        while (events_batch.size() < batch_size && input_queue_->try_dequeue(event)) {
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
                
                // HARDENING: Queue enqueue is lock-free and always succeeds
                // ASSUMPTION: Output queue has sufficient capacity
                // LIMITATION: If queue is full, enqueue may fail silently in lock-free implementation
                // NOTE: moodycamel::ConcurrentQueue grows dynamically, enqueue rarely fails
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
        
        MetricsCollector::instance().increment_counter("normalizer_events_total", dequeued);

        processed_since_log += dequeued;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_log_time >= std::chrono::seconds(5)) {
            spdlog::info(
                "Flow[Normalizer] processed={} in_approx={} out_approx={} total_processed={} total_output={} errors={}",
                processed_since_log,
                input_queue_->size_approx(),
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