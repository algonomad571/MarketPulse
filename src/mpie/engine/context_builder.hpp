#pragma once
#include "../core/feature_context.hpp"

namespace md::mpie {

class ContextBuilder {
public:
    [[nodiscard]] FeatureContext build(
        const MarketEvent& current_event,
        const SymbolState& symbol_state,
        uint64_t state_update_latency_ns) const noexcept;
};

} // namespace md::mpie
