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

    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;

    void initialize(uint32_t universe_size);
    void start() noexcept;
    void stop() noexcept;

    void route_event(const MarketEvent& event) noexcept;
    bool try_route_event(const MarketEvent& event) noexcept;

    [[nodiscard]] FeatureRegistry& get_registry() noexcept { return registry_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Worker>>& get_workers() const noexcept { return workers_; }
    [[nodiscard]] uint64_t get_total_processed() const noexcept;

    void print_stats() const;

private:
    void startup_self_test() const;

    uint32_t num_workers_;
    FeatureRegistry registry_;
    std::vector<std::unique_ptr<Worker>> workers_;
    bool running_{false};
};

} // namespace md::mpie
