#include "control_server.hpp"
#include "../feed/mock_feed.hpp"
#include "../normalize/normalizer.hpp"
#include "../publisher/pub_server.hpp"
#include "../recorder/recorder.hpp"
#include "../replay/replayer.hpp"
#include "../common/symbol_registry.hpp"
#include "../common/metrics.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <deque>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace md {

struct ControlServer::MetricsWsClient : public std::enable_shared_from_this<ControlServer::MetricsWsClient> {
    explicit MetricsWsClient(std::shared_ptr<websocket::stream<beast::tcp_stream>> ws_stream)
        : ws(std::move(ws_stream)) {
    }

    ~MetricsWsClient() {
        stop();
    }

    void start() {
        writer_thread = std::make_unique<std::jthread>([self = shared_from_this()](std::stop_token token) {
            self->writer_loop(token);
        });
    }

    bool enqueue(std::string message) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!running.load()) {
            return false;
        }

        if (pending_messages.size() >= max_pending_messages) {
            pending_messages.pop_front();
            MetricsCollector::instance().increment_counter("control_ws_metrics_dropped_messages_total");
        }

        pending_messages.push_back(std::move(message));
        queue_cv.notify_one();
        return true;
    }

    void stop() {
        const bool was_running = running.exchange(false);
        if (!was_running) {
            return;
        }

        queue_cv.notify_all();
        if (writer_thread && writer_thread->joinable()) {
            writer_thread->request_stop();
            writer_thread->join();
        }

        if (ws && ws->is_open()) {
            beast::error_code ec;
            ws->close(websocket::close_code::normal, ec);
        }
    }

    bool is_running() const {
        return running.load();
    }

private:
    void writer_loop(std::stop_token token) {
        while (running.load() && !token.stop_requested()) {
            std::string next;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this, &token] {
                    return !running.load() || token.stop_requested() || !pending_messages.empty();
                });

                if (!running.load() || token.stop_requested()) {
                    break;
                }

                if (pending_messages.empty()) {
                    continue;
                }

                next = std::move(pending_messages.front());
                pending_messages.pop_front();
            }

            beast::error_code ec;
            ws->text(true);
            ws->write(net::buffer(next), ec);
            if (ec) {
                MetricsCollector::instance().increment_counter("control_ws_metrics_write_errors_total");
                running.store(false);
                break;
            }
        }

        if (ws && ws->is_open()) {
            beast::error_code ec;
            ws->close(websocket::close_code::normal, ec);
        }
        running.store(false);
    }

    static constexpr size_t max_pending_messages = 64;

    std::shared_ptr<websocket::stream<beast::tcp_stream>> ws;
    std::atomic<bool> running{true};
    std::deque<std::string> pending_messages;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::unique_ptr<std::jthread> writer_thread;
};

