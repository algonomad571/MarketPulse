#include "../math/test_framework.hpp"
#include "mpie/observability/telemetry_exporter.hpp"
#include "mpie/engine/worker.hpp"
#include <filesystem>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

using namespace md::mpie;
using namespace md::mpie::tests;

void test_telemetry_export() {
    std::string test_file = "test_telemetry.json";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
    
    std::vector<std::unique_ptr<Worker>> workers;
    workers.push_back(std::make_unique<Worker>(0, 10));
    workers.push_back(std::make_unique<Worker>(1, 10));
    
    // Fake some data
    auto& diag0 = workers[0]->get_diagnostics();
    diag0.processed_count = 1000;
    diag0.active_time_ns = 50000;
    diag0.idle_time_ns = 50000;
    diag0.latency_histogram[5] = 100; // 500-599ns
    diag0.latency_samples = 100;
    
    auto& diag1 = workers[1]->get_diagnostics();
    diag1.processed_count = 1100;
    
    TelemetryExporter::export_json(workers, 100, "Windows", "MSVC", "Release", test_file);
    
    ASSERT_MATH_EQUAL("Observability", std::filesystem::exists(test_file) ? 1.0 : 0.0, 1.0, "JSON file created");
    
    std::ifstream f(test_file);
    nlohmann::json j = nlohmann::json::parse(f);
    
    double throughput = j["performance"]["global_throughput"];
    ASSERT_MATH_EQUAL("Observability", throughput, 21000.0, "Throughput calculation matches 2100 / 0.1s");
    
    double util = j["workers"][0]["utilization_pct"];
    ASSERT_MATH_EQUAL("Observability", util, 50.0, "Utilization 50%");
    
    uint64_t p50 = j["workers"][0]["latency_ns"]["p50"];
    ASSERT_MATH_EQUAL("Observability", static_cast<double>(p50), 550.0, "P50 latency calculated correctly");
    
    std::filesystem::remove(test_file);
}

void run_observability_tests() {
    test_telemetry_export();
}
