#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include "../engine/worker.hpp"

namespace md::mpie {

class TelemetryExporter {
public:
    static void export_json(const std::vector<std::unique_ptr<Worker>>& workers, 
                            uint64_t elapsed_ms,
                            const std::string& os_name = "Windows",
                            const std::string& comp_name = "MSVC",
                            const std::string& build_type = "Release",
                            const std::string& filepath = "runtime_benchmark.json") 
    {
        using json = nlohmann::json;
        json root;

        root["metadata"] = {
            {"os", os_name},
            {"compiler", comp_name},
            {"build", build_type},
            {"engine_version", "2.0.0"},
            {"schema_version", 1}
        };

        uint64_t total_processed = 0;
        std::vector<uint64_t> worker_counts;
        
        json workers_json = json::array();
        
        for (const auto& worker : workers) {
            const auto& diag = worker->get_diagnostics();
            uint64_t processed = diag.processed_count.load(std::memory_order_relaxed);
            total_processed += processed;
            worker_counts.push_back(processed);

            uint64_t active_ns = diag.active_time_ns.load(std::memory_order_relaxed);
            uint64_t idle_ns = diag.idle_time_ns.load(std::memory_order_relaxed);
            uint64_t total_ns = active_ns + idle_ns;
            double util_pct = (total_ns > 0) ? (static_cast<double>(active_ns) / static_cast<double>(total_ns)) * 100.0 : 0.0;

            uint64_t peak_queue = diag.peak_occupancy.load(std::memory_order_relaxed);
            uint64_t occ_sum = diag.occupancy_sum.load(std::memory_order_relaxed);
            uint64_t occ_samples = diag.occupancy_samples.load(std::memory_order_relaxed);
            size_t avg_queue = occ_samples > 0 ? occ_sum / occ_samples : 0;

            // Calculate percentiles
            uint64_t total_samples = diag.latency_samples.load(std::memory_order_relaxed);
            uint64_t p50_ns = 0, p95_ns = 0, p99_ns = 0;
            
            if (total_samples > 0) {
                uint64_t accum = 0;
                bool p50_found = false, p95_found = false, p99_found = false;
                for (size_t i = 0; i < WorkerDiagnostics::LATENCY_BUCKETS; ++i) {
                    accum += diag.latency_histogram[i].load(std::memory_order_relaxed);
                    if (!p50_found && accum >= total_samples * 0.5) {
                        p50_ns = i * WorkerDiagnostics::BUCKET_WIDTH_NS + (WorkerDiagnostics::BUCKET_WIDTH_NS / 2);
                        p50_found = true;
                    }
                    if (!p95_found && accum >= total_samples * 0.95) {
                        p95_ns = i * WorkerDiagnostics::BUCKET_WIDTH_NS + (WorkerDiagnostics::BUCKET_WIDTH_NS / 2);
                        p95_found = true;
                    }
                    if (!p99_found && accum >= total_samples * 0.99) {
                        p99_ns = i * WorkerDiagnostics::BUCKET_WIDTH_NS + (WorkerDiagnostics::BUCKET_WIDTH_NS / 2);
                        p99_found = true;
                    }
                }
            }

            uint64_t min_ns = diag.min_latency_ns.load(std::memory_order_relaxed);
            if (min_ns == std::numeric_limits<uint64_t>::max()) min_ns = 0;

            workers_json.push_back({
                {"worker_id", worker->get_id()},
                {"core", diag.pinned_core.load(std::memory_order_relaxed)},
                {"processed", processed},
                {"utilization_pct", util_pct},
                {"queue_peak", peak_queue},
                {"queue_avg", avg_queue},
                {"latency_ns", {
                    {"min", min_ns},
                    {"p50", p50_ns},
                    {"p95", p95_ns},
                    {"p99", p99_ns},
                    {"max", diag.max_latency_ns.load(std::memory_order_relaxed)}
                }}
            });
        }

        // Calculate load imbalance standard deviation
        double mean = 0.0;
        if (!worker_counts.empty()) {
            mean = static_cast<double>(total_processed) / worker_counts.size();
        }
        
        double variance = 0.0;
        for (uint64_t count : worker_counts) {
            variance += (count - mean) * (count - mean);
        }
        if (!worker_counts.empty()) {
            variance /= worker_counts.size();
        }
        double std_dev = std::sqrt(variance);
        double imbalance_pct = (mean > 0) ? (std_dev / mean) * 100.0 : 0.0;

        double throughput = (elapsed_ms > 0) ? (static_cast<double>(total_processed) / (elapsed_ms / 1000.0)) : 0.0;

        root["performance"] = {
            {"global_throughput", throughput},
            {"elapsed_ms", elapsed_ms},
            {"load_imbalance_pct", imbalance_pct},
            {"load_imbalance_std_dev", std_dev}
        };

        root["workers"] = workers_json;

        std::ofstream file(filepath, std::ios::trunc);
        if (file.is_open()) {
            file << root.dump(4);
            file.close();
        }
    }
};

} // namespace md::mpie