namespace {

http::response<http::string_body> make_json_response(
    http::status status,
    const nlohmann::json& body,
    unsigned version,
    bool keep_alive) {
    http::response<http::string_body> res{status, version};
    res.keep_alive(keep_alive);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

nlohmann::json build_ws_metrics_payload() {
    auto& metrics = MetricsCollector::instance();

    const auto normalizer_latency = metrics.get_latency_percentiles("normalizer_event_latency_ns");
    const auto publisher_latency = metrics.get_latency_percentiles("publisher_publish_latency_ns");
    const auto recorder_latency = metrics.get_latency_percentiles("recorder_write_frame_ns");

    nlohmann::json payload;
    payload["timestamp_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    payload["throughput"] = {
        {"feed_events_ingested_total", metrics.get_counter("feed_events_ingested_total")},
        {"normalizer_events_total", metrics.get_counter("normalizer_events_total")},
        {"publisher_frames_published_total", metrics.get_counter("publisher_frames_published_total")},
        {"recorder_frames_total", metrics.get_counter("recorder_frames_total")},
        {"frame_distribution_total", metrics.get_counter("frame_distribution_total")}
    };

    payload["latency"] = {
        {"normalizer_event_latency_ns", {{"p50", normalizer_latency.p50}, {"p99", normalizer_latency.p99}}},
        {"publisher_publish_latency_ns", {{"p50", publisher_latency.p50}, {"p99", publisher_latency.p99}}},
        {"recorder_write_frame_ns", {{"p50", recorder_latency.p50}, {"p99", recorder_latency.p99}}}
    };

    payload["queue_depths"] = {
        {"pipeline_feed_queue_approx", metrics.get_gauge("pipeline_feed_queue_approx")},
        {"pipeline_normalizer_to_publisher_queue_approx", metrics.get_gauge("pipeline_normalizer_to_publisher_queue_approx")},
        {"pipeline_normalizer_to_recorder_queue_approx", metrics.get_gauge("pipeline_normalizer_to_recorder_queue_approx")}
    };

    return payload;
}

} // namespace

ControlServer::ControlServer(boost::asio::io_context& io_context,
                             uint16_t http_port,
                             uint16_t ws_port, 
                             const std::string& auth_token)
    : io_context_(io_context), http_port_(http_port), ws_port_(ws_port), auth_token_(auth_token) {
}

ControlServer::~ControlServer() {
    stop();
}

void ControlServer::start() {
    if (running_.exchange(true)) {
        return;
    }

    try {
        http_acceptor_ = std::make_unique<tcp::acceptor>(io_context_);

        beast::error_code ec;
        const auto endpoint = tcp::endpoint(tcp::v4(), http_port_);

        http_acceptor_->open(endpoint.protocol(), ec);
        if (ec) {
            throw std::runtime_error("ControlServer open failed: " + ec.message());
        }

        http_acceptor_->set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            throw std::runtime_error("ControlServer set_option failed: " + ec.message());
        }

        http_acceptor_->bind(endpoint, ec);
        if (ec) {
            throw std::runtime_error("ControlServer bind failed: " + ec.message());
        }

        http_acceptor_->listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            throw std::runtime_error("ControlServer listen failed: " + ec.message());
        }

        http_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
            http_server_loop(token);
        });

        start_metrics_websocket();

        spdlog::info("ControlServer listening on HTTP port {}", http_port_);
    } catch (...) {
        stop();
        throw;
    }
}

void ControlServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (http_acceptor_) {
        beast::error_code ec;
        http_acceptor_->close(ec);
        http_acceptor_.reset();
    }

    if (http_thread_ && http_thread_->joinable()) {
        http_thread_->request_stop();
        http_thread_->join();
        http_thread_.reset();
    }

    if (ws_acceptor_) {
        beast::error_code ec;
        ws_acceptor_->close(ec);
        ws_acceptor_.reset();
    }

    if (ws_accept_thread_ && ws_accept_thread_->joinable()) {
        ws_accept_thread_->request_stop();
        ws_accept_thread_->join();
        ws_accept_thread_.reset();
    }

    if (metrics_thread_ && metrics_thread_->joinable()) {
        metrics_thread_->request_stop();
        metrics_thread_->join();
        metrics_thread_.reset();
    }

    // Close all WebSocket connections
    {
        std::lock_guard<std::mutex> lock(ws_connections_mutex_);
        for (auto& client : ws_connections_) {
            if (client) {
                client->stop();
            }
        }
        ws_connections_.clear();
    }
    
    spdlog::info("ControlServer stopped");
}

