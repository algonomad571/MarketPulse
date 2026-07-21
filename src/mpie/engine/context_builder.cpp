#include "context_builder.hpp"

namespace md::mpie {

FeatureContext ContextBuilder::build(
    const MarketEvent& current_event,
    const SymbolState& symbol_state,
    uint64_t state_update_latency_ns) const noexcept 
{
    return FeatureContext{
        &current_event,
        &symbol_state.last_event,
        &symbol_state,
        current_event.symbol_id,
        current_event.timestamp,
        state_update_latency_ns
    };
}

} // namespace md::mpie
