#pragma once
#include "feature_plugin.hpp"
#include <cmath>
#include <algorithm>

namespace md::mpie {

struct VWAP32Executor {
    static constexpr std::string_view name() noexcept { return "VWAP_32"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& trade = ctx->symbol_state->trade;
        size_t count = std::min(trade.price_history.size(), static_cast<size_t>(32));
        
        if (count == 0) [[unlikely]] {
            fv.metrics[IDX_VWAP_32] = fv.metrics[IDX_MID_PRICE];
            return;
        }

        double sum_pv = 0.0;
        double sum_v = 0.0;
        for (size_t i = 0; i < count; ++i) {
            double p = trade.price_history.lookback(i);
            double v = static_cast<double>(trade.volume_history.lookback(i));
            sum_pv += p * v;
            sum_v += v;
        }

        if (sum_v == 0.0) [[unlikely]] {
            fv.metrics[IDX_VWAP_32] = fv.metrics[IDX_MID_PRICE];
        } else {
            fv.metrics[IDX_VWAP_32] = sum_pv / sum_v;
        }
    }
};

struct RealizedVolatility32Executor {
    static constexpr std::string_view name() noexcept { return "RealizedVolatility_32"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& book = ctx->symbol_state->book;
        size_t count = std::min(book.mid_history.size(), static_cast<size_t>(32));
        
        if (count < 2) [[unlikely]] {
            fv.metrics[IDX_REALIZED_VOLATILITY_32] = 0.0;
            fv.metrics[IDX_PRICE_ZSCORE_32] = 0.0;
            return;
        }

        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += book.mid_history.lookback(i);
        }
        double mean = sum / static_cast<double>(count);

        double sum_sq_diff = 0.0;
        for (size_t i = 0; i < count; ++i) {
            double diff = book.mid_history.lookback(i) - mean;
            sum_sq_diff += diff * diff;
        }
        
        double variance = sum_sq_diff / static_cast<double>(count - 1);
        double std_dev = std::sqrt(variance);

        fv.metrics[IDX_REALIZED_VOLATILITY_32] = std_dev;

        if (std_dev == 0.0) [[unlikely]] {
            fv.metrics[IDX_PRICE_ZSCORE_32] = 0.0;
        } else {
            fv.metrics[IDX_PRICE_ZSCORE_32] = (book.mid_history.lookback(0) - mean) / std_dev;
        }
    }
};

struct OFIZScore32Executor {
    static constexpr std::string_view name() noexcept { return "OFI_ZScore_32"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& flow = ctx->symbol_state->flow;
        size_t count = std::min(flow.ofi_history.size(), static_cast<size_t>(32));
        
        if (count < 2) [[unlikely]] {
            fv.metrics[IDX_OFI_ZSCORE_32] = 0.0;
            return;
        }

        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += flow.ofi_history.lookback(i);
        }
        double mean = sum / static_cast<double>(count);

        double sum_sq_diff = 0.0;
        for (size_t i = 0; i < count; ++i) {
            double diff = flow.ofi_history.lookback(i) - mean;
            sum_sq_diff += diff * diff;
        }
        
        double variance = sum_sq_diff / static_cast<double>(count - 1);
        double std_dev = std::sqrt(variance);

        if (std_dev == 0.0) [[unlikely]] {
            fv.metrics[IDX_OFI_ZSCORE_32] = 0.0;
        } else {
            fv.metrics[IDX_OFI_ZSCORE_32] = (flow.ofi_history.lookback(0) - mean) / std_dev;
        }
    }
};

} // namespace md::mpie