void ControlServer::handle_http_request(
    boost::beast::http::request<boost::beast::http::string_body>&& req,
    std::function<void(boost::beast::http::response<boost::beast::http::string_body>)> send) {

    const std::string target(req.target());
    const std::string method_text(req.method_string());
    const auto method = req.method();
    spdlog::info("ControlServer request: method={} target={} body_bytes={}",
                 method_text, target, req.body().size());

    if (method == http::verb::options) {
        http::response<http::string_body> res{http::status::no_content, req.version()};
        res.keep_alive(req.keep_alive());
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        return send(std::move(res));
    }

    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.keep_alive(req.keep_alive());

    try {
        if (target == "/metrics" && method == http::verb::get) {
            res = handle_metrics();
            res.version(req.version());
            res.keep_alive(req.keep_alive());
            res.set(http::field::access_control_allow_origin, "*");
            res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
            res.prepare_payload();
        } else if (target == "/health" && method == http::verb::get) {
            res = handle_health();
            res.version(req.version());
            res.keep_alive(req.keep_alive());
            res.set(http::field::access_control_allow_origin, "*");
            res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
            res.prepare_payload();
        } else if (target == "/feeds/start" && method == http::verb::post) {
            try {
                auto body = nlohmann::json::parse(req.body());
                res = handle_feeds_start(body);
            } catch (const nlohmann::json::parse_error& e) {
                spdlog::warn("ControlServer invalid JSON on /feeds/start: {}", e.what());
                res = make_json_response(
                    http::status::bad_request,
                    nlohmann::json{{"error", "Invalid JSON"}},
                    req.version(),
                    req.keep_alive());
            }
        } else if (target == "/feeds/stop" && method == http::verb::post) {
            res = handle_feeds_stop();
        } else {
            res = make_json_response(
                http::status::not_found,
                nlohmann::json{{"error", "Not Found"}},
                req.version(),
                req.keep_alive());
        }

    } catch (const std::exception& e) {
        spdlog::error("ControlServer internal error on {}: {}", target, e.what());
        res = make_json_response(
            http::status::internal_server_error,
            nlohmann::json{{"error", "Internal Server Error"}},
            req.version(),
            req.keep_alive());
    }

    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.prepare_payload();

    spdlog::info("ControlServer response: method={} target={} status={}",
                 method_text, target, static_cast<unsigned>(res.result()));

    send(std::move(res));
}

void ControlServer::http_server_loop(std::stop_token token) {
    while (running_.load() && !token.stop_requested()) {
        try {
            tcp::socket socket(io_context_);
            beast::error_code ec;
            http_acceptor_->accept(socket, ec);

            if (ec) {
                if (!running_.load() || ec == net::error::operation_aborted || ec == beast::errc::bad_file_descriptor) {
                    break;
                }
                spdlog::warn("ControlServer accept failed: {}", ec.message());
                continue;
            }

            std::thread([this, s = std::move(socket)]() mutable {
                handle_http_session(std::move(s));
            }).detach();
        } catch (const std::exception& e) {
            if (running_.load()) {
                spdlog::error("ControlServer listener error: {}", e.what());
            }
        }
    }
}

void ControlServer::handle_http_session(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(64 * 1024);
    beast::error_code ec;

    http::read(socket, buffer, parser, ec);
    if (ec) {
        if (ec != http::error::end_of_stream) {
            spdlog::debug("ControlServer read error: {}", ec.message());
        }
        return;
    }

    auto req = parser.release();

    auto send = [&](http::response<http::string_body> res) {
        http::write(socket, res, ec);
    };

    handle_http_request(std::move(req), send);

    socket.shutdown(tcp::socket::shutdown_send, ec);
}

http::response<http::string_body> ControlServer::handle_health() {
    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = nlohmann::json{{"status", "ok"}}.dump();
    return res;
}

http::response<http::string_body> ControlServer::handle_feeds_start(const nlohmann::json& body) {
    std::lock_guard<std::mutex> lock(feed_control_mutex_);

    if (!mock_feed_) {
        return make_json_response(
            http::status::service_unavailable,
            nlohmann::json{{"error", "Feed not available"}},
            11,
            false);
    }

    if (!body.contains("l1_rate") || !body["l1_rate"].is_number() ||
        !body.contains("l2_rate") || !body["l2_rate"].is_number() ||
        !body.contains("trade_rate") || !body["trade_rate"].is_number()) {
        return make_json_response(
            http::status::bad_request,
            nlohmann::json{{"error", "Invalid JSON: expected numeric l1_rate, l2_rate, trade_rate"}},
            11,
            false);
    }

    const double l1_rate_d = body["l1_rate"].get<double>();
    const double l2_rate_d = body["l2_rate"].get<double>();
    const double trade_rate_d = body["trade_rate"].get<double>();

    if (l1_rate_d < 0.0 || l2_rate_d < 0.0 || trade_rate_d < 0.0) {
        return make_json_response(
            http::status::bad_request,
            nlohmann::json{{"error", "Rates must be non-negative"}},
            11,
            false);
    }

    const auto l1_rate = static_cast<uint32_t>(l1_rate_d);
    const auto l2_rate = static_cast<uint32_t>(l2_rate_d);
    const auto trade_rate = static_cast<uint32_t>(trade_rate_d);

    spdlog::info("Control action: feed start requested (l1_rate={}, l2_rate={}, trade_rate={})",
                 l1_rate, l2_rate, trade_rate);
    mock_feed_->set_rates(l1_rate, l2_rate, trade_rate);
    mock_feed_->start();

    return make_json_response(
        http::status::ok,
        nlohmann::json{{"status", "started"}},
        11,
        false);
}

