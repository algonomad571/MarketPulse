#include "feature_engine.hpp"
#include <iostream>
#include <cstdlib>

namespace md::mpie {

// ANSI Colors
constexpr std::string_view ANSI_RESET  = "\033[0m";
constexpr std::string_view ANSI_GREEN  = "\033[32m";
constexpr std::string_view ANSI_YELLOW = "\033[33m";
constexpr std::string_view ANSI_RED    = "\033[31m";
constexpr std::string_view ANSI_CYAN   = "\033[36m";

FeatureEngine::FeatureEngine(uint32_t num_workers)
    : num_workers_(num_workers) {
}

FeatureEngine::~FeatureEngine() {
    stop();
}

void FeatureEngine::startup_self_test() const {
    std::cout << ANSI_CYAN << "[Engine] " << ANSI_RESET << "Running Startup Self-Test (Fail-Fast)...\n";
    if (workers_.size() != num_workers_) {
        std::cerr << ANSI_RED << "[Engine] [FATAL] Worker allocation mismatch. Expected " << num_workers_ << " got " << workers_.size() << ".\n" << ANSI_RESET;
        std::abort();
    }
    for (const auto& w : workers_) {
        if (!w) {
            std::cerr << ANSI_RED << "[Engine] [FATAL] Null worker pointer detected.\n" << ANSI_RESET;
            std::abort();
        }
        if (!w->get_queue()) {
            std::cerr << ANSI_RED << "[Engine] [FATAL] Queue not allocated for worker " << w->get_id() << ".\n" << ANSI_RESET;
            std::abort();
        }
    }
    std::cout << ANSI_GREEN << "[Engine] [PASS] Structural invariants verified.\n" << ANSI_RESET;
}

void FeatureEngine::initialize(uint32_t universe_size) {
    std::cout << ANSI_CYAN << "[Engine] " << ANSI_RESET << "Initializing with " << num_workers_ << " workers.\n";
    
    if (!registry_.validate()) {
        std::cerr << ANSI_RED << "[Registry] [FATAL] Registry validation failed.\n" << ANSI_RESET;
        std::abort();
    }
    
    shm_publisher_ = std::make_unique<ShmPublisher>(universe_size);
    
    for (uint32_t i = 0; i < num_workers_; ++i) {
        workers_.push_back(std::make_unique<Worker>(i, universe_size, shm_publisher_.get()));
    }

    startup_self_test();
}

void FeatureEngine::start() noexcept {
    if (running_) return;
    std::cout << ANSI_CYAN << "[Engine] " << ANSI_RESET << "Starting workers...\n";
    for (auto& worker : workers_) {
        worker->start();
    }
    running_ = true;
}

void FeatureEngine::stop() noexcept {
    if (!running_) return;
    std::cout << ANSI_CYAN << "[Engine] " << ANSI_RESET << "Stopping workers...\n";
    for (auto& worker : workers_) {
        worker->stop();
    }
    running_ = false;
}

void FeatureEngine::route_event(const MarketEvent& event) noexcept {
    if (workers_.empty()) [[unlikely]] return;
    uint32_t worker_idx = event.symbol_id % workers_.size();
    auto& worker = workers_[worker_idx];
    worker->get_queue()->enqueue(event);
}

bool FeatureEngine::try_route_event(const MarketEvent& event) noexcept {
    if (workers_.empty()) [[unlikely]] return false;
    
    uint32_t worker_idx = event.symbol_id % workers_.size();
    auto& worker = workers_[worker_idx];
    
    if (worker->get_queue()->try_enqueue(event)) [[likely]] {
        return true;
    } else {
        worker->get_diagnostics().push_failures.fetch_add(1, std::memory_order_relaxed);
        worker->get_diagnostics().rejected_events.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

uint64_t FeatureEngine::get_total_processed() const noexcept {
    uint64_t total = 0;
    for (const auto& worker : workers_) {
        total += worker->get_diagnostics().processed_count.load(std::memory_order_relaxed);
    }
    return total;
}

void FeatureEngine::print_stats() const {
    uint64_t total = get_total_processed();
    for (const auto& worker : workers_) {
        std::cout << ANSI_CYAN << "[Worker " << worker->get_id() << "] " << ANSI_RESET 
                  << worker->get_diagnostics().processed_count.load(std::memory_order_relaxed) 
                  << " events processed.\n";
    }
    std::cout << ANSI_CYAN << "[Engine] " << ANSI_RESET << "Total engine processed: " << total << " events.\n";
}

} // namespace md::mpie
