#include "mock_feed.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

namespace md {

MockFeed::MockFeed(const std::vector<std::string>& symbols,
                   std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> output_queue)
    : symbols_(symbols), output_queue_(output_queue) {
    
    symbol_states_.reserve(symbols_.size());
    
    for (size_t i = 0; i < symbols_.size(); ++i) {
        SymbolState state;
        state.rng.seed(static_cast<uint64_t>(i) + 12345);
        
        // Initialize book levels
        state.bid_levels.reserve(10);
        state.ask_levels.reserve(10);
        
        double bid_base = state.mid_price - state.spread / 2.0;
        double ask_base = state.mid_price + state.spread / 2.0;
        
        for (int level = 0; level < 10; ++level) {
            double bid_price = bid_base - level * 0.01;
            double ask_price = ask_base + level * 0.01;
            double size = state.size_dist(state.rng) * 100.0; // 100x multiplier
            
            state.bid_levels.emplace_back(bid_price, size);
            state.ask_levels.emplace_back(ask_price, size);
        }
        
        symbol_states_.push_back(std::move(state));
    }
}

MockFeed::~MockFeed() {
    stop();
}

void MockFeed::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    
    worker_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        run_loop();
    });
    
    spdlog::info("MockFeed started for {} symbols", symbols_.size());
}

void MockFeed::stop() {
    if (!running_.exchange(false)) {
        return; // not running
    }
    
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->request_stop();
        worker_thread_->join();
    }
    worker_thread_.reset();
    
    spdlog::info("MockFeed stopped");
}

void MockFeed::set_rates(uint32_t l1_msgs_per_sec, uint32_t l2_msgs_per_sec, uint32_t trade_msgs_per_sec) {
    l1_rate_.store(l1_msgs_per_sec);
    l2_rate_.store(l2_msgs_per_sec);
    trade_rate_.store(trade_msgs_per_sec);
    
    spdlog::info("MockFeed rates updated: L1={}/s, L2={}/s, Trade={}/s", 
                 l1_msgs_per_sec, l2_msgs_per_sec, trade_msgs_per_sec);
}

void MockFeed::run_loop() {
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(5);
    
    while (running_.load()) {
        auto start_time = std::chrono::steady_clock::now();
        
        // Check for burst mode
        auto now = std::chrono::steady_clock::now();
        static auto last_burst_check = now;
        if (now - last_burst_check > std::chrono::seconds(15)) {
            // Start burst every 15 seconds
            burst_mode_.store(true);
            burst_start_ = now;
            last_burst_check = now;
        }
        
        if (burst_mode_.load() && (now - burst_start_) > burst_duration_) {
            burst_mode_.store(false);
        }
        
        // Rate multiplier during burst
        uint32_t rate_multiplier = burst_mode_.load() ? 10 : 1;
        
        uint32_t l1_rate = l1_rate_.load() * rate_multiplier;
        uint32_t l2_rate = l2_rate_.load() * rate_multiplier;
        uint32_t trade_rate = trade_rate_.load() * rate_multiplier;
        
        // Generate events for all symbols
        for (size_t i = 0; i < symbols_.size(); ++i) {
            const std::string& symbol = symbols_[i];
            
            // L1 events (proportionally distributed)
            if (l1_rate > 0) {
                uint32_t events_this_symbol = l1_rate / symbols_.size();
                if (i < (l1_rate % symbols_.size())) events_this_symbol++;
                
                for (uint32_t j = 0; j < events_this_symbol; ++j) {
                    generate_l1_event(symbol);
                }
            }
            
            // L2 events  
            if (l2_rate > 0) {
                uint32_t events_this_symbol = l2_rate / symbols_.size();
                if (i < (l2_rate % symbols_.size())) events_this_symbol++;
                
                for (uint32_t j = 0; j < events_this_symbol; ++j) {
                    generate_l2_event(symbol);
                }
            }
            
            // Trade events
            if (trade_rate > 0) {
                uint32_t events_this_symbol = trade_rate / symbols_.size();
                if (i < (trade_rate % symbols_.size())) events_this_symbol++;
                
                for (uint32_t j = 0; j < events_this_symbol; ++j) {
                    generate_trade_event(symbol);
                }
            }
        }
        
        // Print stats periodically
        if (now - last_stats_time > stats_interval) {
            spdlog::info("MockFeed stats: L1={}, L2={}, Trade={}, Total={}, Burst={}",
                         stats_.l1_count.load(), stats_.l2_count.load(), 
                         stats_.trade_count.load(), stats_.total_events.load(),
                         burst_mode_.load());
            last_stats_time = now;
        }
        
        // Sleep to maintain target rate (approximately 1000 Hz loop)
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto target_interval = std::chrono::milliseconds(1);
        if (elapsed < target_interval) {
            std::this_thread::sleep_for(target_interval - elapsed);
        }
    }
}

