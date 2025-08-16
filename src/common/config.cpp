#include "config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace md {

Config Config::load_from_file(const std::string& path) {
    Config config = default_config();
    
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return config; // Return default if file doesn't exist
        }
        
        nlohmann::json j;
        file >> j;
        
        if (j.contains("network")) {
            auto& net = j["network"];
            if (net.contains("pubsub_port")) config.network.pubsub_port = net["pubsub_port"];
            if (net.contains("ctrl_http_port")) config.network.ctrl_http_port = net["ctrl_http_port"];
            if (net.contains("ws_metrics_port")) config.network.ws_metrics_port = net["ws_metrics_port"];
        }
        
        if (j.contains("security")) {
            auto& sec = j["security"];
            if (sec.contains("token")) config.security.token = sec["token"];
        }
        
        if (j.contains("storage")) {
            auto& stor = j["storage"];
            if (stor.contains("dir")) config.storage.dir = stor["dir"];
            if (stor.contains("roll_bytes")) config.storage.roll_bytes = stor["roll_bytes"];
            if (stor.contains("index_interval")) config.storage.index_interval = stor["index_interval"];
        }
        
        if (j.contains("pipeline")) {
            auto& pipe = j["pipeline"];
            if (pipe.contains("publisher_lanes")) config.pipeline.publisher_lanes = pipe["publisher_lanes"];
            if (pipe.contains("recorder_fsync_ms")) config.pipeline.recorder_fsync_ms = pipe["recorder_fsync_ms"];
            if (pipe.contains("normalizer_threads")) config.pipeline.normalizer_threads = pipe["normalizer_threads"];
        }
        
        if (j.contains("feeds")) {
            auto& feeds = j["feeds"];
            if (feeds.contains("default_symbols")) {
                config.feeds.default_symbols.clear();
                for (const auto& sym : feeds["default_symbols"]) {
                    config.feeds.default_symbols.push_back(sym);
                }
            }
            if (feeds.contains("mock_enabled")) config.feeds.mock_enabled = feeds["mock_enabled"];
            if (feeds.contains("binance_enabled")) config.feeds.binance_enabled = feeds["binance_enabled"];
        }
    } catch (const std::exception& e) {
        // Log error and return default config
        return config;
    }
    
    return config;
}

Config Config::default_config() {
    return Config{};
}

} // namespace md