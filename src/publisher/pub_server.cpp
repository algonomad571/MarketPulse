#include "pub_server.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <algorithm>

namespace md {

TopicSubscription::TopicSubscription(const std::string& pattern, bool lossless) 
    : pattern(pattern), is_wildcard(pattern.find('*') != std::string::npos), lossless(lossless) {
    
    if (is_wildcard) {
        // Convert glob pattern to regex
        std::string regex_pattern = pattern;
        // Replace * with .*
        size_t pos = 0;
        while ((pos = regex_pattern.find('*', pos)) != std::string::npos) {
            regex_pattern.replace(pos, 1, ".*");
            pos += 2;
        }
        compiled_regex = std::regex(regex_pattern);
    }
}

ClientConnection::ClientConnection(boost::asio::ip::tcp::socket socket, 
                                   const std::string& auth_token)
    : socket_(std::move(socket)), auth_token_(auth_token) {
    
    send_queue_ = std::make_unique<moodycamel::ConcurrentQueue<QueuedFrame>>();
    write_buffer_.reserve(64 * 1024); // 64KB buffer
}

void ClientConnection::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    // Start write thread
    write_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        write_loop();
    });
    
    // Start reading control messages
    read_control_messages();
    
    spdlog::info("Client connected: {}", get_remote_endpoint());
}

void ClientConnection::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    boost::system::error_code ec;
    socket_.close(ec);
    
    if (write_thread_ && write_thread_->joinable()) {
        write_thread_->request_stop();
        write_thread_->join();
    }
    
    spdlog::info("Client disconnected: {}", get_remote_endpoint());
}

void ClientConnection::send_frame(const std::string& topic, const Frame& frame) {
    if (!running_.load() || !authenticated_.load()) {
        return;
    }
    
    // Check queue depth
    if (send_queue_->size_approx() >= MAX_QUEUE_SIZE) {
        // Apply backpressure policy
        bool is_lossless = false;
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            for (const auto& sub : subscriptions_) {
                if (sub.lossless) {
                    is_lossless = true;
                    break;
                }
            }
        }
        
        if (is_lossless) {
            // For lossless subscribers, we should block or apply backpressure
            // For now, just drop and warn
            frames_dropped_.fetch_add(1);
            MetricsCollector::instance().increment_counter("publisher_frames_dropped_backpressure");
            return;
        } else {
            frames_dropped_.fetch_add(1);
            MetricsCollector::instance().increment_counter("publisher_frames_dropped_queue_full");
            return;
        }
    }
    
    QueuedFrame queued_frame;
    queued_frame.topic = topic;
    
    // Serialize frame
    std::vector<std::byte> temp_buffer;
    auto encoded = encode_frame(frame, temp_buffer);
    queued_frame.data.assign(encoded.begin(), encoded.end());
    
    send_queue_->enqueue(std::move(queued_frame));
}

void ClientConnection::send_heartbeat() {
    HbBody hb_body;
    hb_body.ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    Frame heartbeat(hb_body);
    send_frame("heartbeat", heartbeat);
}

void ClientConnection::send_control_ack(uint32_t ack_code) {
    ControlAckBody ack_body;
    ack_body.ack_code = ack_code;
    ack_body.reserved = 0;
    
    Frame ack_frame(ack_body);
    send_frame("control", ack_frame);
}

uint32_t ClientConnection::get_queue_depth() const {
    return static_cast<uint32_t>(send_queue_->size_approx());
}

std::string ClientConnection::get_remote_endpoint() const {
    try {
        return socket_.remote_endpoint().address().to_string() + ":" + 
               std::to_string(socket_.remote_endpoint().port());
    } catch (...) {
        return "unknown";
    }
}

void ClientConnection::read_control_messages() {
    auto self = shared_from_this();

    boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_buffer_), '\n',
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (ec) {
                // HARDENING: Distinguish between timeout and other errors
                if (ec == boost::asio::error::operation_aborted) {
                    spdlog::debug("Read operation cancelled for client {}", get_remote_endpoint());
                } else if (ec == boost::asio::error::eof) {
                    spdlog::info("Client {} disconnected (EOF)", get_remote_endpoint());
                } else {
                    spdlog::warn("Read error from client {}: {}", get_remote_endpoint(), ec.message());
                    MetricsCollector::instance().increment_counter("publisher_read_errors_total");
                }
                stop();
                return;
            }
            
            std::string message(read_buffer_.data(), length - 1); // exclude \n
            read_buffer_.erase(0, length);
            
            process_control_message(message);
            
            // Continue reading
            if (running_.load()) {
                read_control_messages();
            }
        });
}

