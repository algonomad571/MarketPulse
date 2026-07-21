#pragma once
#include "feature_plugin.hpp"

namespace md::mpie {

struct BookImbalanceExecutor {
    static constexpr std::string_view name() noexcept { return "QueueImbalance"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        double bid_sz = static_cast<double>(ev->bid_size);
        double ask_sz = static_cast<double>(ev->ask_size);
        double total_sz = bid_sz + ask_sz;
        
        if (total_sz == 0.0) [[unlikely]] {
            fv.metrics[IDX_BOOK_IMBALANCE] = 0.0;
            return;
        }
        
        // Standard normalized volume imbalance bounds scalar: (-1.0 to +1.0)
        fv.metrics[IDX_BOOK_IMBALANCE] = (bid_sz - ask_sz) / total_sz;
    }
};

} // namespace md::mpie
