#include "normalizer.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>

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
    RawEvent event;
    const size_t batch_size = 100;
    std::vector<RawEvent> events_batch;
    events_batch.reserve(batch_size);
    
    while (running_.load()) {
        // Try to dequeue in batches for better performance
        size_t dequeued = input_queue_->try_dequeue_bulk(events_batch.data(), batch_size);
        
        if (dequeued == 0) {
            // No events available, sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Process batch
        for (size_t i = 0; i < dequeued; ++i) {
            MEASURE_LATENCY("normalize_event_ns");
            
            try {
                Frame frame = normalize_event(events_batch[i]);
                output_queue_->enqueue(std::move(frame));
                
                stats_.frames_output.fetch_add(1);
            } catch (const std::exception& e) {
                spdlog::warn("Failed to normalize event: {}", e.what());
                stats_.errors.fetch_add(1);
                MetricsCollector::instance().increment_counter("normalizer_errors_total");
            }
            
            stats_.events_processed.fetch_add(1);
        }
        
        MetricsCollector::instance().increment_counter("normalizer_events_total", dequeued);
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