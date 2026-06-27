#include "mpie/engine/feature_engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace md::mpie;

int main() {
    std::cout << "Starting MarketPulse Intelligence Engine (MPIE) M1 Standalone\n";
    
    // We will use 4 workers for the test
    FeatureEngine engine(4);
    
    // Register a dummy feature to test the registry
    FeatureDescriptor dummy{
        .name = "DummyFeature",
        .feature_version = 1,
        .schema_version = 1,
        .category = FeatureCategory::Price,
        .pass = ExecutionPass::Pass1,
        .stateful = false,
        .lookback_window = 0,
        .complexity = Complexity::O1,
        .dependencies = {}
    };
    engine.get_registry().register_feature(dummy);
    
    engine.initialize();
    engine.start();
    
    std::cout << "Injecting 1,000,000 synthetic MarketEvents...\n";
    
    for (uint32_t i = 0; i < 1'000'000; ++i) {
        MarketEvent ev{
            .timestamp = i * 1000ULL,
            .symbol_id = static_cast<uint32_t>(i % 100), // 100 symbols
            .event_type = EventType::Trade,
            .bid_price = 100.0,
            .ask_price = 101.0,
            .bid_size = 10,
            .ask_size = 10,
            .trade_price = 100.5,
            .trade_size = 5
        };
        engine.route_event(ev);
    }
    
    std::cout << "Finished injecting events. Waiting for workers to process...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    engine.print_stats();
    
    std::cout << "Shutting down engine...\n";
    engine.stop();
    
    std::cout << "MPIE shutdown complete.\n";
    return 0;
}