void MockFeed::generate_l1_event(const std::string& symbol) {
    auto symbol_idx = std::distance(symbols_.begin(), 
                                   std::find(symbols_.begin(), symbols_.end(), symbol));
    auto& state = symbol_states_[symbol_idx];
    
    // Random walk the mid price
    double price_change = state.price_walk(state.rng);
    state.mid_price += price_change;
    state.mid_price = std::max(0.01, state.mid_price); // Floor at 1 cent
    
    // Update spread based on volatility
    state.spread = std::max(0.001, 0.01 + std::abs(price_change) * 10.0);
    
    // Create L1 event
    RawEvent event;
    event.type = RawEvent::L1;
    event.symbol = symbol;
    event.timestamp_ns = get_timestamp_ns();
    event.bid_price = state.mid_price - state.spread / 2.0;
    event.ask_price = state.mid_price + state.spread / 2.0;
    event.bid_size = state.size_dist(state.rng) * 1000.0;
    event.ask_size = state.size_dist(state.rng) * 1000.0;
    event.sequence = state.sequence++;
    
    // Update state
    if (!state.bid_levels.empty()) {
        state.bid_levels[0] = {event.bid_price, event.bid_size};
    }
    if (!state.ask_levels.empty()) {
        state.ask_levels[0] = {event.ask_price, event.ask_size};
    }
    
    output_queue_->enqueue(event);
    stats_.l1_count.fetch_add(1);
    stats_.total_events.fetch_add(1);
    
    MetricsCollector::instance().increment_counter("mock_feed_l1_total");
}

void MockFeed::generate_l2_event(const std::string& symbol) {
    auto symbol_idx = std::distance(symbols_.begin(), 
                                   std::find(symbols_.begin(), symbols_.end(), symbol));
    auto& state = symbol_states_[symbol_idx];
    
    // Pick random level and side
    uint16_t level = state.level_dist(state.rng);
    Side side = (state.rng() % 2 == 0) ? Side::Bid : Side::Ask;
    
    auto& levels = (side == Side::Bid) ? state.bid_levels : state.ask_levels;
    if (level >= levels.size()) {
        level = levels.size() - 1;
    }
    
    // Generate action (80% update, 15% insert, 5% delete)
    BookAction action;
    uint32_t action_rand = state.rng() % 100;
    if (action_rand < 80) {
        action = BookAction::Update;
    } else if (action_rand < 95) {
        action = BookAction::Insert;
    } else {
        action = BookAction::Delete;
    }
    
    RawEvent event;
    event.type = RawEvent::L2;
    event.symbol = symbol;
    event.timestamp_ns = get_timestamp_ns();
    event.side = side;
    event.action = action;
    event.level = level;
    event.sequence = state.sequence++;
    
    if (action == BookAction::Delete) {
        event.price = levels[level].first;
        event.size = 0.0;
    } else {
        // Update or insert
        double base_price = (side == Side::Bid) ? 
            state.mid_price - state.spread / 2.0 - level * 0.01 :
            state.mid_price + state.spread / 2.0 + level * 0.01;
        
        event.price = base_price + state.price_walk(state.rng) * 0.1;
        event.size = state.size_dist(state.rng) * 500.0;
        
        // Update state
        levels[level] = {event.price, event.size};
    }
    
    output_queue_->enqueue(event);
    stats_.l2_count.fetch_add(1);
    stats_.total_events.fetch_add(1);
    
    MetricsCollector::instance().increment_counter("mock_feed_l2_total");
}

void MockFeed::generate_trade_event(const std::string& symbol) {
    auto symbol_idx = std::distance(symbols_.begin(), 
                                   std::find(symbols_.begin(), symbols_.end(), symbol));
    auto& state = symbol_states_[symbol_idx];
    
    RawEvent event;
    event.type = RawEvent::Trade;
    event.symbol = symbol;
    event.timestamp_ns = get_timestamp_ns();
    
    // Trade price near mid with some randomness
    event.trade_price = state.mid_price + state.price_walk(state.rng) * 0.5;
    event.trade_size = state.size_dist(state.rng) * 100.0;
    event.aggressor_side = (state.rng() % 2 == 0) ? 0 : 1; // Buy or Sell
    event.sequence = state.sequence++;
    
    output_queue_->enqueue(event);
    stats_.trade_count.fetch_add(1);
    stats_.total_events.fetch_add(1);
    
    MetricsCollector::instance().increment_counter("mock_feed_trade_total");
}

uint64_t MockFeed::get_timestamp_ns() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace md