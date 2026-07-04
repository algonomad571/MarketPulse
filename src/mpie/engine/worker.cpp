#include "worker.hpp"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace md::mpie {

// ANSI Colors for Task 5
constexpr std::string_view ANSI_RESET  = "\033[0m";
constexpr std::string_view ANSI_GREEN  = "\033[32m";
constexpr std::string_view ANSI_YELLOW = "\033[33m";
constexpr std::string_view ANSI_RED    = "\033[31m";
constexpr std::string_view ANSI_CYAN   = "\033[36m";

Worker::Worker(uint32_t worker_id)
    : worker_id_(worker_id),
      queue_(std::make_shared<moodycamel::ConcurrentQueue<MarketEvent>>(1024 * 1024))
{}

Worker::~Worker() {
    stop();
}

void Worker::start() noexcept {
    if (running_.exchange(true, std::memory_order_acquire)) {
        return;
    }
    thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        thread_func(std::move(token));
    });
}

void Worker::stop() noexcept {
    if (running_.exchange(false, std::memory_order_release)) {
        if (thread_) {
            thread_->request_stop();
            thread_->join();
            thread_.reset();
        }
    }
}

void Worker::pin_thread() noexcept {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    DWORD_PTR mask = (1ULL << (worker_id_ % 64)); 
    SetThreadAffinityMask(thread, mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(worker_id_ % CPU_SETSIZE, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    diagnostics_.pinned_core.store(worker_id_ % 64, std::memory_order_relaxed);
}

void Worker::record_latency(uint64_t latency_ns) noexcept {
    diagnostics_.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
    diagnostics_.latency_samples.fetch_add(1, std::memory_order_relaxed);
    
    // Atomic min/max updates (lock-free CAS loops)
    uint64_t current_min = diagnostics_.min_latency_ns.load(std::memory_order_relaxed);
    while (latency_ns < current_min && 
           !diagnostics_.min_latency_ns.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed)) {}

    uint64_t current_max = diagnostics_.max_latency_ns.load(std::memory_order_relaxed);
    while (latency_ns > current_max && 
           !diagnostics_.max_latency_ns.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed)) {}

    // Fixed-size bucket tracking (HdrHistogram approach)
    size_t bucket_idx = std::min<size_t>(latency_ns / WorkerDiagnostics::BUCKET_WIDTH_NS, WorkerDiagnostics::LATENCY_BUCKETS - 1);
    diagnostics_.latency_histogram[bucket_idx].fetch_add(1, std::memory_order_relaxed);
}

void Worker::thread_func(std::stop_token token) noexcept {
    pin_thread();
    
    MarketEvent event;
    auto last_time = std::chrono::high_resolution_clock::now();

    while (!token.stop_requested()) {
        auto iter_start = std::chrono::high_resolution_clock::now();

        size_t current_occ = queue_->size_approx();
        diagnostics_.occupancy_sum.fetch_add(current_occ, std::memory_order_relaxed);
        diagnostics_.occupancy_samples.fetch_add(1, std::memory_order_relaxed);
        
        size_t peak = diagnostics_.peak_occupancy.load(std::memory_order_relaxed);
        if (current_occ > peak) {
            // Uncontended, single writer so a simple store or CAS is fine
            diagnostics_.peak_occupancy.compare_exchange_weak(peak, current_occ, std::memory_order_relaxed);
        }

        if (queue_->try_dequeue(event)) {
            // Calculate entry-to-dispatch delta
            auto dispatch_time = std::chrono::high_resolution_clock::now();
            uint64_t latency_ns = 0;
            // Only calculate if the timestamp looks like a realistic chrono count (not just i=1,2,3 from tests)
            if (event.timestamp > 1000000000ULL) { 
                latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    dispatch_time.time_since_epoch() - std::chrono::nanoseconds(event.timestamp)
                ).count();
            }

            // Processing logic goes here (State Management in M2)
            // ...

            auto iter_end = std::chrono::high_resolution_clock::now();
            
            // Add processing time to latency delta
            if (event.timestamp > 1000000000ULL) {
                uint64_t processing_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - dispatch_time).count();
                record_latency(latency_ns + processing_ns);
            }

            diagnostics_.processed_count.fetch_add(1, std::memory_order_relaxed);
            
            uint64_t busy_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start).count();
            diagnostics_.active_time_ns.fetch_add(busy_ns, std::memory_order_relaxed);
        } else {
            diagnostics_.pop_failures.fetch_add(1, std::memory_order_relaxed);
            
            // Spin/yield
            std::this_thread::yield(); 
            
            auto iter_end = std::chrono::high_resolution_clock::now();
            uint64_t idle_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start).count();
            diagnostics_.idle_time_ns.fetch_add(idle_ns, std::memory_order_relaxed);
        }
    }
}

} // namespace md::mpie
