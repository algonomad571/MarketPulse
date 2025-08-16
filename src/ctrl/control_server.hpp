#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <thread>

namespace md {

// Forward declarations
class MockFeed;
class Normalizer;
class PubServer;
class Recorder;
class Replayer;
class SymbolRegistry;

class ControlServer {
public:
    ControlServer(boost::asio::io_context& io_context,
                  uint16_t http_port,
                  uint16_t ws_port,
                  const std::string& auth_token);
    
    ~ControlServer();
    
    void start();
    void stop();
    
    // Set component references
    void set_mock_feed(std::shared_ptr<MockFeed> mock_feed) { mock_feed_ = mock_feed; }
    void set_normalizer(std::shared_ptr<Normalizer> normalizer) { normalizer_ = normalizer; }
    void set_pub_server(std::shared_ptr<PubServer> pub_server) { pub_server_ = pub_server; }
    void set_recorder(std::shared_ptr<Recorder> recorder) { recorder_ = recorder; }
    void set_replayer(std::shared_ptr<Replayer> replayer) { replayer_ = replayer; }
    void set_symbol_registry(std::shared_ptr<SymbolRegistry> symbol_registry) { symbol_registry_ = symbol_registry; }

private:
    // HTTP handlers
    void handle_http_request(boost::beast::http::request<boost::beast::http::string_body>&& req,
                             std::function<void(boost::beast::http::response<boost::beast::http::string_body>)> send);
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_health();
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_symbols_get();
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_feeds_get();
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_feeds_post(const std::string& body);
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_replay_post(const std::string& body);
    
    boost::beast::http::response<boost::beast::http::string_body> 
    handle_metrics();
    
    // WebSocket metrics
    void start_metrics_websocket();
    void metrics_broadcast_loop();
    
    boost::asio::io_context& io_context_;
    uint16_t http_port_;
    uint16_t ws_port_;
    std::string auth_token_;
    
    std::atomic<bool> running_{false};
    
    // Component references
    std::shared_ptr<MockFeed> mock_feed_;
    std::shared_ptr<Normalizer> normalizer_;
    std::shared_ptr<PubServer> pub_server_;
    std::shared_ptr<Recorder> recorder_;
    std::shared_ptr<Replayer> replayer_;
    std::shared_ptr<SymbolRegistry> symbol_registry_;
    
    // WebSocket metrics
    std::unique_ptr<std::jthread> metrics_thread_;
    std::vector<std::shared_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>>> ws_connections_;
    std::mutex ws_connections_mutex_;
};

} // namespace md