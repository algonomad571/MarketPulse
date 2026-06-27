#pragma once

#include "worker.hpp"
#include "../registry/feature_registry.hpp"
#include <vector>
#include <memory>

namespace md::mpie {

class FeatureEngine {
public:
    explicit FeatureEngine(uint32_t num_workers);
    ~FeatureEngine();

    void initialize();
    void start();
    void stop();

    // Inject events for testing/M1 demo
    void route_event(const MarketEvent& event);

    FeatureRegistry& get_registry() { return registry_; }

    void print_stats() const;

private:
    uint32_t num_workers_;
    FeatureRegistry registry_;
    std::vector<std::unique_ptr<Worker>> workers_;
    bool running_{false};
};

} // namespace md::mpie
