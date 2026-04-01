#include "common/frame.hpp"
#include "common/symbol_registry.hpp"
#include "common/config.hpp"
#include "common/metrics.hpp"
#include "common/crc32.hpp"
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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace md {

class MarketDataCore {
public:
    explicit MarketDataCore(const Config& config)
        : config_(config), io_context_(config.pipeline.publisher_lanes) {
        setup_components();
    }

    ~MarketDataCore() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) {
            spdlog::warn("System already running");
            return;
        }

        spdlog::info("Starting MarketData Core System...");

        try {
            spdlog::info("Starting Distributor...");
            setup_frame_distribution();

            spdlog::info("Starting Normalizer...");
            normalizer_->start();

            spdlog::info("Starting Publisher...");
            pub_server_->start();

            spdlog::info("Starting Recorder...");
            recorder_->start();

            spdlog::info("Starting Control Server...");
            control_server_->start();

            spdlog::info("Starting Mock Feed...");
            mock_feed_->start();

            spdlog::info("All components started successfully");

        } catch (const std::exception& e) {
            spdlog::error("Failed to start components: {}", e.what());
            spdlog::warn("Attempting to stop partially started components...");
            stop();
            throw std::runtime_error(std::string("System startup failed: ") + e.what());
        }
    }

    void stop() {
        const bool was_running = running_.exchange(false);
        if (!was_running) {
            spdlog::warn("Stop requested while system not marked running; cleaning up partially started components");
        }

        spdlog::info("Stopping MarketData Core System...");

        auto stop_component = [](auto& component, const std::string& name) {
            if (!component) {
                return;
            }

            spdlog::info("Stopping {}...", name);
            auto start = std::chrono::steady_clock::now();
            try {
                component->stop();
            } catch (const std::exception& e) {
                spdlog::error("Error stopping {}: {}", name, e.what());
            } catch (...) {
                spdlog::error("Unknown error stopping {}", name);
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            spdlog::info("{} stopped in {}ms", name, elapsed);
        };

        // Stop producer first so queues drain.
        stop_component(mock_feed_, "MockFeed");

        if (distribution_thread_) {
            distribution_thread_->request_stop();
            if (distribution_thread_->joinable()) {
                distribution_thread_->join();
            }
            distribution_thread_.reset();
            spdlog::info("Distribution thread stopped");
        }

        stop_component(recorder_, "Recorder");
        stop_component(pub_server_, "Publisher");
        stop_component(normalizer_, "Normalizer");
        stop_component(control_server_, "ControlServer");

        io_context_.stop();
        request_shutdown();

        spdlog::info("All components stopped gracefully");
    }

    void run() {
        spdlog::info("Starting {} IO context threads...", config_.pipeline.publisher_lanes);

        std::vector<std::thread> io_threads;
        io_threads.reserve(config_.pipeline.publisher_lanes);

        for (size_t i = 0; i < config_.pipeline.publisher_lanes; ++i) {
            io_threads.emplace_back([this, i]() {
                spdlog::debug("IO thread {} started", i);
                io_context_.run();
                spdlog::debug("IO thread {} finished", i);
            });
        }

        std::unique_lock<std::mutex> lock(shutdown_mutex_);
        shutdown_cv_.wait(lock, [this] { return shutdown_requested_.load(); });
        lock.unlock();

        io_context_.stop();

        for (auto& thread : io_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        spdlog::info("All IO threads stopped");
    }

    void request_shutdown() {
        {
            std::lock_guard<std::mutex> lock(shutdown_mutex_);
            shutdown_requested_.store(true);
        }
        shutdown_cv_.notify_all();
    }

    bool is_running() const {
        return running_.load();
    }

    void validate_health() const {
        if (!running_.load()) {
            throw std::runtime_error("System is not running");
        }

        if (!mock_feed_ || !normalizer_ || !pub_server_ || !recorder_ || !control_server_) {
            throw std::runtime_error("One or more components not initialized");
        }
    }

private:
    void setup_components() {
        spdlog::info("Setting up components...");

        initialize_crc32_table();

        feed_to_normalizer_ = std::make_shared<moodycamel::ConcurrentQueue<RawEvent>>(100000);
        normalizer_to_publisher_ = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);
        normalizer_to_recorder_ = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);

        symbol_registry_ = std::make_shared<SymbolRegistry>();

        mock_feed_ = std::make_shared<MockFeed>(config_.feeds.default_symbols, feed_to_normalizer_);

        normalizer_ = std::make_shared<Normalizer>(
            feed_to_normalizer_,
            normalizer_to_publisher_,
            symbol_registry_,
            config_.pipeline.normalizer_threads);

        recorder_ = std::make_shared<Recorder>(
            config_.storage.dir,
            normalizer_to_recorder_,
            config_.storage.roll_bytes,
            config_.storage.index_interval,
            config_.pipeline.recorder_fsync_ms);
        recorder_->set_symbol_registry(symbol_registry_);

        pub_server_ = std::make_shared<PubServer>(
            io_context_,
            config_.network.pubsub_port,
            config_.security.token);

        replayer_ = std::make_shared<Replayer>(config_.storage.dir, pub_server_, symbol_registry_);

        control_server_ = std::make_shared<ControlServer>(
            io_context_,
            config_.network.ctrl_http_port,
            config_.network.ws_metrics_port,
            config_.security.token);

        control_server_->set_mock_feed(mock_feed_);
        control_server_->set_normalizer(normalizer_);
        control_server_->set_pub_server(pub_server_);
        control_server_->set_recorder(recorder_);
        control_server_->set_replayer(replayer_);
        control_server_->set_symbol_registry(symbol_registry_);

        spdlog::info("Components setup complete");
    }

    void setup_frame_distribution() {
        if (distribution_thread_) {
            return;
        }

        distribution_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
            const size_t batch_size = 100;
            std::vector<Frame> frames_batch;
            frames_batch.reserve(batch_size);
            auto last_log_time = std::chrono::steady_clock::now();
            uint64_t routed_since_log = 0;

            while (running_.load() && !token.stop_requested()) {
                frames_batch.clear();
                Frame frame;
                while (frames_batch.size() < batch_size && normalizer_to_publisher_->try_dequeue(frame)) {
                    frames_batch.push_back(std::move(frame));
                }

                size_t dequeued = frames_batch.size();

                if (dequeued == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }

                for (size_t i = 0; i < dequeued; ++i) {
                    const auto& frame = frames_batch[i];

                    std::string topic = generate_topic(frame);
                    pub_server_->publish(topic, frame);

                    normalizer_to_recorder_->enqueue(frame);
                }

                MetricsCollector::instance().increment_counter("frame_distribution_total", dequeued);
                routed_since_log += dequeued;

                static uint64_t update_counter = 0;
                if (++update_counter % 100 == 0) {
                    MetricsCollector::instance().set_gauge("pipeline_feed_queue_approx",
                        feed_to_normalizer_->size_approx());
                    MetricsCollector::instance().set_gauge("pipeline_normalizer_to_publisher_queue_approx",
                        normalizer_to_publisher_->size_approx());
                    MetricsCollector::instance().set_gauge("pipeline_normalizer_to_recorder_queue_approx",
                        normalizer_to_recorder_->size_approx());
                }

                const auto now = std::chrono::steady_clock::now();
                if (now - last_log_time >= std::chrono::seconds(5)) {
                    spdlog::info(
                        "Flow[Distributor] routed={} feed_q={} norm_to_pub_q={} norm_to_rec_q={}",
                        routed_since_log,
                        feed_to_normalizer_->size_approx(),
                        normalizer_to_publisher_->size_approx(),
                        normalizer_to_recorder_->size_approx());
                    routed_since_log = 0;
                    last_log_time = now;
                }
            }
        });
    }

    std::string generate_topic(const Frame& frame) {
        uint32_t symbol_id = 0;
        std::string msg_type;

        std::visit([&symbol_id, &msg_type](const auto& body) {
            using T = std::decay_t<decltype(body)>;

            if constexpr (std::is_same_v<T, L1Body>) {
                symbol_id = body.symbol_id;
                msg_type = "l1";
            } else if constexpr (std::is_same_v<T, L2Body>) {
                symbol_id = body.symbol_id;
                msg_type = "l2";
            } else if constexpr (std::is_same_v<T, TradeBody>) {
                symbol_id = body.symbol_id;
                msg_type = "trade";
            } else {
                symbol_id = 0;
                msg_type = "other";
            }
        }, frame.body);

        std::string symbol = std::string(symbol_registry_->by_id(symbol_id));
        if (symbol.empty()) {
            symbol = "UNKNOWN";
        }

        return msg_type + "." + symbol;
    }

    Config config_;
    boost::asio::io_context io_context_;

    std::shared_ptr<moodycamel::ConcurrentQueue<RawEvent>> feed_to_normalizer_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> normalizer_to_publisher_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> normalizer_to_recorder_;

    std::shared_ptr<SymbolRegistry> symbol_registry_;
    std::shared_ptr<MockFeed> mock_feed_;
    std::shared_ptr<Normalizer> normalizer_;
    std::shared_ptr<PubServer> pub_server_;
    std::shared_ptr<Recorder> recorder_;
    std::shared_ptr<Replayer> replayer_;
    std::shared_ptr<ControlServer> control_server_;

    std::unique_ptr<std::jthread> distribution_thread_;

    std::atomic<bool> running_{false};
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace md

