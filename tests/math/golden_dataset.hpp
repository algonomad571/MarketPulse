#pragma once
#include "mpie/core/market_event.hpp"
#include <vector>
#include <cmath>
#include <string>

namespace md::mpie::tests {

struct ExpectedOutputs {
    double spread{0.0};
    double mid{0.0};
    double micro{0.0};
    double wap{0.0};
    double log_ret{0.0};
};

struct GoldenScenario {
    std::string description;
    MarketEvent event;
    ExpectedOutputs expected;
};

inline std::vector<GoldenScenario> generate_golden_dataset() {
    std::vector<GoldenScenario> dataset;
    std::vector<double> history;
    
    auto add_event = [&](const std::string& desc, double bid, double ask, uint64_t bid_sz, uint64_t ask_sz) {
        MarketEvent ev{};
        ev.timestamp = 1000 + dataset.size();
        ev.symbol_id = 1;
        ev.event_type = EventType::L2;
        ev.bid_price = bid;
        ev.ask_price = ask;
        ev.bid_size = bid_sz;
        ev.ask_size = ask_sz;
        ev.trade_price = 0.0;
        ev.trade_size = 0;
        
        ExpectedOutputs ex{};
        ex.spread = ask - bid;
        ex.mid = (bid + ask) / 2.0;
        
        double total_sz = static_cast<double>(bid_sz + ask_sz);
        if (total_sz == 0.0) {
            ex.micro = ex.mid;
            ex.wap = ex.mid;
        } else {
            ex.micro = ((bid * static_cast<double>(ask_sz)) + (ask * static_cast<double>(bid_sz))) / total_sz;
            ex.wap = ((bid * static_cast<double>(bid_sz)) + (ask * static_cast<double>(ask_sz))) / total_sz;
        }
        
        if (ask > 0.0 && bid > 0.0) {
            history.push_back(ex.mid);
        }
        
        if (history.size() < 2) {
            ex.log_ret = 0.0;
        } else {
            double cur = history.back();
            double prv = history[history.size() - 2];
            if (prv <= 0.0 || cur <= 0.0) {
                ex.log_ret = 0.0;
            } else {
                ex.log_ret = std::log(cur / prv);
            }
        }
        
        dataset.push_back({desc, ev, ex});
    };

    // 1-5. Normal Market Sequence
    add_event("Normal Tick 1", 100.0, 100.2, 50, 50);
    add_event("Normal Tick 2", 100.1, 100.2, 80, 20);
    add_event("Normal Tick 3", 100.1, 100.4, 20, 80);
    add_event("Normal Tick 4", 100.2, 100.3, 50, 50);
    add_event("Normal Tick 5", 100.2, 100.5, 90, 10);

    // 6-10. Spreads
    add_event("Tight Spread", 100.3, 100.31, 100, 100);
    add_event("Wide Spread", 90.0, 110.0, 100, 100);
    add_event("Crossed Market", 100.5, 100.4, 50, 50);
    add_event("Locked Market", 100.0, 100.0, 50, 50);
    
    // 11-15. Volume Imbalances
    add_event("Large Bid Volume", 100.0, 100.2, 10000, 10);
    add_event("Large Ask Volume", 100.0, 100.2, 10, 10000);
    add_event("Zero Bid Volume", 100.0, 100.2, 0, 100);
    add_event("Zero Ask Volume", 100.0, 100.2, 100, 0);
    add_event("Zero Total Volume", 100.0, 100.2, 0, 0);

    // 16-20. Price Boundaries
    add_event("Tiny Prices", 1e-8, 1e-8 + 1e-9, 10, 10);
    add_event("Large Prices", 1e6, 1e6 + 0.5, 10, 10);
    add_event("Negative Prices", -5.0, -4.0, 10, 10);
    add_event("Zero Prices", 0.0, 0.0, 10, 10);
    add_event("Zero Bid Price Only", 0.0, 100.0, 10, 10);

    // 21-30. Long Trend for Log Return Validation
    add_event("Trend Reset", 100.0, 100.2, 50, 50);
    add_event("Rising 1", 100.1, 100.3, 50, 50);
    add_event("Rising 2", 100.2, 100.4, 50, 50);
    add_event("Rising 3", 100.3, 100.5, 50, 50);
    add_event("Falling 1", 100.2, 100.4, 50, 50);
    add_event("Falling 2", 100.1, 100.3, 50, 50);
    add_event("Falling 3", 100.0, 100.2, 50, 50);
    add_event("Constant 1", 100.0, 100.2, 50, 50);
    add_event("Constant 2", 100.0, 100.2, 50, 50);
    add_event("Sudden Jump", 150.0, 150.2, 50, 50);

    return dataset;
}

} // namespace md::mpie::tests
