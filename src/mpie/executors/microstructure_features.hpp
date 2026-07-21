#pragma once
#include "feature_plugin.hpp"
#include <cmath>

namespace md::mpie {

struct MultiLevelImbalanceL5Executor {
    static constexpr std::string_view name() noexcept { return "MultiLevelImbalanceL5"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& book = ctx->symbol_state->book;
        double bid_vol = 0.0;
        double ask_vol = 0.0;
        
        for (size_t i = 0; i < 5; ++i) {
            bid_vol += static_cast<double>(book.bid_sizes[i]);
            ask_vol += static_cast<double>(book.ask_sizes[i]);
        }
        
        double total_vol = bid_vol + ask_vol;
        if (total_vol == 0.0) [[unlikely]] {
            fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5] = 0.0;
        } else {
            fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5] = (bid_vol - ask_vol) / total_vol;
        }
    }
};

struct BookPressureRatioExecutor {
    static constexpr std::string_view name() noexcept { return "BookPressureRatio"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto& book = ctx->symbol_state->book;
        double bid_pressure = 0.0;
        double ask_pressure = 0.0;
        
        double mid = fv.metrics[IDX_MID_PRICE];
        if (mid <= 0.0) [[unlikely]] {
            fv.metrics[IDX_BOOK_PRESSURE_RATIO] = 0.0;
            return;
        }
        
        for (size_t i = 0; i < 5; ++i) {
            if (book.bids[i] > 0.0) {
                double dist = (mid - book.bids[i]) / mid;
                double w = 1.0 / (1.0 + dist * 10000.0); 
                bid_pressure += static_cast<double>(book.bid_sizes[i]) * w;
            }
            if (book.asks[i] > 0.0) {
                double dist = (book.asks[i] - mid) / mid;
                double w = 1.0 / (1.0 + dist * 10000.0);
                ask_pressure += static_cast<double>(book.ask_sizes[i]) * w;
            }
        }
        
        double total_pressure = bid_pressure + ask_pressure;
        if (total_pressure == 0.0) [[unlikely]] {
            fv.metrics[IDX_BOOK_PRESSURE_RATIO] = 0.0;
        } else {
            fv.metrics[IDX_BOOK_PRESSURE_RATIO] = std::log((bid_pressure + 1.0) / (ask_pressure + 1.0));
        }
    }
};

struct TradeFlowImbalanceExecutor {
    static constexpr std::string_view name() noexcept { return "TradeFlowImbalance"; }
    static constexpr uint32_t version() noexcept { return 1; }

    inline void compute(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        const auto* ev = ctx->current_event;
        if (ev->event_type != EventType::Trade || ev->trade_size == 0) {
            fv.metrics[IDX_TRADE_FLOW_IMBALANCE] = 0.0;
            fv.metrics[IDX_EFFECTIVE_SPREAD] = 0.0;
            return;
        }
        
        double mid = fv.metrics[IDX_MID_PRICE];
        if (mid <= 0.0) [[unlikely]] {
            fv.metrics[IDX_TRADE_FLOW_IMBALANCE] = 0.0;
            fv.metrics[IDX_EFFECTIVE_SPREAD] = 0.0;
            return;
        }
        
        double trade_price = ev->trade_price;
        double trade_size = static_cast<double>(ev->trade_size);
        
        double direction = 0.0;
        if (trade_price > mid) direction = 1.0;
        else if (trade_price < mid) direction = -1.0;
        
        fv.metrics[IDX_TRADE_FLOW_IMBALANCE] = direction * trade_size;
        fv.metrics[IDX_EFFECTIVE_SPREAD] = 2.0 * std::abs(trade_price - mid);
    }
};

} // namespace md::mpie
