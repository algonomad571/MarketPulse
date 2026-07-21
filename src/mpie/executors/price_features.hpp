#pragma once
#include "feature_plugin.hpp"
#include <cmath>

namespace md::mpie {

struct SpreadExecutor {
    static constexpr std::string_view name() noexcept { return "BidAskSpread"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        fv.metrics[IDX_SPREAD] = ev->ask_price - ev->bid_price;
    }
};

struct MidPriceExecutor {
    static constexpr std::string_view name() noexcept { return "MidPrice"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        fv.metrics[IDX_MID_PRICE] = (ev->bid_price + ev->ask_price) * 0.5;
    }
};

struct MicroPriceExecutor {
    static constexpr std::string_view name() noexcept { return "MicroPrice"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        double total_size = static_cast<double>(ev->bid_size + ev->ask_size);
        
        if (total_size == 0.0) [[unlikely]] {
            fv.metrics[IDX_MICRO_PRICE] = (ev->bid_price + ev->ask_price) * 0.5;
            return;
        }
        
        // Micro-price weight matches volume pressure: (BidPrice * AskSize + AskPrice * BidSize) / TotalSize
        fv.metrics[IDX_MICRO_PRICE] = ((ev->bid_price * ev->ask_size) + (ev->ask_price * ev->bid_size)) / total_size;
    }
};

struct LogReturnExecutor {
    static constexpr std::string_view name() noexcept { return "LogReturn"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& mid_history = ctx->symbol_state->book.mid_history;
        if (mid_history.size() < 2) [[unlikely]] {
            fv.metrics[IDX_LOG_RETURN] = 0.0;
            return;
        }
        
        double current_mid = mid_history.lookback(0);
        double prev_mid = mid_history.lookback(1);
        
        if (prev_mid <= 0.0 || current_mid <= 0.0) [[unlikely]] {
            fv.metrics[IDX_LOG_RETURN] = 0.0;
            return;
        }

        fv.metrics[IDX_LOG_RETURN] = std::log(current_mid / prev_mid);
    }
};

struct WAPExecutor {
    static constexpr std::string_view name() noexcept { return "WAP"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        double total_size = static_cast<double>(ev->bid_size + ev->ask_size);
        
        if (total_size == 0.0) [[unlikely]] {
            fv.metrics[IDX_WAP] = (ev->bid_price + ev->ask_price) * 0.5;
            return;
        }
        
        fv.metrics[IDX_WAP] = ((ev->bid_price * static_cast<double>(ev->bid_size)) + 
                               (ev->ask_price * static_cast<double>(ev->ask_size))) / total_size;
    }
};

} // namespace md::mpie
