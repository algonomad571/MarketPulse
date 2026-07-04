#include "context_builder.hpp"

namespace md::mpie {

FeatureContext ContextBuilder::build(const MarketEvent& current_event, 
                                     const MarketEvent& previous_event, 
                                     const SymbolState& symbol_state, 
                                     uint64_t state_update_latency_ns) const noexcept {
    return FeatureContext{
        current_event,
        previous_event,
        symbol_state,
        current_event.symbol_id,
        0, // _pad1
        current_event.timestamp,
        state_update_latency_ns,
        {0, 0} // _pad2
    };
}

} // namespace md::mpie
