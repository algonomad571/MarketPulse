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

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace md {

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

        spdlog::info("ControlServer listening on HTTP port {} (/metrics only)", http_port_);
    } catch (...) {
        running_.store(false);
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

    // Close all WebSocket connections
    {
        std::lock_guard<std::mutex> lock(ws_connections_mutex_);
        for (auto& ws : ws_connections_) {
            if (ws) {
                beast::error_code ec;
                ws->close(websocket::close_code::going_away, ec);
            }
        }
        ws_connections_.clear();
    }
    
    spdlog::info("ControlServer stopped");
}

void ControlServer::handle_http_request(
    boost::beast::http::request<boost::beast::http::string_body>&& req,
    std::function<void(boost::beast::http::response<boost::beast::http::string_body>)> send) {

    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(false);

    try {
        std::string target(req.target());
        auto method = req.method();

        if (target == "/metrics" && method == http::verb::get) {
            res = handle_metrics();
        } else {
            // 404 Not Found
            res.result(http::status::not_found);
            res.set(http::field::content_type, "text/plain");
            res.body() = "Not Found\n";
        }

    } catch (const std::exception& e) {
        // 500 Internal Server Error
        res.result(http::status::internal_server_error);
        res.set(http::field::content_type, "text/plain");
        res.body() = "Internal Server Error\n";
        spdlog::error("HTTP request error: {}", e.what());
    }

    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.prepare_payload();

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

            handle_http_session(std::move(socket));
        } catch (const std::exception& e) {
            if (running_.load()) {
                spdlog::error("ControlServer listener error: {}", e.what());
            }
        }
    }
}

void ControlServer::handle_http_session(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    beast::error_code ec;

    http::read(socket, buffer, req, ec);
    if (ec) {
        if (ec != http::error::end_of_stream) {
            spdlog::debug("ControlServer read error: {}", ec.message());
        }
        return;
    }

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
    
    nlohmann::json health;
    health["status"] = "ok";
    health["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Component status
    if (mock_feed_) {
        const auto& stats = mock_feed_->get_stats();
        health["components"]["mock_feed"] = {
            {"l1_count", stats.l1_count.load()},
            {"l2_count", stats.l2_count.load()},
            {"trade_count", stats.trade_count.load()},
            {"total_events", stats.total_events.load()}
        };
    }
    
    if (normalizer_) {
        const auto& stats = normalizer_->get_stats();
        health["components"]["normalizer"] = {
            {"events_processed", stats.events_processed.load()},
            {"frames_output", stats.frames_output.load()},
            {"errors", stats.errors.load()}
        };
    }
    
    if (pub_server_) {
        const auto& stats = pub_server_->get_stats();
        health["components"]["publisher"] = {
            {"total_connections", stats.total_connections.load()},
            {"active_connections", stats.active_connections.load()},
            {"frames_published", stats.frames_published.load()},
            {"frames_dropped", stats.frames_dropped.load()}
        };
    }
    
    if (recorder_) {
        const auto& stats = recorder_->get_stats();
        health["components"]["recorder"] = {
            {"frames_written", stats.frames_written.load()},
            {"bytes_written", stats.bytes_written.load()},
            {"is_recording", stats.is_recording.load()}
        };
    }
    
    res.body() = health.dump(2);
    return res;
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
    // TODO: Implement WebSocket metrics server
    // This would involve setting up a WebSocket acceptor and handling connections
    spdlog::info("WebSocket metrics server would start on port {}", ws_port_);
}

void ControlServer::metrics_broadcast_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        
        if (!running_.load()) break;
        
        try {
            std::string metrics_json = MetricsCollector::instance().get_json_metrics();
            
            // Broadcast to all connected WebSocket clients
            std::lock_guard<std::mutex> lock(ws_connections_mutex_);
            
            auto it = ws_connections_.begin();
            while (it != ws_connections_.end()) {
                auto& ws = *it;
                if (!ws || !ws->is_open()) {
                    it = ws_connections_.erase(it);
                    continue;
                }
                
                try {
                    ws->text(true);
                    beast::error_code ec;
                    ws->write(net::buffer(metrics_json), ec);
                    if (ec) {
                        spdlog::warn("WebSocket write error: {}", ec.message());
                        it = ws_connections_.erase(it);
                        continue;
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("WebSocket broadcast error: {}", e.what());
                    it = ws_connections_.erase(it);
                    continue;
                }
                
                ++it;
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Metrics broadcast error: {}", e.what());
        }
    }
}

} // namespace md