#pragma once

#include "../common/frame.hpp"
#include "../common/backpressure.hpp"
#include "../common/symbol_registry.hpp"
#include "../feed/mock_feed.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <concurrentqueue.h>

namespace md {

class Normalizer {
public:
    Normalizer(std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> input_queue,
               std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> output_queue,
               std::shared_ptr<SymbolRegistry> symbol_registry,
               uint32_t num_threads,
               uint32_t queue_high_watermark,
               uint32_t queue_low_watermark);
    
    ~Normalizer();
    
    void start();
    void stop();
    
    struct Stats {
        std::atomic<uint64_t> events_processed{0};
        std::atomic<uint64_t> frames_output{0};
        std::atomic<uint64_t> errors{0};
    };
    
    const Stats& get_stats() const { return stats_; }

private:
    void worker_thread();
    Frame normalize_event(const RawEvent& event);
    
    std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> input_queue_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> output_queue_;
    std::shared_ptr<SymbolRegistry> symbol_registry_;
    
    uint32_t num_threads_;
    std::vector<std::unique_ptr<std::jthread>> worker_threads_;
    std::atomic<bool> running_{false};
    QueueBackpressureController queue_backpressure_;
    
    Stats stats_;
};

} // namespace md