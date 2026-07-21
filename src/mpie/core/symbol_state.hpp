#pragma once

#include "fixed_ring_buffer.hpp"
#include "market_event.hpp"
#include <cstdint>
#include <type_traits>

namespace md::mpie {

struct alignas(64) PriceState {
    double last_trade_price{0.0};
    uint64_t last_trade_size{0};
    double current_bid{0.0};
    double current_ask{0.0};
    uint64_t current_bid_size{0};
    uint64_t current_ask_size{0};
    uint64_t _pad[2]{0, 0}; // explicit pad to 64 bytes
};

struct alignas(64) BookState {
    double implied_mid{0.0};
    double vwap{0.0};
    double bids[5]{0.0};
    double asks[5]{0.0};
    uint64_t bid_sizes[5]{0};
    uint64_t ask_sizes[5]{0};
    FixedRingBuffer<double, 64> mid_history;
};

struct alignas(64) TradeState {
    FixedRingBuffer<double, 64> price_history;
    FixedRingBuffer<uint64_t, 64> volume_history;
};

struct alignas(64) FlowState {
    FixedRingBuffer<double, 64> ofi_history;
};

struct alignas(64) FeatureScratchpad {
    double temp_values[8]{0.0};
};

struct alignas(64) SymbolState {
    PriceState price;
    BookState book;
    TradeState trade;
    FlowState flow;
    FeatureScratchpad scratch;
    
    MarketEvent last_event{};
    bool is_initialized{false};
    uint8_t _pad[55]{0}; // explicit pad
};

// Contract checks
static_assert(std::is_standard_layout_v<SymbolState>, "SymbolState must be standard layout");

} // namespace md::mpie
