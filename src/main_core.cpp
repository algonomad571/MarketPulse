#include "common/frame.hpp"
#include "common/symbol_registry.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "feed/mock_feed.hpp"
#include "normalize/normalizer.hpp"
#include "publisher/pub_server.hpp"
#include "recorder/recorder.hpp"
#include "replay/replayer.hpp"
#include "ctrl/control_server.hpp"

#include <boost/asio.hpp>
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>
#include <signal.h>
#include <memory>
#include <csignal>

namespace md {

class MarketDataCore {
public:
    MarketDataCore(const Config& config) : config_(config), io_context_(config.pipeline.publisher_lanes) {
        setup_components();
    }
    
    ~MarketDataCore() {
        stop();
    }
    
    void start() {
        spdlog::info("Starting MarketData Core System...");
        
        // Start components in order
        normalizer_->start();
        pub_server_->start();
        recorder_->start();
        control_server_->start();
        mock_feed_->start();
        
        spdlog::info("All components started successfully");
        running_ = true;
    }
    
    void stop() {
        if (!running_) return;
        
        spdlog::info("Stopping MarketData Core System...");
        
        mock_feed_->stop();
        control_server_->stop();
        recorder_->stop();
        pub_server_->stop();
        normalizer_->stop();
        
        io_context_.stop();
        
        running_ = false;
        spdlog::info("All components stopped");
    }
    
    void run() {
        // Start I/O context threads
        std::vector<std::thread> io_threads;
        for (size_t i = 0; i < config_.pipeline.publisher_lanes; ++i) {
            io_threads.emplace_back([this]() {
                io_context_.run();
            });
        }
        
        // Wait for shutdown signal
        std::unique_lock<std::mutex> lock(shutdown_mutex_);
        shutdown_cv_.wait(lock, [this] { return shutdown_requested_; });
        
        // Stop and wait for threads
        for (auto& thread : io_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    void request_shutdown() {
        {
            std::lock_guard<std::mutex> lock(shutdown_mutex_);
            shutdown_requested_ = true;
        }
        shutdown_cv_.notify_one();
    }

private:
    void setup_components() {
        spdlog::info("Setting up components...");
        
        // Initialize CRC32 table
        initialize_crc32_table();
        
        // Create shared queues
        feed_to_normalizer_ = std::make_shared<moodycamel::ConcurrentQueue<RawEvent>>(100000);
        normalizer_to_publisher_ = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);
        normalizer_to_recorder_ = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);
        
        // Create symbol registry
        symbol_registry_ = std::make_shared<SymbolRegistry>();
        
        // Create mock feed
        mock_feed_ = std::make_shared<MockFeed>(config_.feeds.default_symbols, feed_to_normalizer_);
        
        // Create normalizer
        normalizer_ = std::make_shared<Normalizer>(
            feed_to_normalizer_, 
            normalizer_to_publisher_, 
            symbol_registry_,
            config_.pipeline.normalizer_threads
        );
        
        // Create publisher
        pub_server_ = std::make_shared<PubServer>(
            io_context_, 
            config_.network.pubsub_port, 
            config_.security.token
        );
        
        // Create recorder
        recorder_ = std::make_shared<Recorder>(
            config_.storage.dir,
            normalizer_to_recorder_,
            config_.storage.roll_bytes,
            config_.storage.index_interval,
            config_.pipeline.recorder_fsync_ms
        );
        
        // Create replayer
        replayer_ = std::make_shared<Replayer>(config_.storage.dir, pub_server_);
        
        // Create control server
        control_server_ = std::make_shared<ControlServer>(
            io_context_,
            config_.network.ctrl_http_port,
            config_.network.ws_metrics_port,
            config_.security.token
        );
        
        // Set component references in control server
        control_server_->set_mock_feed(mock_feed_);
        control_server_->set_normalizer(normalizer_);
        control_server_->set_pub_server(pub_server_);
        control_server_->set_recorder(recorder_);
        control_server_->set_replayer(replayer_);
        control_server_->set_symbol_registry(symbol_registry_);
        
        // Setup frame distribution from normalizer to publisher and recorder
        setup_frame_distribution();
        
        spdlog::info("Components setup complete");
    }
    
