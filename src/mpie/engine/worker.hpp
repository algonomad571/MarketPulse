#pragma once

#include "../core/market_event.hpp"
#include <concurrentqueue.h>
#include <thread>
#include <atomic>
#include <memory>

namespace md::mpie {

class Worker {
public:
    explicit Worker(uint32_t worker_id);
    ~Worker();

    void start();
    void stop();

    std::shared_ptr<moodycamel::ConcurrentQueue<MarketEvent>> get_queue() const;

    uint32_t get_id() const { return worker_id_; }
    uint64_t get_processed_count() const { return processed_count_.load(std::memory_order_relaxed); }

private:
    void thread_func(std::stop_token token);
    void pin_thread();

    uint32_t worker_id_;
    std::shared_ptr<moodycamel::ConcurrentQueue<MarketEvent>> queue_;
    std::unique_ptr<std::jthread> thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> processed_count_{0};
};

} // namespace md::mpie
