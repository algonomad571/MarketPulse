#include "state_manager.hpp"

namespace md::mpie {

StateManager::StateManager(uint32_t symbol_universe_size) {
    // Dynamic at startup, zero allocation on the hot path.
    // L3 cache is optimized by allocating only what the universe size demands.
    pre_allocated_vector_.resize(symbol_universe_size);
}

void StateManager::update_state(const MarketEvent& event) noexcept {
    auto& state = pre_allocated_vector_[event.symbol_id];
    
    if (!state.initialized) {
        state.last_event = event;
        state.initialized = true;
    }
    
    state.price_state.current_bid = event.bid_price;
    state.price_state.current_ask = event.ask_price;
    state.book_state.bid_size = event.bid_size;
    state.book_state.ask_size = event.ask_size;
    
    if (event.event_type == EventType::Trade) {
        state.price_state.last_trade_price = event.trade_price;
        state.price_state.update_count++;
        state.trade_state.price_history.push(event.trade_price);
        state.trade_state.volume_history.push(event.trade_size);
    }
}

} // namespace md::mpie