http::response<http::string_body> ControlServer::handle_feeds_stop() {
    std::lock_guard<std::mutex> lock(feed_control_mutex_);

    if (!mock_feed_) {
        return make_json_response(
            http::status::service_unavailable,
            nlohmann::json{{"error", "Feed not available"}},
            11,
            false);
    }

    spdlog::info("Control action: feed stop requested");
    mock_feed_->stop();

    return make_json_response(
        http::status::ok,
        nlohmann::json{{"status", "stopped"}},
        11,
        false);
}

http::response<http::string_body> ControlServer::handle_symbols_get() {
    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    
    nlohmann::json response;
    
    if (symbol_registry_) {
        auto symbols = symbol_registry_->get_all();
        nlohmann::json symbols_json = nlohmann::json::array();
        
        for (const auto& [id, symbol] : symbols) {
            symbols_json.push_back({
                {"id", id},
                {"symbol", symbol}
            });
        }
        
        response["symbols"] = symbols_json;
        response["count"] = symbols.size();
    } else {
        response["symbols"] = nlohmann::json::array();
        response["count"] = 0;
    }
    
    res.body() = response.dump(2);
    return res;
}

http::response<http::string_body> ControlServer::handle_feeds_get() {
    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    
    nlohmann::json response;
    response["feeds"] = nlohmann::json::array();
    
    if (mock_feed_) {
        const auto& stats = mock_feed_->get_stats();
        response["feeds"].push_back({
            {"name", "mock"},
            {"active", true},
            {"stats", {
                {"l1_count", stats.l1_count.load()},
                {"l2_count", stats.l2_count.load()},
                {"trade_count", stats.trade_count.load()},
                {"total_events", stats.total_events.load()}
            }}
        });
    }
    
    res.body() = response.dump(2);
    return res;
}

http::response<http::string_body> ControlServer::handle_feeds_post(const std::string& body) {
    http::response<http::string_body> res;
    
    try {
        auto json = nlohmann::json::parse(body);
        std::string action = json.value("action", "");
        
        if (action == "start" && mock_feed_) {
            uint32_t l1_rate = json.value("l1_rate", 50000);
            uint32_t l2_rate = json.value("l2_rate", 30000);
            uint32_t trade_rate = json.value("trade_rate", 5000);
            
            mock_feed_->set_rates(l1_rate, l2_rate, trade_rate);
            mock_feed_->start();
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"started"})";
            
        } else if (action == "stop" && mock_feed_) {
            mock_feed_->stop();
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"stopped"})";
            
        } else {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"Invalid action or feed not available"})";
        }
        
    } catch (const std::exception& e) {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "application/json");
        res.body() = nlohmann::json{{"error", e.what()}}.dump();
    }
    
    return res;
}

http::response<http::string_body> ControlServer::handle_replay_post(const std::string& body) {
    http::response<http::string_body> res;
    
    try {
        auto json = nlohmann::json::parse(body);
        std::string action = json.value("action", "");
        
        if (!replayer_) {
            res.result(http::status::service_unavailable);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"Replayer not available"})";
            return res;
        }
        
        if (action == "start") {
            uint64_t from_ts_ns = json.value("from_ts_ns", 0ULL);
            uint64_t to_ts_ns = json.value("to_ts_ns", 0ULL);
            double rate = json.value("rate", 1.0);
            auto topics = json.value("topics", std::vector<std::string>{"*"});
            
            std::string session_id = replayer_->start_session(from_ts_ns, to_ts_ns, topics, rate);
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = nlohmann::json{{"session_id", session_id}}.dump();
            
        } else if (action == "stop") {
            std::string session_id = json.value("session_id", "");
            replayer_->stop_session(session_id);
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"stopped"})";
            
        } else if (action == "pause") {
            std::string session_id = json.value("session_id", "");
            replayer_->pause_session(session_id);
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"paused"})";
            
        } else if (action == "resume") {
            std::string session_id = json.value("session_id", "");
            replayer_->resume_session(session_id);
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"resumed"})";
            
        } else if (action == "seek") {
            std::string session_id = json.value("session_id", "");
            uint64_t ts_ns = json.value("timestamp_ns", 0ULL);
            replayer_->seek_session(session_id, ts_ns);
            
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"seeked"})";
            
        } else {
            res.result(http::status::bad_request);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"Invalid action"})";
        }
        
    } catch (const std::exception& e) {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "application/json");
        res.body() = nlohmann::json{{"error", e.what()}}.dump();
    }
    
    return res;
}

