#include "common/backpressure.hpp"
#include "common/frame.hpp"
#include "common/metrics.hpp"
#include "common/symbol_registry.hpp"
#include "feed/mock_feed.hpp"
#include "normalize/normalizer.hpp"
#include "publisher/pub_server.hpp"
#include "recorder/recorder.hpp"

#include <boost/asio.hpp>
#include <concurrentqueue.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace md {
namespace {

struct BenchmarkOptions {
    uint32_t producers = 1;
    uint32_t duration_seconds = 10;
    uint32_t normalizer_threads = 4;
    uint32_t queue_high_watermark = 80000;
    uint32_t queue_low_watermark = 60000;
};

struct BenchmarkResult {
    uint64_t messages_processed = 0;
    double throughput_msgs_per_sec = 0.0;
    double avg_latency_us = 0.0;
    double p50_latency_us = 0.0;
    double p99_latency_us = 0.0;
    uint64_t max_queue_depth = 0;
    uint64_t dropped_frames = 0;
    double runtime_seconds = 0.0;
};

bool parse_uint_arg(const std::string& arg, const std::string& name, uint32_t& out) {
    const std::string prefix = "--" + name + "=";
    if (!arg.starts_with(prefix)) {
        return false;
    }

    const std::string value = arg.substr(prefix.size());
    if (value.empty()) {
        return false;
    }

    try {
        const auto parsed = static_cast<uint32_t>(std::stoul(value));
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<BenchmarkOptions> parse_options(int argc, char* argv[]) {
    BenchmarkOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);

        if (parse_uint_arg(arg, "producers", options.producers)) {
            continue;
        }
        if (parse_uint_arg(arg, "duration", options.duration_seconds)) {
            continue;
        }
        if (parse_uint_arg(arg, "normalizer_threads", options.normalizer_threads)) {
            continue;
        }
        if (parse_uint_arg(arg, "queue_high_watermark", options.queue_high_watermark)) {
            continue;
        }
        if (parse_uint_arg(arg, "queue_low_watermark", options.queue_low_watermark)) {
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        return std::nullopt;
    }

    if (!(options.producers == 1 || options.producers == 4 || options.producers == 8)) {
        std::cerr << "Invalid --producers value. Supported: 1, 4, 8\n";
        return std::nullopt;
    }

    if (options.duration_seconds == 0) {
        std::cerr << "--duration must be > 0\n";
        return std::nullopt;
    }

    if (options.queue_high_watermark == 0) {
        std::cerr << "--queue_high_watermark must be > 0\n";
        return std::nullopt;
    }

    if (options.queue_low_watermark >= options.queue_high_watermark) {
        std::cerr << "--queue_low_watermark must be < --queue_high_watermark\n";
        return std::nullopt;
    }

    return options;
}

void print_usage() {
    std::cerr << "Usage:\n";
    std::cerr << "  throughput_benchmark --producers=1|4|8 [--duration=10] [--normalizer_threads=4]\\\n"
              << " [--queue_high_watermark=80000] [--queue_low_watermark=60000]\n";
}

uint64_t frame_timestamp_ns(const Frame& frame) {
    return std::visit([](const auto& body) -> uint64_t {
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, L1Body> || std::is_same_v<T, L2Body> || std::is_same_v<T, TradeBody>) {
            return body.ts_ns;
        } else if constexpr (std::is_same_v<T, HbBody>) {
            return body.ts_ns;
        } else {
            return 0;
        }
    }, frame.body);
}

std::string frame_topic(const Frame& frame, const SymbolRegistry& registry) {
    uint32_t symbol_id = 0;
    std::string msg_type;

    std::visit([&](const auto& body) {
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
        } else if constexpr (std::is_same_v<T, HbBody>) {
            symbol_id = 0;
            msg_type = "hb";
        } else {
            symbol_id = 0;
            msg_type = "control";
        }
    }, frame.body);

    const std::string symbol = (symbol_id == 0) ? "UNKNOWN" : std::string(registry.by_id(symbol_id));
    return msg_type + "." + symbol;
}

std::string make_run_dir() {
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path run_dir = std::filesystem::path("data") / "benchmark_runs" / ("run_" + std::to_string(now_ns));
    std::filesystem::create_directories(run_dir);
    return run_dir.string();
}

