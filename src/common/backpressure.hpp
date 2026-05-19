#pragma once

#include "metrics.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

namespace md {

class QueueBackpressureController {
public:
    QueueBackpressureController(std::string queue_name,
                                uint32_t high_watermark,
                                uint32_t low_watermark,
                                std::chrono::microseconds pause_sleep = std::chrono::microseconds(100))
        : queue_name_(std::move(queue_name)),
          high_watermark_(high_watermark),
          low_watermark_(low_watermark),
          pause_sleep_(pause_sleep) {
        if (high_watermark_ == 0) {
            high_watermark_ = 1;
        }
        if (low_watermark_ >= high_watermark_) {
            low_watermark_ = high_watermark_ - 1;
        }

        // Update the aggregate queue depth metric on first use.
        MetricsCollector::instance().set_gauge(metric_name("queue_depth"), 0);
        MetricsCollector::instance().set_gauge(metric_name("backpressure_active"), 0);
    }

    void wait_for_capacity(const std::function<size_t()>& queue_depth_fn,
                           const std::atomic<bool>* running = nullptr) {
        size_t depth = queue_depth_fn();
        // Update the aggregate depth gauge at the exact point queue pressure is observed.
        MetricsCollector::instance().set_gauge(metric_name("queue_depth"), static_cast<double>(depth));
        MetricsCollector::instance().set_gauge("queue_depth", static_cast<double>(depth));

        if (depth < high_watermark_) {
            return;
        }

        bool should_log_activate = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!active_) {
                active_ = true;
                should_log_activate = true;
            }
        }

        if (should_log_activate) {
            // Update pause counters where backpressure actually activates.
            MetricsCollector::instance().increment_counter(metric_name("producer_pauses"));
            MetricsCollector::instance().increment_counter("producer_pauses");
            active_queue_count().fetch_add(1, std::memory_order_relaxed);
            MetricsCollector::instance().set_gauge(metric_name("backpressure_active"), 1);
            MetricsCollector::instance().set_gauge("backpressure_active", 1);
            spdlog::warn("[Backpressure] Activated queue={} depth={} high={} low={}",
                         queue_name_,
                         depth,
                         high_watermark_,
                         low_watermark_);
        }

        while (depth > low_watermark_) {
            if (running != nullptr && !running->load()) {
                break;
            }

            std::this_thread::sleep_for(pause_sleep_);
            depth = queue_depth_fn();
            MetricsCollector::instance().set_gauge(metric_name("queue_depth"), static_cast<double>(depth));
        }

        bool should_log_release = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (active_ && depth <= low_watermark_) {
                active_ = false;
                should_log_release = true;
            }
        }

        if (should_log_release) {
            // Update resume counters where backpressure actually releases.
            MetricsCollector::instance().increment_counter(metric_name("producer_resume_count"));
            MetricsCollector::instance().increment_counter("producer_resume_count");
            const uint64_t remaining_active = active_queue_count().fetch_sub(1, std::memory_order_relaxed);
            MetricsCollector::instance().set_gauge(metric_name("backpressure_active"), 0);
            MetricsCollector::instance().set_gauge("backpressure_active", remaining_active > 1 ? 1 : 0);
            spdlog::info("[Backpressure] Released queue={} depth={} high={} low={}",
                         queue_name_,
                         depth,
                         high_watermark_,
                         low_watermark_);
        }
    }

private:
    static std::atomic<uint64_t>& active_queue_count() {
        static std::atomic<uint64_t> count{0};
        return count;
    }

    std::string metric_name(const std::string& suffix) const {
        return "queue_" + queue_name_ + "_" + suffix;
    }

    std::string queue_name_;
    uint32_t high_watermark_;
    uint32_t low_watermark_;
    std::chrono::microseconds pause_sleep_;

    bool active_ = false;
    std::mutex state_mutex_;
};

} // namespace md