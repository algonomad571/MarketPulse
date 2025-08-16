#pragma once

#include "../common/frame.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>

namespace md {

class PubServer; // forward declaration

struct ReplaySession {
    std::string session_id;
    uint64_t start_ts_ns;
    uint64_t end_ts_ns;
    double rate_multiplier;
    std::vector<std::string> topics;
    
    // State
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<uint64_t> current_ts_ns{0};
    std::atomic<uint64_t> frames_sent{0};
    
    // File handles
    std::unique_ptr<std::ifstream> mdf_file;
    std::unique_ptr<std::ifstream> idx_file;
    std::string mdf_path;
    std::string idx_path;
    
    // Playback thread
    std::unique_ptr<std::jthread> playback_thread;
    
    // Token bucket for rate limiting
    double tokens{0.0};
    std::chrono::steady_clock::time_point last_token_time;
};

class Replayer {
public:
    explicit Replayer(const std::string& data_dir, std::shared_ptr<PubServer> publisher);
    ~Replayer();
    
    // Session management
    std::string start_session(uint64_t from_ts_ns, uint64_t to_ts_ns, 
                              const std::vector<std::string>& topics,
                              double rate_multiplier = 1.0);
    
    void pause_session(const std::string& session_id);
    void resume_session(const std::string& session_id);
    void seek_session(const std::string& session_id, uint64_t ts_ns);
    void stop_session(const std::string& session_id);
    
    // Query active sessions
    std::vector<std::string> get_active_sessions() const;
    
    struct SessionInfo {
        std::string session_id;
        uint64_t start_ts_ns;
        uint64_t end_ts_ns;
        uint64_t current_ts_ns;
        double rate_multiplier;
        bool running;
        bool paused;
        uint64_t frames_sent;
        std::vector<std::string> topics;
    };
    
    std::vector<SessionInfo> get_session_info() const;
    
    struct Stats {
        std::atomic<uint64_t> total_sessions{0};
        std::atomic<uint64_t> active_sessions{0};
        std::atomic<uint64_t> total_frames_replayed{0};
    };
    
    const Stats& get_stats() const { return stats_; }

private:
    void playback_thread(const std::string& session_id);
    
    // File operations
    bool find_files_for_timestamp(uint64_t timestamp_ns, std::string& mdf_path, std::string& idx_path) const;
    bool seek_to_timestamp(ReplaySession& session, uint64_t target_ts_ns);
    std::optional<Frame> read_next_frame(ReplaySession& session);
    
    // Rate limiting
    bool consume_tokens(ReplaySession& session, double tokens_needed);
    void add_tokens(ReplaySession& session, double elapsed_seconds);
    
    std::string generate_session_id() const;
    std::string get_virtual_topic(const std::string& session_id, const std::string& base_topic) const;
    
    std::string data_dir_;
    std::shared_ptr<PubServer> publisher_;
    
    std::unordered_map<std::string, std::shared_ptr<ReplaySession>> sessions_;
    mutable std::mutex sessions_mutex_;
    
    Stats stats_;
    
    static constexpr double MAX_RATE_MULTIPLIER = 100.0;
    static constexpr size_t MAX_CONCURRENT_SESSIONS = 10;
};

} // namespace md