BenchmarkResult run_benchmark(const BenchmarkOptions& options) {
    BenchmarkResult result;

    auto feed_to_normalizer = std::make_shared<moodycamel::ConcurrentQueue<RawEvent>>(100000);
    auto normalizer_to_distributor = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);
    auto distributor_to_recorder = std::make_shared<moodycamel::ConcurrentQueue<Frame>>(100000);

    auto symbol_registry = std::make_shared<SymbolRegistry>();

    auto normalizer = std::make_shared<Normalizer>(
        feed_to_normalizer,
        normalizer_to_distributor,
        symbol_registry,
        options.normalizer_threads,
        options.queue_high_watermark,
        options.queue_low_watermark);

    boost::asio::io_context io_context(1);
    auto publisher = std::make_shared<PubServer>(
        io_context,
        0,
        "benchmark-token",
        options.queue_high_watermark,
        options.queue_low_watermark);

    auto recorder = std::make_shared<Recorder>(
        make_run_dir(),
        distributor_to_recorder,
        2ULL * 1024ULL * 1024ULL * 1024ULL,
        10000,
        50);
    recorder->set_symbol_registry(symbol_registry);

    const std::vector<std::string> symbols = {
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "ADAUSDT", "DOGEUSDT",
        "BNBUSDT", "XRPUSDT", "AVAXUSDT", "DOTUSDT", "LTCUSDT"
    };

    std::vector<std::shared_ptr<MockFeed>> feeds;
    feeds.reserve(options.producers);
    for (uint32_t i = 0; i < options.producers; ++i) {
        auto feed = std::make_shared<MockFeed>(
            symbols,
            feed_to_normalizer,
            options.queue_high_watermark,
            options.queue_low_watermark);
        feed->set_rates(5000, 3000, 1000);
        feeds.push_back(feed);
    }

    std::atomic<bool> benchmark_running{true};
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> total_latency_ns{0};
    std::atomic<uint64_t> max_queue_depth{0};

    LatencyHistogram latency_hist({1000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000, 5000000, 10000000});

    std::jthread io_thread([&]() {
        io_context.run();
    });

    std::jthread distributor_thread([&](std::stop_token token) {
        const size_t batch_size = 256;
        std::vector<Frame> batch;
        batch.reserve(batch_size);

        while (benchmark_running.load() && !token.stop_requested()) {
            batch.clear();
            Frame frame;
            while (batch.size() < batch_size && normalizer_to_distributor->try_dequeue(frame)) {
                batch.push_back(std::move(frame));
            }

            if (batch.empty()) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            for (const auto& current_frame : batch) {
                const uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const uint64_t ts_ns = frame_timestamp_ns(current_frame);
                if (ts_ns > 0 && now_ns >= ts_ns) {
                    const uint64_t latency_ns = now_ns - ts_ns;
                    total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
                    latency_hist.record(latency_ns);
                }

                publisher->publish(frame_topic(current_frame, *symbol_registry), current_frame);
                distributor_to_recorder->enqueue(current_frame);
                messages_processed.fetch_add(1, std::memory_order_relaxed);
            }

            const uint64_t current_depth = std::max({
                static_cast<uint64_t>(feed_to_normalizer->size_approx()),
                static_cast<uint64_t>(normalizer_to_distributor->size_approx()),
                static_cast<uint64_t>(distributor_to_recorder->size_approx())
            });
            uint64_t previous_max = max_queue_depth.load(std::memory_order_relaxed);
            while (current_depth > previous_max &&
                   !max_queue_depth.compare_exchange_weak(previous_max, current_depth, std::memory_order_relaxed)) {
            }
        }
    });

    const auto start = std::chrono::steady_clock::now();

    publisher->start();
    normalizer->start();
    recorder->start();
    for (auto& feed : feeds) {
        feed->start();
    }

    std::this_thread::sleep_for(std::chrono::seconds(options.duration_seconds));

    benchmark_running.store(false);

    for (auto& feed : feeds) {
        feed->stop();
    }

    distributor_thread.request_stop();
    if (distributor_thread.joinable()) {
        distributor_thread.join();
    }

    recorder->stop();
    publisher->stop();
    normalizer->stop();

    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }

    const auto end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

    const uint64_t processed = messages_processed.load(std::memory_order_relaxed);
    result.messages_processed = processed;
    result.throughput_msgs_per_sec = (result.runtime_seconds > 0.0)
        ? static_cast<double>(processed) / result.runtime_seconds
        : 0.0;

    result.avg_latency_us = (processed > 0)
        ? static_cast<double>(total_latency_ns.load(std::memory_order_relaxed)) / static_cast<double>(processed) / 1000.0
        : 0.0;

    const auto pct = latency_hist.get_percentiles();
    result.p50_latency_us = static_cast<double>(pct.p50) / 1000.0;
    result.p99_latency_us = static_cast<double>(pct.p99) / 1000.0;

    result.max_queue_depth = max_queue_depth.load(std::memory_order_relaxed);

    auto& metrics = MetricsCollector::instance();
    result.dropped_frames =
        metrics.get_counter("normalizer_enqueue_failures_total") +
        metrics.get_counter("publisher_frames_dropped_backpressure") +
        metrics.get_counter("publisher_frames_dropped_queue_full") +
        metrics.get_counter("recorder_degraded_drops_total");

    return result;
}

} // namespace
} // namespace md

int main(int argc, char* argv[]) {
    auto options = md::parse_options(argc, argv);
    if (!options.has_value()) {
        md::print_usage();
        return 1;
    }

    const md::BenchmarkResult result = md::run_benchmark(*options);

    std::cout << "================================\n";
    std::cout << "Messages Processed: " << result.messages_processed << "\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << result.throughput_msgs_per_sec << " msgs/sec\n";
    std::cout << "Average Latency: " << std::fixed << std::setprecision(2) << result.avg_latency_us << " us\n";
    std::cout << "P50 Latency: " << std::fixed << std::setprecision(2) << result.p50_latency_us << " us\n";
    std::cout << "P99 Latency: " << std::fixed << std::setprecision(2) << result.p99_latency_us << " us\n";
    std::cout << "Queue Depth: " << result.max_queue_depth << "\n";
    std::cout << "Dropped Frames: " << result.dropped_frames << "\n";
    std::cout << "Runtime: " << std::fixed << std::setprecision(2) << result.runtime_seconds << " s\n";
    std::cout << "================================\n";

    return 0;
}
