#pragma once
#include "../core/feature_vector.hpp"
#include <atomic>

namespace md::mpie {

class FeaturePublisher {
    std::atomic<uint64_t> total_published_{0};

public:
    inline void publish(const FeatureVector& fv) noexcept {
        if (fv.is_valid) [[likely]] {
            total_published_.fetch_add(1, std::memory_order_relaxed);
            // In the future: write to shared memory / disruptor queue
        }
    }

    [[nodiscard]] uint64_t get_total_published() const noexcept {
        return total_published_.load(std::memory_order_relaxed);
    }
};

} // namespace md::mpie