std::unique_ptr<md::MarketDataCore> g_core;
std::atomic<bool> g_shutdown_in_progress{false};
std::atomic<int> g_signal_count{0};

void signal_handler(int signal) {
    if (g_shutdown_in_progress.load()) {
        std::cerr << "\nShutdown already in progress. Press Ctrl+C again to force exit.\n";

        if (g_signal_count.fetch_add(1) >= 1) {
            std::cerr << "Forcing immediate exit...\n";
            std::_Exit(1);
        }
        return;
    }

    g_shutdown_in_progress.store(true);
    std::cerr << "\nReceived shutdown signal (" << signal << "), stopping gracefully...\n";

    if (g_core) {
        g_core->request_shutdown();
    }
}

int main(int argc, char* argv[]) {
    int exit_code = 0;

    try {
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");

        spdlog::info("========================================");
        spdlog::info("MarketPulse Core System Starting");
        spdlog::info("========================================");

        std::string config_path = "config.json";
        if (argc > 1) {
            config_path = argv[1];
        }

        spdlog::info("Loading configuration from: {}", config_path);
        md::Config config;

        try {
            config = md::Config::load_from_file(config_path);
            spdlog::info("Configuration loaded successfully");
        } catch (const std::exception& e) {
            spdlog::error("Failed to load configuration: {}", e.what());
            throw std::runtime_error("Configuration error: " + std::string(e.what()));
        }

        spdlog::info("Installing signal handlers...");
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
#ifdef SIGQUIT
        std::signal(SIGQUIT, signal_handler);
#endif

        spdlog::info("Initializing core system...");
        auto init_start = std::chrono::steady_clock::now();

        g_core = std::make_unique<md::MarketDataCore>(config);

        auto init_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - init_start).count();
        spdlog::info("Core system initialized in {}ms", init_elapsed);

        spdlog::info("Starting all components...");
        auto start_time = std::chrono::steady_clock::now();

        try {
            g_core->start();

            auto start_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            spdlog::info("All components started in {}ms", start_elapsed);

        } catch (const std::exception& e) {
            spdlog::error("Failed to start system: {}", e.what());
            g_core.reset();
            return 2;
        }

        spdlog::info("========================================");
        spdlog::info("=== System Running ===");
        spdlog::info("Publisher port: {}", config.network.pubsub_port);
        spdlog::info("Control HTTP port: {}", config.network.ctrl_http_port);
        spdlog::info("WebSocket metrics port: {}", config.network.ws_metrics_port);
        spdlog::info("Data directory: {}", config.storage.dir);
        spdlog::info("Symbols: {}", config.feeds.default_symbols.size());
        spdlog::info("========================================");
        spdlog::info("Press Ctrl+C to stop gracefully");
        spdlog::info("Press Ctrl+C twice to force exit");
        spdlog::info("========================================");

        spdlog::info("Entering main event loop...");
        auto run_start = std::chrono::steady_clock::now();

        try {
            g_core->run();
        } catch (const std::exception& e) {
            spdlog::error("Runtime error in main loop: {}", e.what());
            exit_code = 3;
        }

        auto run_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - run_start).count();
        spdlog::info("Main loop exited after {} seconds", run_elapsed);

        spdlog::info("========================================");
        spdlog::info("Initiating graceful shutdown...");
        spdlog::info("========================================");

        auto shutdown_start = std::chrono::steady_clock::now();

        try {
            g_core->stop();

            auto shutdown_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - shutdown_start).count();
            spdlog::info("Graceful shutdown completed in {}ms", shutdown_elapsed);

        } catch (const std::exception& e) {
            spdlog::error("Error during shutdown: {}", e.what());
            exit_code = 4;
        }

        g_core.reset();

        spdlog::info("========================================");
        spdlog::info("MarketPulse Core System Stopped");
        spdlog::info("Exit code: {}", exit_code);
        spdlog::info("========================================");

        return exit_code;

    } catch (const std::exception& e) {
        spdlog::critical("FATAL ERROR: {}", e.what());
        spdlog::critical("System cannot continue, exiting...");

        if (g_core) {
            try {
                g_core->stop();
            } catch (...) {
                spdlog::error("Emergency cleanup failed");
            }
            g_core.reset();
        }

        return 1;

    } catch (...) {
        spdlog::critical("FATAL ERROR: Unknown exception");

        if (g_core) {
            try {
                spdlog::warn("Attempting emergency cleanup...");
                g_core->stop();
            } catch (const std::exception& e) {
                spdlog::error("Exception during emergency cleanup: {}", e.what());
            } catch (...) {
                spdlog::error("Unknown exception during emergency cleanup");
            }
            g_core.reset();
        }

        return 255;
    }
}
