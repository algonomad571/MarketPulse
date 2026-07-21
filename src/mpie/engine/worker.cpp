#include "worker.hpp"
#include "../egress/shm_publisher.hpp" // Includes macros MPIE_PAUSE, MPIE_PREFETCH_READ
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

Worker::Worker(uint32_t worker_id, uint32_t universe_size, ShmPublisher* shm_pub)
    : worker_id_(worker_id),
      queue_(std::make_shared<moodycamel::ConcurrentQueue<MarketEvent>>(1024 * 1024)),
      state_manager_(universe_size),
      store_worker_("worker_" + std::to_string(worker_id) + "_features.bin")
{
    publisher_.set_store(&store_worker_);
    publisher_.set_shm(shm_pub);
}

Worker::~Worker() {
    stop();
}

void Worker::start() noexcept {
    if (running_.exchange(true, std::memory_order_acquire)) {
        return;
    }
    store_worker_.start();
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
        store_worker_.stop();
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
    if (diagnostics_.processed_count.load(std::memory_order_relaxed) > 10000) {
        while (latency_ns > current_max && 
               !diagnostics_.max_latency_ns.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed)) {}
    }

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
            diagnostics_.peak_occupancy.compare_exchange_weak(peak, current_occ, std::memory_order_relaxed);
        }

        if (queue_->try_dequeue(event)) [[likely]] {
            spin_count_ = 0;

            // Calculate entry-to-dispatch delta
            auto dispatch_time = std::chrono::high_resolution_clock::now();
            uint64_t latency_ns = 0;
            if (event.timestamp > 1000000000ULL) { 
                latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    dispatch_time.time_since_epoch() - std::chrono::nanoseconds(event.timestamp)
                ).count();
            }

            auto state_update_start = std::chrono::high_resolution_clock::now();
            
            // Prefetch SymbolState to hide memory-bus latency
            const auto* state_ptr = &state_manager_.get_state(event.symbol_id);
            MPIE_PREFETCH_READ(state_ptr);
            
            // M2: State Management & Context Building (Hot Path)
            state_manager_.update_state(event);
            
            auto state_update_end = std::chrono::high_resolution_clock::now();
            uint64_t state_update_latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(state_update_end - state_update_start).count();

            // Build Context (Zero Allocation)
            FeatureContext context = context_builder_.build(
                event,
                state_manager_.get_state(event.symbol_id),
                state_update_latency_ns
            );
            
            // M3: Compile-Time Pipeline Execution
            FeatureVector fv{};
            fv.engine_timestamp = event.timestamp;
            fv.symbol_id = event.symbol_id;
            fv.version = 1;
            fv.is_valid = true;
            
            pipeline_.execute_pipeline(&context, fv);
            validator_.validate(fv);
            publisher_.publish(fv);

            // Touch context to prevent optimizer from stripping it out of our bench
            volatile uint64_t dummy = fv.engine_timestamp;
            (void)dummy;

            auto iter_end = std::chrono::high_resolution_clock::now();
            
            // Add processing time to latency delta
            if (event.timestamp > 1000000000ULL) {
                uint64_t processing_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - dispatch_time).count();
                record_latency(latency_ns + processing_ns);
            }

            diagnostics_.processed_count.fetch_add(1, std::memory_order_relaxed);
            // Increment active time
            diagnostics_.active_time_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - iter_start).count(),
                std::memory_order_relaxed
            );
        } else {
            diagnostics_.pop_failures.fetch_add(1, std::memory_order_relaxed);

            // Stage 1: Brief PAUSE instruction (keeps CPU pipeline hot, ~10-14 cycles)
            MPIE_PAUSE();
            
            // Stage 2: If idle for > 100 consecutive iterations, yield thread turn
            if (++spin_count_ > 100) {
                std::this_thread::yield();
            }
            
            auto iter_end = std::chrono::high_resolution_clock::now();
            uint64_t idle_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start).count();
            diagnostics_.idle_time_ns.fetch_add(idle_ns, std::memory_order_relaxed);
        }
    }
}

} // namespace md::mpie
