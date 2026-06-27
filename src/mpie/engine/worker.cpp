#include "worker.hpp"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace md::mpie {

Worker::Worker(uint32_t worker_id)
    : worker_id_(worker_id),
      queue_(std::make_shared<moodycamel::ConcurrentQueue<MarketEvent>>(1024 * 1024))
{}

Worker::~Worker() {
    stop();
}

void Worker::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        thread_func(std::move(token));
    });
}

void Worker::stop() {
    if (running_.exchange(false)) {
        if (thread_) {
            thread_->request_stop();
            thread_->join();
            thread_.reset();
        }
    }
}

std::shared_ptr<moodycamel::ConcurrentQueue<MarketEvent>> Worker::get_queue() const {
    return queue_;
}

void Worker::pin_thread() {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    DWORD_PTR mask = (1ULL << (worker_id_ % 64)); // simple wrapping for demo
    SetThreadAffinityMask(thread, mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(worker_id_ % CPU_SETSIZE, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    std::cout << "[Worker " << worker_id_ << "] Thread pinned to core " << (worker_id_ % 64) << "\n";
}

void Worker::thread_func(std::stop_token token) {
    pin_thread();
    
    MarketEvent event;
    while (!token.stop_requested()) {
        if (queue_->try_dequeue(event)) {
            // Process the event
            // In M1, we just increment a counter
            processed_count_.fetch_add(1, std::memory_order_relaxed);
            
            // Further passes and feature processing will go here in M3
        } else {
            // Spin/yield loop to avoid pegging CPU at 100% when idle for demo
            std::this_thread::yield(); 
        }
    }
}

} // namespace md::mpie
