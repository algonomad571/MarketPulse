#pragma once

#include <cstdint>

namespace md::mpie {

enum class EventType : uint8_t {
    L1 = 1,
    L2 = 2,
    Trade = 3
};

struct MarketEvent
{
    uint64_t timestamp;
    uint32_t symbol_id;
    EventType event_type;
    double bid_price;
    double ask_price;
    uint64_t bid_size;
    uint64_t ask_size;
    double trade_price;
    uint64_t trade_size;
};

} // namespace md::mpie
