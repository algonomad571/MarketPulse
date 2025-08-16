#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace md {

struct NetworkConfig {
    uint16_t pubsub_port = 9100;
    uint16_t ctrl_http_port = 8080;
    uint16_t ws_metrics_port = 8080;
};

struct SecurityConfig {
    std::string token = "devtoken";
};

struct StorageConfig {
    std::string dir = "./data";
    uint64_t roll_bytes = 2147483648ULL; // 2GB
    uint32_t index_interval = 10000;
};

struct MetricsConfig {
    std::vector<uint64_t> histogram_buckets_ns = {
        100000, 500000, 1000000, 2000000, 5000000, 10000000
    };
};

struct PipelineConfig {
    uint32_t publisher_lanes = 8;
    uint32_t recorder_fsync_ms = 50;
    uint32_t normalizer_threads = 4;
};

struct FeedsConfig {
    std::vector<std::string> default_symbols = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
    bool mock_enabled = true;
    bool binance_enabled = false;
};

struct Config {
    NetworkConfig network;
    SecurityConfig security;
    StorageConfig storage;
    MetricsConfig metrics;
    PipelineConfig pipeline;
    FeedsConfig feeds;
    
    static Config load_from_file(const std::string& path);
    static Config default_config();
};

} // namespace md