void ClientConnection::write_loop() {
    const size_t batch_size = 100;
    std::vector<QueuedFrame> frames_batch;
    frames_batch.reserve(batch_size);
    
    // HARDENING: Track consecutive write failures for circuit breaker
    uint32_t consecutive_failures = 0;
    constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 5;
    
    while (running_.load()) {
        frames_batch.clear();
        QueuedFrame frame;
        while (frames_batch.size() < batch_size && send_queue_->try_dequeue(frame)) {
            frames_batch.push_back(std::move(frame));
        }

        size_t dequeued = frames_batch.size();
        
        if (dequeued == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Send batch
        for (size_t i = 0; i < dequeued; ++i) {
            const auto& frame = frames_batch[i];

            boost::system::error_code ec;
            boost::asio::write(socket_, boost::asio::buffer(frame.data), ec);
            
            if (ec) {
                spdlog::warn("Write error to client {} (attempt {}/{}): {}", 
                           get_remote_endpoint(), consecutive_failures + 1, MAX_CONSECUTIVE_FAILURES, ec.message());
                
                consecutive_failures++;
                MetricsCollector::instance().increment_counter("publisher_write_errors_total");
                
                // HARDENING: Circuit breaker - disconnect after repeated failures
                // LIMITATION: Don't retry writes, as TCP already handles retransmission
                if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                    spdlog::error("Client {} disconnected after {} consecutive write failures", 
                                get_remote_endpoint(), consecutive_failures);
                    stop();
                    return;
                }
                
                // Continue trying other frames in batch
                continue;
            }
            
            // SUCCESS: Reset failure counter
            consecutive_failures = 0;
            frames_sent_.fetch_add(1);
        }
        
        MetricsCollector::instance().increment_counter("publisher_frames_sent_total", dequeued);
    }
}

void ClientConnection::process_control_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        std::string op = json.value("op", "");
        
        if (op == "auth") {
            std::string token = json.value("token", "");
            if (token == auth_token_) {
                authenticated_.store(true);
                send_control_ack(200); // OK
                spdlog::info("Client {} authenticated", get_remote_endpoint());
            } else {
                send_control_ack(401); // Unauthorized
                spdlog::warn("Authentication failed for client {}", get_remote_endpoint());
                MetricsCollector::instance().increment_counter("publisher_auth_failures_total");
                stop();
            }
        } else if (op == "subscribe") {
            if (!authenticated_.load()) {
                send_control_ack(401);
                return;
            }
            
            auto topics = json.value("topics", std::vector<std::string>{});
            bool lossless = json.value("lossless", false);
            
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            for (const auto& topic : topics) {
                subscriptions_.emplace_back(topic, lossless);
                spdlog::info("Client {} subscribed to '{}' (lossless={})", 
                           get_remote_endpoint(), topic, lossless);
            }
            
            send_control_ack(200);
            MetricsCollector::instance().increment_counter("publisher_subscriptions_total", topics.size());
            
        } else if (op == "unsubscribe") {
            // Unsubscribe from topics
            auto topics = json.value("topics", std::vector<std::string>{});
            {
                std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                
                for (const auto& topic : topics) {
                    auto it = std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                        [&topic](const TopicSubscription& sub) {
                            return sub.pattern == topic;
                        });
                    subscriptions_.erase(it, subscriptions_.end());
                }
            }
            
            send_control_ack(200);
            MetricsCollector::instance().increment_counter("publisher_unsubscriptions_total", topics.size());
        } else {
            send_control_ack(400); // Bad Request
        }
        
    } catch (const std::exception& e) {
        spdlog::warn("Error processing control message from {}: {}", get_remote_endpoint(), e.what());
        send_control_ack(400);
    }
}

// PubServer implementation

PubServer::PubServer(boost::asio::io_context& io_context, 
                     uint16_t port, 
                     const std::string& auth_token)
    : io_context_(io_context), acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      auth_token_(auth_token), port_(port) {
}

PubServer::~PubServer() {
    stop();
}

void PubServer::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    // Start heartbeat thread
    heartbeat_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        heartbeat_loop();
    });
    
    accept_connections();
    
    spdlog::info("PubServer started on port {}", port_);
}

void PubServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    boost::system::error_code ec;
    acceptor_.close(ec);
    
    // Stop all clients
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            client->stop();
        }
        clients_.clear();
    }
    
    if (heartbeat_thread_ && heartbeat_thread_->joinable()) {
        heartbeat_thread_->request_stop();
        heartbeat_thread_->join();
    }
    
    spdlog::info("PubServer stopped");
}

void PubServer::publish(const std::string& topic, const Frame& frame) {
    if (!running_.load()) {
        return;
    }
    
    MEASURE_LATENCY("publisher_publish_latency_ns");
    
    // Cache latest frame per topic (bounded cache)
    {
        std::lock_guard<std::mutex> lock(latest_frames_mutex_);
        if (latest_frames_.size() < MAX_CACHED_TOPICS || latest_frames_.count(topic) > 0) {
            latest_frames_[topic] = frame;
        }
    }
    
    std::vector<std::shared_ptr<ClientConnection>> clients_snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_snapshot = clients_;
    }
    
    uint32_t clients_sent = 0;
    for (auto& client : clients_snapshot) {
        if (!client->is_authenticated()) {
            continue;
        }
        
        // Check if client is subscribed to this topic
        bool should_send = false;
        for (const auto& sub : client->get_subscriptions()) {
            if (matches_topic(topic, sub)) {
                should_send = true;
                break;
            }
        }
        
        if (should_send) {
            client->send_frame(topic, frame);
            clients_sent++;
        }
    }
    
    stats_.frames_published.fetch_add(1);
    MetricsCollector::instance().increment_counter("publisher_frames_published_total");
    MetricsCollector::instance().set_gauge("publisher_active_clients", clients_snapshot.size());
}