http::response<http::string_body> ControlServer::handle_metrics() {
    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/plain");
    res.body() = MetricsCollector::instance().get_prometheus_metrics();
    return res;
}

http::response<http::string_body> ControlServer::handle_latest_event(const std::string& topic) {
    http::response<http::string_body> res;
    
    if (!pub_server_) {
        res.result(http::status::service_unavailable);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"Publisher not available"})";
        return res;
    }
    
    auto frame_opt = pub_server_->get_latest_frame(topic);
    
    if (!frame_opt) {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "application/json");
        res.body() = nlohmann::json{{"error", "No data for topic: " + topic}}.dump();
        return res;
    }
    
    const Frame& frame = *frame_opt;
    nlohmann::json result;
    result["topic"] = topic;
    result["msg_type"] = frame.header.msg_type;
    
    // Extract frame data based on type
    std::visit([&result, this](const auto& body) {
        using T = std::decay_t<decltype(body)>;
        
        if constexpr (std::is_same_v<T, L1Body>) {
            result["type"] = "L1";
            result["timestamp_ns"] = body.ts_ns;
            result["symbol_id"] = body.symbol_id;
            if (symbol_registry_) {
                result["symbol"] = std::string(symbol_registry_->by_id(body.symbol_id));
            }
            result["bid_price"] = body.bid_px / 1e8;
            result["bid_size"] = body.bid_sz / 1e8;
            result["ask_price"] = body.ask_px / 1e8;
            result["ask_size"] = body.ask_sz / 1e8;
            result["sequence"] = body.seq;
            
        } else if constexpr (std::is_same_v<T, L2Body>) {
            result["type"] = "L2";
            result["timestamp_ns"] = body.ts_ns;
            result["symbol_id"] = body.symbol_id;
            if (symbol_registry_) {
                result["symbol"] = std::string(symbol_registry_->by_id(body.symbol_id));
            }
            result["side"] = body.side == 0 ? "bid" : "ask";
            result["action"] = body.action == 0 ? "insert" : (body.action == 1 ? "update" : "delete");
            result["level"] = body.level;
            result["price"] = body.price / 1e8;
            result["size"] = body.size / 1e8;
            result["sequence"] = body.seq;
            
        } else if constexpr (std::is_same_v<T, TradeBody>) {
            result["type"] = "Trade";
            result["timestamp_ns"] = body.ts_ns;
            result["symbol_id"] = body.symbol_id;
            if (symbol_registry_) {
                result["symbol"] = std::string(symbol_registry_->by_id(body.symbol_id));
            }
            result["price"] = body.price / 1e8;
            result["size"] = body.size / 1e8;
            result["aggressor_side"] = body.aggressor_side == 0 ? "buy" : (body.aggressor_side == 1 ? "sell" : "unknown");
            result["sequence"] = body.seq;
            
        } else if constexpr (std::is_same_v<T, HbBody>) {
            result["type"] = "Heartbeat";
            result["timestamp_ns"] = body.ts_ns;
            
        } else {
            result["type"] = "Other";
        }
    }, frame.body);
    
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = result.dump(2);
    return res;
}

