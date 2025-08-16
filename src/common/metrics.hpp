#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace md {

class LatencyHistogram {
public:
    explicit LatencyHistogram(const std::vector<uint64_t>& buckets_ns);
    
    void record(uint64_t latency_ns);
    
    struct Percentiles {
        uint64_t p50;
        uint64_t p95;
        uint64_t p99;
        uint64_t p999;
        uint64_t max;
        uint64_t count;
    };
    
    Percentiles get_percentiles() const;
    void reset();

private:
    std::vector<uint64_t> buckets_;
    std::vector<std::atomic<uint64_t>> counts_;
    std::atomic<uint64_t> total_count_{0};
    std::atomic<uint64_t> max_value_{0};
};

class MetricsCollector {
public:
    static MetricsCollector& instance();
    
    // Counters
    void increment_counter(const std::string& name, uint64_t delta = 1);
    uint64_t get_counter(const std::string& name) const;
    
    // Gauges
    void set_gauge(const std::string& name, double value);
    double get_gauge(const std::string& name) const;
    
    // Histograms
    void record_latency(const std::string& name, uint64_t latency_ns);
    LatencyHistogram::Percentiles get_latency_percentiles(const std::string& name) const;
    
    // Get all metrics for export
    std::string get_prometheus_metrics() const;
    std::string get_json_metrics() const;
    
private:
    MetricsCollector() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::atomic<uint64_t>> counters_;
    std::unordered_map<std::string, std::atomic<double>> gauges_;
    std::unordered_map<std::string, std::unique_ptr<LatencyHistogram>> histograms_;
    
    std::vector<uint64_t> default_buckets_ = {
        100000, 500000, 1000000, 2000000, 5000000, 10000000, 50000000
    };
};

// RAII latency timer
class LatencyTimer {
public:
    explicit LatencyTimer(const std::string& metric_name);
    ~LatencyTimer();
    
    void cancel() { cancelled_ = true; }

private:
    std::string metric_name_;
    std::chrono::steady_clock::time_point start_;
    bool cancelled_ = false;
};

#define MEASURE_LATENCY(name) md::LatencyTimer _timer(name)

} // namespace md