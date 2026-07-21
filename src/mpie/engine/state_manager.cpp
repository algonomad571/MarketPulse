#include "state_manager.hpp"

namespace md::mpie {

StateManager::StateManager(uint32_t universe_size) {
    // Dynamic-at-startup configuration. Zero allocations on hot path.
    states_.resize(universe_size);
}

void StateManager::update_state(const MarketEvent& event) noexcept {
    auto& state = states_[event.symbol_id];
    
    if (event.event_type == EventType::Trade) {
        state.price.last_trade_price = event.trade_price;
        state.price.last_trade_size = event.trade_size;
        state.trade.price_history.push(event.trade_price);
        state.trade.volume_history.push(event.trade_size);
    } else {
        state.price.current_bid = event.bid_price;
        state.price.current_ask = event.ask_price;
        state.price.current_bid_size = event.bid_size;
        state.price.current_ask_size = event.ask_size;
        
        state.book.bids[0] = event.bid_price;
        state.book.asks[0] = event.ask_price;
        state.book.bid_sizes[0] = event.bid_size;
        state.book.ask_sizes[0] = event.ask_size;
        
        if (event.ask_price > 0.0 && event.bid_price > 0.0) {
            state.book.implied_mid = (event.bid_price + event.ask_price) / 2.0;
            state.book.mid_history.push(state.book.implied_mid);
        }
    }
    
    state.last_event = event;
    state.is_initialized = true;
}

} // namespace md::mpie
