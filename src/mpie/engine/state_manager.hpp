#pragma once

#include "../core/symbol_state.hpp"
#include "../core/market_event.hpp"
#include <vector>
#include <cstdint>

namespace md::mpie {

class StateManager {
public:
    explicit StateManager(uint32_t universe_size);

    StateManager(const StateManager&) = delete;
    StateManager& operator=(const StateManager&) = delete;

    // Hot-path state mutation (zero-allocation)
    void update_state(const MarketEvent& event) noexcept;
    
    // Constant time O(1) read-only access
    [[nodiscard]] inline const SymbolState& get_state(uint32_t symbol_id) const noexcept {
        return states_[symbol_id];
    }

private:
    std::vector<SymbolState> states_;
};

} // namespace md::mpie
