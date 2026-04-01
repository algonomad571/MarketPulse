#pragma once

// Publisher: TCP pub-sub server for real-time market data distribution
// 
// ASSUMPTIONS:
// - Any TCP write taking >5 seconds indicates a stalled connection
// - Maximum 1000 concurrent connections is sufficient
// - Clients can handle drops for non-lossless subscriptions
// 
// FAILURE MODES:
// - Slow/stalled clients → disconnected after 5 consecutive write failures
// - Connection limit reached → new connections rejected with metric
// - Network errors → classified and logged appropriately
// 
// LIMITATIONS:
// - Lossless subscriptions drop frames (no true backpressure yet)
// - No per-IP connection limits (global limit only)
// - No authentication rate limiting
// 
// RECOVERY:
// - Monitor publisher_write_errors_total
// - Check publisher_connections_rejected_total
// - Review client behavior if drop rates high

#include "../common/frame.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <mutex>
#include <regex>
#include <optional>
#include <concurrentqueue.h>

namespace md {

struct TopicSubscription {
    std::string pattern;
    std::regex compiled_regex;
    bool is_wildcard;
    bool lossless;
    
    TopicSubscription(const std::string& pattern, bool lossless);
};

class ClientConnection : public std::enable_shared_from_this<ClientConnection> {
public:
    ClientConnection(boost::asio::ip::tcp::socket socket, 
                     const std::string& auth_token);
    
    void start();
    void stop();
    void send_frame(const std::string& topic, const Frame& frame);
    void send_heartbeat();
    void send_control_ack(uint32_t ack_code);
    
    const std::vector<TopicSubscription>& get_subscriptions() const { return subscriptions_; }
    bool is_authenticated() const { return authenticated_; }
    uint32_t get_queue_depth() const;
    
    std::string get_remote_endpoint() const;

private:
    void read_control_messages();
    void write_loop();
    void process_control_message(const std::string& message);
    
    boost::asio::ip::tcp::socket socket_;
    std::string auth_token_;
    std::atomic<bool> running_{false};
    std::atomic<bool> authenticated_{false};
    
    std::vector<TopicSubscription> subscriptions_;
    std::mutex subscriptions_mutex_;
    
    // Send queue
    struct QueuedFrame {
        std::string topic;
        std::vector<std::byte> data;
    };
    
    std::unique_ptr<moodycamel::ConcurrentQueue<QueuedFrame>> send_queue_;
    std::unique_ptr<std::jthread> write_thread_;
    
    // Buffer management
    std::vector<std::byte> write_buffer_;
    std::string read_buffer_;
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    
    std::atomic<uint64_t> frames_sent_{0};
    std::atomic<uint64_t> frames_dropped_{0};
};

class PubServer {
public:
    explicit PubServer(boost::asio::io_context& io_context, 
                       uint16_t port, 
                       const std::string& auth_token);
    
    ~PubServer();
    
    void start();
    void stop();
    
    void publish(const std::string& topic, const Frame& frame);
    void add_virtual_topic_prefix(const std::string& prefix);
    
    struct Stats {
        std::atomic<uint64_t> total_connections{0};
        std::atomic<uint64_t> active_connections{0};
        std::atomic<uint64_t> frames_published{0};
        std::atomic<uint64_t> frames_dropped{0};
        std::atomic<uint64_t> auth_failures{0};
    };
    
    const Stats& get_stats() const { return stats_; }
    std::vector<std::string> get_active_clients() const;
    
    // Get latest frame for a topic (for API queries)
    std::optional<Frame> get_latest_frame(const std::string& topic) const;

private:
    void accept_connections();
    void remove_client(std::shared_ptr<ClientConnection> client);
    bool matches_topic(const std::string& topic, const TopicSubscription& sub) const;
    void heartbeat_loop();
    
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::string auth_token_;
    uint16_t port_;
    
    std::atomic<bool> running_{false};
    std::vector<std::shared_ptr<ClientConnection>> clients_;
    mutable std::mutex clients_mutex_;
    
    std::unordered_set<std::string> virtual_topic_prefixes_;
    std::mutex prefixes_mutex_;
    
    // Latest frame cache (for API queries)
    std::unordered_map<std::string, Frame> latest_frames_;
    mutable std::mutex latest_frames_mutex_;
    static constexpr size_t MAX_CACHED_TOPICS = 10000;
    
    std::unique_ptr<std::jthread> heartbeat_thread_;
    Stats stats_;
};

} // namespace md