    void setup_frame_distribution() {
        // Create a thread to distribute frames from normalizer to publisher and recorder
        distribution_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
            Frame frame;
            const size_t batch_size = 100;
            std::vector<Frame> frames_batch;
            frames_batch.reserve(batch_size);
            
            while (running_ && !token.stop_requested()) {
                size_t dequeued = normalizer_to_publisher_->try_dequeue_bulk(frames_batch.data(), batch_size);
                
                if (dequeued == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }
                
                for (size_t i = 0; i < dequeued; ++i) {
                    const auto& frame = frames_batch[i];
                    
                    // Send to publisher
                    std::string topic = generate_topic(frame);
                    pub_server_->publish(topic, frame);
                    
                    // Send to recorder
                    normalizer_to_recorder_->enqueue(frame);
                }
                
                MetricsCollector::instance().increment_counter("frame_distribution_total", dequeued);
            }
        });
    }
    
    std::string generate_topic(const Frame& frame) {
        // Extract symbol_id and message type to generate topic
        uint32_t symbol_id = 0;
        std::string msg_type;
        
        std::visit([&symbol_id, &msg_type](const auto& body) {
            symbol_id = body.symbol_id;
            
            if constexpr (std::is_same_v<std::decay_t<decltype(body)>, L1Body>) {
                msg_type = "l1";
            } else if constexpr (std::is_same_v<std::decay_t<decltype(body)>, L2Body>) {
                msg_type = "l2";
            } else if constexpr (std::is_same_v<std::decay_t<decltype(body)>, TradeBody>) {
                msg_type = "trade";
            } else {
                msg_type = "other";
            }
        }, frame.body);
        
        // Get symbol name
        std::string symbol = std::string(symbol_registry_->by_id(symbol_id));
        if (symbol.empty()) {
            symbol = "UNKNOWN";
        }
        
        return msg_type + "." + symbol;
    }
    
    Config config_;
    boost::asio::io_context io_context_;
    
    // Shared queues
    std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> feed_to_normalizer_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> normalizer_to_publisher_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> normalizer_to_recorder_;
    
    // Components
    std::shared_ptr<SymbolRegistry> symbol_registry_;
    std::shared_ptr<MockFeed> mock_feed_;
    std::shared_ptr<Normalizer> normalizer_;
    std::shared_ptr<PubServer> pub_server_;
    std::shared_ptr<Recorder> recorder_;
    std::shared_ptr<Replayer> replayer_;
    std::shared_ptr<ControlServer> control_server_;
    
    // Distribution thread
    std::unique_ptr<std::jthread> distribution_thread_;
    
    // Shutdown coordination
    std::atomic<bool> running_{false};
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace md

// Global pointer for signal handler
std::unique_ptr<md::MarketDataCore> g_core;

void signal_handler(int signal) {
    spdlog::info("Received signal {}, shutting down...", signal);
    if (g_core) {
        g_core->request_shutdown();
    }
}

int main(int argc, char* argv[]) {
    try {
        // Setup logging
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");
        
        // Load configuration
        std::string config_path = "config.json";
        if (argc > 1) {
            config_path = argv[1];
        }
        
        md::Config config = md::Config::load_from_file(config_path);
        spdlog::info("Loaded configuration from {}", config_path);
        
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create and start core system
        g_core = std::make_unique<md::MarketDataCore>(config);
        g_core->start();
        
        spdlog::info("=== MarketData Core System is running ===");
        spdlog::info("Publisher port: {}", config.network.pubsub_port);
        spdlog::info("Control HTTP port: {}", config.network.ctrl_http_port);
        spdlog::info("WebSocket metrics port: {}", config.network.ws_metrics_port);
        spdlog::info("Data directory: {}", config.storage.dir);
        spdlog::info("Press Ctrl+C to stop");
        
        // Run main loop
        g_core->run();
        
        // Cleanup
        g_core->stop();
        g_core.reset();
        
        spdlog::info("MarketData Core System shutdown complete");
        return 0;
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}