void PubServer::add_virtual_topic_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(prefixes_mutex_);
    virtual_topic_prefixes_.insert(prefix);
    spdlog::info("Added virtual topic prefix: {}", prefix);
}

std::vector<std::string> PubServer::get_active_clients() const {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    result.reserve(clients_.size());
    for (const auto& client : clients_) {
        result.push_back(client->get_remote_endpoint());
    }
    
    return result;
}

void PubServer::accept_connections() {
    auto new_socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    
    acceptor_.async_accept(*new_socket,
        [this, new_socket](boost::system::error_code ec) {
            if (ec) {
                if (running_.load()) {
                    // HARDENING: Distinguish between temporary and fatal errors
                    if (ec == boost::asio::error::operation_aborted) {
                        spdlog::debug("Accept operation cancelled (server shutting down)");
                    } else {
                        spdlog::error("Accept error: {} (code: {})", ec.message(), ec.value());
                        MetricsCollector::instance().increment_counter("publisher_accept_errors_total");
                        
                        // Continue accepting despite error (could be temporary)
                        if (running_.load()) {
                            accept_connections();
                        }
                    }
                }
                return;
            }
            
            // HARDENING: Enforce maximum connection limit
            // ASSUMPTION: More than 1000 concurrent connections indicates misconfiguration or attack
            // LIMITATION: Prevents legitimate high-scale deployments beyond 1000 connections
            constexpr size_t MAX_CONNECTIONS = 1000;
            size_t current_clients = 0;
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                current_clients = clients_.size();
                
                if (current_clients >= MAX_CONNECTIONS) {
                    spdlog::warn("Maximum connection limit ({}) reached, rejecting new connection", MAX_CONNECTIONS);
                    MetricsCollector::instance().increment_counter("publisher_connections_rejected_total");
                    
                    // Close socket immediately
                    boost::system::error_code close_ec;
                    new_socket->close(close_ec);
                    
                    // Continue accepting
                    if (running_.load()) {
                        accept_connections();
                    }
                    return;
                }
            }
            
            try {
                auto client = std::make_shared<ClientConnection>(std::move(*new_socket), auth_token_);
                
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.push_back(client);
                }
                
                client->start();
                stats_.total_connections.fetch_add(1);
                stats_.active_connections.store(clients_.size());
                
                spdlog::info("New client connected: {} (total: {})", 
                           client->get_remote_endpoint(), clients_.size());
                
            } catch (const std::exception& e) {
                spdlog::error("Failed to create client connection: {}", e.what());
                MetricsCollector::instance().increment_counter("publisher_client_creation_errors_total");
            }
            
            // Continue accepting
            if (running_.load()) {
                accept_connections();
            }
        });
}

bool PubServer::matches_topic(const std::string& topic, const TopicSubscription& sub) const {
    if (sub.is_wildcard) {
        return std::regex_match(topic, sub.compiled_regex);
    } else {
        return topic == sub.pattern;
    }
}

void PubServer::heartbeat_loop() {
    auto last_log_time = std::chrono::steady_clock::now();
    uint64_t last_published_total = 0;

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!running_.load()) break;
        
        // Clean up disconnected clients and send heartbeats
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = std::remove_if(clients_.begin(), clients_.end(),
                [](const std::shared_ptr<ClientConnection>& client) {
                    return !client || !client->is_authenticated();
                });
            clients_.erase(it, clients_.end());
            
            // Send heartbeats to active clients
            for (auto& client : clients_) {
                if (client->is_authenticated()) {
                    client->send_heartbeat();
                }
            }
            
            stats_.active_connections.store(clients_.size());
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_log_time >= std::chrono::seconds(5)) {
            const uint64_t published_total = stats_.frames_published.load();
            uint64_t published_delta = 0;
            if (published_total >= last_published_total) {
                published_delta = published_total - last_published_total;
            }

            size_t latest_topics = 0;
            {
                std::lock_guard<std::mutex> lock(latest_frames_mutex_);
                latest_topics = latest_frames_.size();
            }

            spdlog::info(
                "Flow[Publisher] published={} total_published={} active_clients={} cached_topics={}",
                published_delta,
                published_total,
                stats_.active_connections.load(),
                latest_topics);

            last_published_total = published_total;
            last_log_time = now;
        }
    }
}

std::optional<Frame> PubServer::get_latest_frame(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(latest_frames_mutex_);
    auto it = latest_frames_.find(topic);
    if (it != latest_frames_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace md