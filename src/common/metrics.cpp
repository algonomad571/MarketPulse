#include "metrics.hpp"
#include <algorithm>
#include <sstream>
#include <nlohmann/json.hpp>

namespace md {

LatencyHistogram::LatencyHistogram(const std::vector<uint64_t>& buckets_ns) 
    : buckets_(buckets_ns) {
    counts_.resize(buckets_.size() + 1);  // +1 for overflow bucket
    for (auto& count : counts_) {
        count.store(0);
    }
}

void LatencyHistogram::record(uint64_t latency_ns) {
    total_count_.fetch_add(1);
    
    // Update max
    uint64_t current_max = max_value_.load();
    while (latency_ns > current_max) {
        if (max_value_.compare_exchange_weak(current_max, latency_ns)) {
            break;
        }
    }
    
    // Find bucket
    size_t bucket_idx = 0;
    for (size_t i = 0; i < buckets_.size(); ++i) {
        if (latency_ns <= buckets_[i]) {
            bucket_idx = i;
            break;
        }
    }
    if (bucket_idx == 0 && latency_ns > buckets_.back()) {
        bucket_idx = buckets_.size();  // overflow bucket
    }
    
    counts_[bucket_idx].fetch_add(1);
}

LatencyHistogram::Percentiles LatencyHistogram::get_percentiles() const {
    uint64_t total = total_count_.load();
    if (total == 0) {
        return {0, 0, 0, 0, 0, 0};
    }
    
    std::vector<uint64_t> cumulative(counts_.size());
    for (size_t i = 0; i < counts_.size(); ++i) {
        cumulative[i] = counts_[i].load();
        if (i > 0) cumulative[i] += cumulative[i-1];
    }
    
    auto find_percentile = [&](double p) -> uint64_t {
        uint64_t target = static_cast<uint64_t>(total * p / 100.0);
        for (size_t i = 0; i < cumulative.size(); ++i) {
            if (cumulative[i] >= target) {
                return (i < buckets_.size()) ? buckets_[i] : max_value_.load();
            }
        }
        return max_value_.load();
    };
    
    return {
        find_percentile(50.0),   // p50
        find_percentile(95.0),   // p95
        find_percentile(99.0),   // p99
        find_percentile(99.9),   // p999
        max_value_.load(),       // max
        total                    // count
    };
}

void LatencyHistogram::reset() {
    total_count_.store(0);
    max_value_.store(0);
    for (auto& count : counts_) {
        count.store(0);
    }
}

MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector instance;
    return instance;
}

void MetricsCollector::increment_counter(const std::string& name, uint64_t delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name].fetch_add(delta);
}

uint64_t MetricsCollector::get_counter(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second.load() : 0;
}

void MetricsCollector::set_gauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name].store(value);
}

double MetricsCollector::get_gauge(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    return (it != gauges_.end()) ? it->second.load() : 0.0;
}

void MetricsCollector::record_latency(const std::string& name, uint64_t latency_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& histogram = histograms_[name];
    if (!histogram) {
        histogram = std::make_unique<LatencyHistogram>(default_buckets_);
    }
    histogram->record(latency_ns);
}

LatencyHistogram::Percentiles MetricsCollector::get_latency_percentiles(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = histograms_.find(name);
    return (it != histograms_.end()) ? it->second->get_percentiles() : LatencyHistogram::Percentiles{};
}

std::string MetricsCollector::get_json_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json metrics;
    metrics["timestamp_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Counters
    nlohmann::json counters_json;
    for (const auto& [name, counter] : counters_) {
        counters_json[name] = counter.load();
    }
    metrics["counters"] = counters_json;
    
    // Gauges
    nlohmann::json gauges_json;
    for (const auto& [name, gauge] : gauges_) {
        gauges_json[name] = gauge.load();
    }
    metrics["gauges"] = gauges_json;
    
    // Histograms
    nlohmann::json histograms_json;
    for (const auto& [name, histogram] : histograms_) {
        auto percentiles = histogram->get_percentiles();
        histograms_json[name] = {
            {"p50", percentiles.p50},
            {"p95", percentiles.p95},
            {"p99", percentiles.p99},
            {"p999", percentiles.p999},
            {"max", percentiles.max},
            {"count", percentiles.count}
        };
    }
    metrics["histograms"] = histograms_json;
    
    return metrics.dump();
}

std::string MetricsCollector::get_prometheus_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::stringstream ss;
    
    // Counters
    for (const auto& [name, counter] : counters_) {
        ss << "# TYPE " << name << " counter\n";
        ss << name << " " << counter.load() << "\n";
    }
    
    // Gauges
    for (const auto& [name, gauge] : gauges_) {
        ss << "# TYPE " << name << " gauge\n";
        ss << name << " " << gauge.load() << "\n";
    }
    
    // Histograms
    for (const auto& [name, histogram] : histograms_) {
        auto percentiles = histogram->get_percentiles();
        ss << "# TYPE " << name << " histogram\n";
        ss << name << "_p50 " << percentiles.p50 << "\n";
        ss << name << "_p95 " << percentiles.p95 << "\n";
        ss << name << "_p99 " << percentiles.p99 << "\n";
        ss << name << "_p999 " << percentiles.p999 << "\n";
        ss << name << "_max " << percentiles.max << "\n";
        ss << name << "_count " << percentiles.count << "\n";
    }
    
    return ss.str();
}

LatencyTimer::LatencyTimer(const std::string& metric_name) 
    : metric_name_(metric_name), start_(std::chrono::steady_clock::now()) {
}

LatencyTimer::~LatencyTimer() {
    if (!cancelled_) {
        auto end = std::chrono::steady_clock::now();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        MetricsCollector::instance().record_latency(metric_name_, duration_ns);
    }
}

} // namespace md