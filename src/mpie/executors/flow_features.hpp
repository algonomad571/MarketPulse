#pragma once
#include "feature_plugin.hpp"

namespace md::mpie {

struct OrderFlowImbalanceExecutor {
    static constexpr std::string_view name() noexcept { return "OrderFlowImbalance"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* cur = ctx->current_event;
        const auto* prev = ctx->previous_event;

        // Boundary guard: If this is the initial event, delta is identically zero
        if (prev == nullptr || prev->timestamp == 0) [[unlikely]] {
            fv.metrics[IDX_ORDER_FLOW_IMBALANCE] = 0.0;
            return;
        }

        // --- Bid Delta Calculation ---
        double delta_v_b = 0.0;
        double cur_bid_sz = static_cast<double>(cur->bid_size);
        double prev_bid_sz = static_cast<double>(prev->bid_size);

        if (cur->bid_price > prev->bid_price) {
            delta_v_b = cur_bid_sz;
        } else if (cur->bid_price == prev->bid_price) {
            delta_v_b = cur_bid_sz - prev_bid_sz;
        } else {
            delta_v_b = -prev_bid_sz;
        }

        // --- Ask Delta Calculation ---
        double delta_v_a = 0.0;
        double cur_ask_sz = static_cast<double>(cur->ask_size);
        double prev_ask_sz = static_cast<double>(prev->ask_size);

        if (cur->ask_price < prev->ask_price) {
            delta_v_a = cur_ask_sz;
        } else if (cur->ask_price == prev->ask_price) {
            delta_v_a = cur_ask_sz - prev_ask_sz;
        } else {
            delta_v_a = -prev_ask_sz;
        }

        // Net Flow Pressure
        fv.metrics[IDX_ORDER_FLOW_IMBALANCE] = delta_v_b - delta_v_a;
    }
};

} // namespace md::mpie
