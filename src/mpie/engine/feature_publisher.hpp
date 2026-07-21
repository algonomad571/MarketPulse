#pragma once
#include "../core/feature_vector.hpp"
#include "../storage/feature_store_worker.hpp"
#include <atomic>

namespace md::mpie {

class FeaturePublisher {
    std::atomic<uint64_t> total_published_{0};
    FeatureStoreWorker* store_{nullptr};

public:
    void set_store(FeatureStoreWorker* store) noexcept { store_ = store; }

    inline void publish(const FeatureVector& fv) noexcept {
        if (fv.is_valid) [[likely]] {
            total_published_.fetch_add(1, std::memory_order_relaxed);
            if (store_) [[likely]] {
                store_->enqueue(fv);
            }
        }
    }

    [[nodiscard]] uint64_t get_total_published() const noexcept {
        return total_published_.load(std::memory_order_relaxed);
    }
};

} // namespace md::mpie
