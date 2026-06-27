#include "feature_engine.hpp"
#include <iostream>
#include <cstdlib>

namespace md::mpie {

FeatureEngine::FeatureEngine(uint32_t num_workers)
    : num_workers_(num_workers) {
}

FeatureEngine::~FeatureEngine() {
    stop();
}

void FeatureEngine::initialize() {
    std::cout << "[FeatureEngine] Initializing with " << num_workers_ << " workers.\n";
    
    if (!registry_.validate()) {
        std::cerr << "[FeatureEngine] FATAL: Registry validation failed.\n";
        std::abort();
    }
    
    for (uint32_t i = 0; i < num_workers_; ++i) {
        workers_.push_back(std::make_unique<Worker>(i));
    }
}

void FeatureEngine::start() {
    if (running_) return;
    std::cout << "[FeatureEngine] Starting workers...\n";
    for (auto& worker : workers_) {
        worker->start();
    }
    running_ = true;
}

void FeatureEngine::stop() {
    if (!running_) return;
    std::cout << "[FeatureEngine] Stopping workers...\n";
    for (auto& worker : workers_) {
        worker->stop();
    }
    running_ = false;
}

void FeatureEngine::route_event(const MarketEvent& event) {
    if (workers_.empty()) return;
    
    // Deterministic routing by symbol_id for cache locality
    uint32_t worker_idx = event.symbol_id % workers_.size();
    
    auto queue = workers_[worker_idx]->get_queue();
    queue->enqueue(event);
}

void FeatureEngine::print_stats() const {
    uint64_t total = 0;
    for (const auto& worker : workers_) {
        total += worker->get_processed_count();
        std::cout << "Worker " << worker->get_id() << ": " 
                  << worker->get_processed_count() << " events processed.\n";
    }
    std::cout << "Total engine processed: " << total << " events.\n";
}

} // namespace md::mpie
