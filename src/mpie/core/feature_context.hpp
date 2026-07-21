#pragma once
#include "market_event.hpp"
#include "symbol_state.hpp"

namespace md::mpie {

struct alignas(64) FeatureContext {
    const MarketEvent* current_event;
    const MarketEvent* previous_event;
    const SymbolState* symbol_state; // Gives read-only access to the sub-states & ring buffers
    
    uint32_t symbol_id;
    uint64_t market_timestamp;
    uint64_t state_update_latency_ns; // Populated by worker diagnostics instrumentation
};

// Enforce standard layout to prevent compiler padding adjustments on the stack
static_assert(std::is_standard_layout_v<FeatureContext>, "FeatureContext must maintain standard layout constraints!");

} // namespace md::mpie
