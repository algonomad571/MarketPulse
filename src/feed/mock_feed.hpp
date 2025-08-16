#pragma once

#include "../common/frame.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <random>
#include <chrono>

// Forward declare for lock-free queue
namespace moodycamel {
    template<typename T>
    class ConcurrentQueue;
}

namespace md {

struct RawEvent {
    enum Type { L1, L2, Trade };
    Type type;
    std::string symbol;
    uint64_t timestamp_ns;
    
    // L1 data
    double bid_price = 0.0, ask_price = 0.0;
    double bid_size = 0.0, ask_size = 0.0;
    
    // L2 data  
    BookAction action = BookAction::Update;
    Side side = Side::Bid;
    uint16_t level = 0;
    double price = 0.0, size = 0.0;
    
    // Trade data
    double trade_price = 0.0, trade_size = 0.0;
    uint8_t aggressor_side = 255; // unknown
    
    uint64_t sequence = 0;
};

class MockFeed {
public:
    MockFeed(const std::vector<std::string>& symbols, 
             std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> output_queue);
    ~MockFeed();
    
    void start();
    void stop();
    void set_rates(uint32_t l1_msgs_per_sec, uint32_t l2_msgs_per_sec, uint32_t trade_msgs_per_sec);
    
    struct Stats {
        std::atomic<uint64_t> l1_count{0};
        std::atomic<uint64_t> l2_count{0};
        std::atomic<uint64_t> trade_count{0};
        std::atomic<uint64_t> total_events{0};
    };
    
    const Stats& get_stats() const { return stats_; }

private:
    void run_loop();
    void generate_l1_event(const std::string& symbol);
    void generate_l2_event(const std::string& symbol);
    void generate_trade_event(const std::string& symbol);
    
    uint64_t get_timestamp_ns() const;
    
    std::vector<std::string> symbols_;
    std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> output_queue_;
    
    std::unique_ptr<std::jthread> worker_thread_;
    std::atomic<bool> running_{false};
    
    // Rate control
    std::atomic<uint32_t> l1_rate_{50000};   // 50k/sec default
    std::atomic<uint32_t> l2_rate_{30000};   // 30k/sec default  
    std::atomic<uint32_t> trade_rate_{5000}; // 5k/sec default
    
    // Per-symbol state
    struct SymbolState {
        double mid_price = 100.0;
        double spread = 0.01;
        std::vector<std::pair<double, double>> bid_levels; // price, size
        std::vector<std::pair<double, double>> ask_levels;
        uint64_t sequence = 1;
        
        // Random number generators
        std::mt19937_64 rng;
        std::normal_distribution<double> price_walk{0.0, 0.001}; // 0.1% moves
        std::exponential_distribution<double> size_dist{1.0};
        std::uniform_int_distribution<int> level_dist{0, 9}; // L2 levels 0-9
    };
    
    std::vector<SymbolState> symbol_states_;
    Stats stats_;
    
    // Burst mode simulation
    std::atomic<bool> burst_mode_{false};
    std::chrono::steady_clock::time_point burst_start_;
    std::chrono::milliseconds burst_duration_{1000}; // 1 second bursts
};

} // namespace md