void ControlServer::start_metrics_websocket() {
    ws_acceptor_ = std::make_unique<tcp::acceptor>(io_context_);

    beast::error_code ec;
    const auto endpoint = tcp::endpoint(tcp::v4(), ws_port_);

    ws_acceptor_->open(endpoint.protocol(), ec);
    if (ec) {
        throw std::runtime_error("ControlServer WS open failed: " + ec.message());
    }

    ws_acceptor_->set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("ControlServer WS set_option failed: " + ec.message());
    }

    ws_acceptor_->bind(endpoint, ec);
    if (ec) {
        throw std::runtime_error("ControlServer WS bind failed: " + ec.message());
    }

    ws_acceptor_->listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        throw std::runtime_error("ControlServer WS listen failed: " + ec.message());
    }

    ws_accept_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        websocket_accept_loop(token);
    });

    metrics_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        while (running_.load() && !token.stop_requested()) {
            metrics_broadcast_loop();
        }
    });

    spdlog::info("ControlServer WebSocket metrics endpoint available at ws://localhost:{}/ws/metrics", ws_port_);
}

void ControlServer::websocket_accept_loop(std::stop_token token) {
    while (running_.load() && !token.stop_requested()) {
        try {
            tcp::socket socket(io_context_);
            beast::error_code ec;
            ws_acceptor_->accept(socket, ec);

            if (ec) {
                if (!running_.load() || ec == net::error::operation_aborted || ec == beast::errc::bad_file_descriptor) {
                    break;
                }
                spdlog::warn("ControlServer WS accept failed: {}", ec.message());
                continue;
            }

            std::thread([this, s = std::move(socket)]() mutable {
                handle_websocket_session(std::move(s));
            }).detach();
        } catch (const std::exception& e) {
            if (running_.load()) {
                spdlog::error("ControlServer WS listener error: {}", e.what());
            }
        }
    }
}

void ControlServer::handle_websocket_session(tcp::socket socket) {
    beast::error_code ec;
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    http::read(socket, buffer, req, ec);
    if (ec) {
        spdlog::debug("ControlServer WS pre-read failed: {}", ec.message());
        return;
    }

    if (req.target() != "/ws/metrics") {
        auto res = make_json_response(
            http::status::not_found,
            nlohmann::json{{"error", "WebSocket endpoint not found"}},
            req.version(),
            false);
        http::write(socket, res, ec);
        socket.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    if (!websocket::is_upgrade(req)) {
        auto res = make_json_response(
            http::status::bad_request,
            nlohmann::json{{"error", "Expected WebSocket upgrade"}},
            req.version(),
            false);
        http::write(socket, res, ec);
        socket.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    auto ws = std::make_shared<websocket::stream<beast::tcp_stream>>(std::move(socket));
    ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws->set_option(websocket::stream_base::decorator(
        [](websocket::response_type& response) {
            response.set(http::field::server, "MarketPulse-ControlServer");
        }));

    ws->accept(req, ec);
    if (ec) {
        spdlog::warn("ControlServer WS handshake failed: {}", ec.message());
        return;
    }

    auto client = std::make_shared<MetricsWsClient>(ws);
    client->start();

    {
        std::lock_guard<std::mutex> lock(ws_connections_mutex_);
        ws_connections_.push_back(client);
    }

    MetricsCollector::instance().increment_counter("control_ws_metrics_connections_total");
    spdlog::info("ControlServer WS client connected");
}

void ControlServer::metrics_broadcast_loop() {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!running_.load()) {
        return;
    }

    try {
        const std::string metrics_json = build_ws_metrics_payload().dump();

        std::vector<std::shared_ptr<MetricsWsClient>> clients_snapshot;
        {
            std::lock_guard<std::mutex> lock(ws_connections_mutex_);
            ws_connections_.erase(
                std::remove_if(ws_connections_.begin(), ws_connections_.end(),
                    [](const auto& client) {
                        return !client || !client->is_running();
                    }),
                ws_connections_.end());
            clients_snapshot = ws_connections_;
        }

        for (auto& client : clients_snapshot) {
            if (!client || !client->enqueue(metrics_json)) {
                if (client) {
                    client->stop();
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(ws_connections_mutex_);
            ws_connections_.erase(
                std::remove_if(ws_connections_.begin(), ws_connections_.end(),
                    [](const auto& client) {
                        return !client || !client->is_running();
                    }),
                ws_connections_.end());
        }
    } catch (const std::exception& e) {
        spdlog::error("Metrics broadcast error: {}", e.what());
    }
}

} // namespace md