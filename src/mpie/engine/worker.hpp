#pragma once

#include "../core/market_event.hpp"
#include <concurrentqueue.h>
#include <thread>
#include <atomic>
#include <memory>
#include <array>
#include <chrono>

#include "state_manager.hpp"
#include "context_builder.hpp"
#include "feature_pipeline.hpp"
#include "feature_validator.hpp"
#include "feature_publisher.hpp"
#include "../storage/feature_store_worker.hpp"

namespace md::mpie {

// Task 3, 4, 7: Lock-Free Queue & Worker Diagnostics + Latency Histogram
struct alignas(64) WorkerDiagnostics {
    std::atomic<uint64_t> processed_count{0};
    std::atomic<uint64_t> push_failures{0};
    std::atomic<uint64_t> pop_failures{0};
    std::atomic<uint64_t> rejected_events{0};
    
    std::atomic<uint64_t> active_time_ns{0};
    std::atomic<uint64_t> idle_time_ns{0};
    std::atomic<uint64_t> total_latency_ns{0};
    std::atomic<uint64_t> latency_samples{0};
    
    std::atomic<uint64_t> min_latency_ns{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> max_latency_ns{0};

    std::atomic<uint32_t> pinned_core{0};
    std::atomic<size_t> peak_occupancy{0};
    std::atomic<size_t> occupancy_sum{0};
    std::atomic<uint64_t> occupancy_samples{0};

    // Fixed-size bucketed array for quantile approximation (HdrHistogram approach)
    // Bins: 0-99ns, 100-199ns, ..., >10us
    static constexpr size_t LATENCY_BUCKETS = 100;
    static constexpr uint64_t BUCKET_WIDTH_NS = 100;
    std::array<std::atomic<uint64_t>, LATENCY_BUCKETS> latency_histogram{};

    WorkerDiagnostics() noexcept {
        for (auto& bucket : latency_histogram) {
            bucket.store(0, std::memory_order_relaxed);
        }
    }
};

class Worker {
public:
    explicit Worker(uint32_t worker_id, uint32_t universe_size);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void start() noexcept;
    void stop() noexcept;

    [[nodiscard]] std::shared_ptr<moodycamel::ConcurrentQueue<MarketEvent>> get_queue() const noexcept { return queue_; }
    [[nodiscard]] uint32_t get_id() const noexcept { return worker_id_; }
    [[nodiscard]] const WorkerDiagnostics& get_diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] WorkerDiagnostics& get_diagnostics() noexcept { return diagnostics_; }

private:
    void thread_func(std::stop_token token) noexcept;
    void pin_thread() noexcept;
    void record_latency(uint64_t latency_ns) noexcept;

    uint32_t worker_id_;
    std::shared_ptr<moodycamel::ConcurrentQueue<MarketEvent>> queue_;
    std::unique_ptr<std::jthread> thread_;
    std::atomic<bool> running_{false};
    
    WorkerDiagnostics diagnostics_;
    
    StateManager state_manager_;
    ContextBuilder context_builder_;
    
    FeaturePipeline pipeline_;
    FeatureValidator validator_;
    FeaturePublisher publisher_;
    FeatureStoreWorker store_worker_;
};

} // namespace md::mpie
