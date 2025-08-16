#include "replayer.hpp"
#include "../publisher/pub_server.hpp"
#include "../common/metrics.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <random>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace md {

Replayer::Replayer(const std::string& data_dir, std::shared_ptr<PubServer> publisher)
    : data_dir_(data_dir), publisher_(publisher) {
}

Replayer::~Replayer() {
    // Stop all active sessions
    std::vector<std::string> session_ids;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& [id, session] : sessions_) {
            session_ids.push_back(id);
        }
    }
    
    for (const auto& id : session_ids) {
        stop_session(id);
    }
}

std::string Replayer::start_session(uint64_t from_ts_ns, uint64_t to_ts_ns,
                                     const std::vector<std::string>& topics,
                                     double rate_multiplier) {
    
    // Validate inputs
    if (from_ts_ns >= to_ts_ns) {
        throw std::invalid_argument("Invalid timestamp range");
    }
    
    if (rate_multiplier <= 0 || rate_multiplier > MAX_RATE_MULTIPLIER) {
        throw std::invalid_argument("Invalid rate multiplier");
    }
    
    if (topics.empty()) {
        throw std::invalid_argument("No topics specified");
    }
    
    // Check session limit
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (sessions_.size() >= MAX_CONCURRENT_SESSIONS) {
            throw std::runtime_error("Maximum concurrent sessions exceeded");
        }
    }
    
    // Find appropriate data files
    std::string mdf_path, idx_path;
    if (!find_files_for_timestamp(from_ts_ns, mdf_path, idx_path)) {
        throw std::runtime_error("No data files found for timestamp range");
    }
    
    // Create new session
    auto session = std::make_shared<ReplaySession>();
    session->session_id = generate_session_id();
    session->start_ts_ns = from_ts_ns;
    session->end_ts_ns = to_ts_ns;
    session->rate_multiplier = rate_multiplier;
    session->topics = topics;
    session->current_ts_ns.store(from_ts_ns);
    session->mdf_path = mdf_path;
    session->idx_path = idx_path;
    
    // Open files
    session->mdf_file = std::make_unique<std::ifstream>(mdf_path, std::ios::binary);
    session->idx_file = std::make_unique<std::ifstream>(idx_path, std::ios::binary);
    
    if (!session->mdf_file->is_open() || !session->idx_file->is_open()) {
        throw std::runtime_error("Failed to open data files");
    }
    
    // Seek to start position
    if (!seek_to_timestamp(*session, from_ts_ns)) {
        throw std::runtime_error("Failed to seek to start timestamp");
    }
    
    // Initialize token bucket
    session->tokens = 1000.0; // Start with some tokens
    session->last_token_time = std::chrono::steady_clock::now();
    
    // Register virtual topics with publisher
    std::string virtual_prefix = "replay." + session->session_id;
    publisher_->add_virtual_topic_prefix(virtual_prefix);
    
    // Start playback thread
    session->running.store(true);
    session->playback_thread = std::make_unique<std::jthread>([this, session_id = session->session_id](std::stop_token token) {
        playback_thread(session_id);
    });
    
    // Add to active sessions
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[session->session_id] = session;
    }
    
    stats_.total_sessions.fetch_add(1);
    stats_.active_sessions.store(sessions_.size());
    
    spdlog::info("Started replay session {} for timestamp range {}-{} at {}x rate",
                 session->session_id, from_ts_ns, to_ts_ns, rate_multiplier);
    
    return session->session_id;
}

void Replayer::pause_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->paused.store(true);
        spdlog::info("Paused replay session {}", session_id);
    }
}

void Replayer::resume_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->paused.store(false);
        // Reset token timing
        it->second->last_token_time = std::chrono::steady_clock::now();
        spdlog::info("Resumed replay session {}", session_id);
    }
}

void Replayer::seek_session(const std::string& session_id, uint64_t ts_ns) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        auto& session = *it->second;
        if (ts_ns >= session.start_ts_ns && ts_ns <= session.end_ts_ns) {
            if (seek_to_timestamp(session, ts_ns)) {
                session.current_ts_ns.store(ts_ns);
                spdlog::info("Seeked replay session {} to timestamp {}", session_id, ts_ns);
            } else {
                spdlog::warn("Failed to seek session {} to timestamp {}", session_id, ts_ns);
            }
        }
    }
}

void Replayer::stop_session(const std::string& session_id) {
    std::shared_ptr<ReplaySession> session;
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return;
        }
        session = it->second;
        sessions_.erase(it);
    }
    
    // Stop the session
    session->running.store(false);
    if (session->playback_thread && session->playback_thread->joinable()) {
        session->playback_thread->request_stop();
        session->playback_thread->join();
    }
    
    stats_.active_sessions.store(sessions_.size());
    spdlog::info("Stopped replay session {}", session_id);
}

std::vector<std::string> Replayer::get_active_sessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> result;
    result.reserve(sessions_.size());
    
    for (const auto& [id, session] : sessions_) {
        result.push_back(id);
    }
    
    return result;
}

std::vector<Replayer::SessionInfo> Replayer::get_session_info() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<SessionInfo> result;
    result.reserve(sessions_.size());
    
    for (const auto& [id, session] : sessions_) {
        SessionInfo info;
        info.session_id = session->session_id;
        info.start_ts_ns = session->start_ts_ns;
        info.end_ts_ns = session->end_ts_ns;
        info.current_ts_ns = session->current_ts_ns.load();
        info.rate_multiplier = session->rate_multiplier;
        info.running = session->running.load();
        info.paused = session->paused.load();
        info.frames_sent = session->frames_sent.load();
        info.topics = session->topics;
        
        result.push_back(info);
    }
    
    return result;
}

void Replayer::playback_thread(const std::string& session_id) {
    std::shared_ptr<ReplaySession> session;
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return;
        }
        session = it->second;
    }
    
    spdlog::info("Playback thread started for session {}", session_id);
    
    auto last_frame_time = std::chrono::steady_clock::now();
    std::optional<uint64_t> prev_timestamp_ns;
    
    while (session->running.load()) {
        if (session->paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        auto frame_opt = read_next_frame(*session);
        if (!frame_opt) {
            // End of data or error
            spdlog::info("Replay session {} completed (end of data)", session_id);
            break;
        }
        
        Frame frame = std::move(*frame_opt);
        
        // Extract timestamp from frame
        uint64_t frame_timestamp_ns = 0;
        std::visit([&frame_timestamp_ns](const auto& body) {
            frame_timestamp_ns = body.ts_ns;
        }, frame.body);
        
        // Check if we've reached the end time
        if (frame_timestamp_ns > session->end_ts_ns) {
            spdlog::info("Replay session {} completed (end time reached)", session_id);
            break;
        }
        
        session->current_ts_ns.store(frame_timestamp_ns);
        
        // Rate limiting using original inter-arrival times
        if (prev_timestamp_ns.has_value()) {
            uint64_t original_delay_ns = frame_timestamp_ns - *prev_timestamp_ns;
            double scaled_delay_s = static_cast<double>(original_delay_ns) / 1e9 / session->rate_multiplier;
            
            if (scaled_delay_s > 0.001) { // Only delay if > 1ms
                double tokens_needed = scaled_delay_s * 1000.0; // 1000 tokens per second rate
                
                if (!consume_tokens(*session, tokens_needed)) {
                    // Rate limit - wait a bit
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue; // Try again
                }
            }
        }
        
        prev_timestamp_ns = frame_timestamp_ns;
        
        // Determine topic and publish
        std::string base_topic;
        std::visit([&base_topic, session](const auto& body) {
            if constexpr (std::is_same_v<std::decay_t<decltype(body)>, L1Body>) {
                // Get symbol name from symbol registry (we'd need a reference to it)
                base_topic = "l1.UNKNOWN"; // TODO: resolve symbol ID to name
            } else if constexpr (std::is_same_v<std::decay_t<decltype(body)>, L2Body>) {
                base_topic = "l2.UNKNOWN";
            } else if constexpr (std::is_same_v<std::decay_t<decltype(body)>, TradeBody>) {
                base_topic = "trade.UNKNOWN";
            }
        }, frame.body);
        
        // Check if this topic matches any subscribed patterns
        bool should_send = false;
        for (const auto& topic_pattern : session->topics) {
            if (topic_pattern == "*" || 
                topic_pattern.find("*") != std::string::npos ||
                base_topic.find(topic_pattern) == 0) {
                should_send = true;
                break;
            }
        }
        
        if (should_send) {
            std::string virtual_topic = get_virtual_topic(session_id, base_topic);
            publisher_->publish(virtual_topic, frame);
            session->frames_sent.fetch_add(1);
            stats_.total_frames_replayed.fetch_add(1);
        }
        
        MetricsCollector::instance().increment_counter("replayer_frames_sent_total");
    }
    
    spdlog::info("Playback thread finished for session {}", session_id);
}

bool Replayer::find_files_for_timestamp(uint64_t timestamp_ns, std::string& mdf_path, std::string& idx_path) const {
    // Convert timestamp to date for file search
    auto timestamp_s = timestamp_ns / 1000000000ULL;
    auto time = std::time_t(timestamp_s);
    
    // Look for files in data directory
    try {
        for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mdf") {
                std::string filename = entry.path().filename().string();
                
                // Parse timestamp from filename (format: md_YYYYMMDD_HHMMSS.mdf)
                if (filename.length() >= 18 && filename.substr(0, 3) == "md_") {
                    std::string date_part = filename.substr(3, 8);  // YYYYMMDD
                    std::string time_part = filename.substr(12, 6); // HHMMSS
                    
                    // Simple check - more sophisticated parsing would be better
                    // For now, just use the first file found
                    mdf_path = entry.path().string();
                    idx_path = entry.path().string();
                    idx_path.replace(idx_path.length() - 4, 4, ".idx");
                    
                    // Check if idx file exists
                    if (std::filesystem::exists(idx_path)) {
                        return true;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error searching for data files: {}", e.what());
    }
    
    return false;
}

bool Replayer::seek_to_timestamp(ReplaySession& session, uint64_t target_ts_ns) {
    if (!session.mdf_file || !session.idx_file) {
        return false;
    }
    
    // Read index entries to find the appropriate position
    session.idx_file->seekg(0, std::ios::end);
    size_t idx_size = session.idx_file->tellg();
    size_t num_entries = idx_size / sizeof(IndexEntry);
    
    if (num_entries == 0) {
        // No index, start from beginning
        session.mdf_file->seekg(sizeof(MdfHeader));
        return true;
    }
    
    // Binary search through index
    session.idx_file->seekg(0);
    std::vector<IndexEntry> entries(num_entries);
    session.idx_file->read(reinterpret_cast<char*>(entries.data()), idx_size);
    
    auto it = std::lower_bound(entries.begin(), entries.end(), target_ts_ns,
        [](const IndexEntry& entry, uint64_t timestamp) {
            return entry.ts_ns_first < timestamp;
        });
    
    if (it == entries.begin()) {
        // Target is before first index entry, start from beginning
        session.mdf_file->seekg(sizeof(MdfHeader));
    } else {
        // Use the previous entry
        --it;
        session.mdf_file->seekg(it->file_offset);
    }
    
    return session.mdf_file->good();
}

std::optional<Frame> Replayer::read_next_frame(ReplaySession& session) {
    if (!session.mdf_file || !session.mdf_file->good()) {
        return std::nullopt;
    }
    
    // Read frame header
    FrameHeader header;
    session.mdf_file->read(reinterpret_cast<char*>(&header), sizeof(FrameHeader));
    
    if (session.mdf_file->gcount() != sizeof(FrameHeader)) {
        return std::nullopt; // EOF or error
    }
    
    // Validate header
    if (header.magic != 0x4D444146 || header.version != 1) {
        spdlog::warn("Invalid frame header in replay session {}", session.session_id);
        return std::nullopt;
    }
    
    // Read body
    std::vector<std::byte> body_data(header.body_len);
    session.mdf_file->read(reinterpret_cast<char*>(body_data.data()), header.body_len);
    
    if (session.mdf_file->gcount() != header.body_len) {
        return std::nullopt;
    }
    
    // Create span and decode
    std::vector<std::byte> frame_data(sizeof(FrameHeader) + header.body_len);
    std::memcpy(frame_data.data(), &header, sizeof(FrameHeader));
    std::memcpy(frame_data.data() + sizeof(FrameHeader), body_data.data(), header.body_len);
    
    return decode_frame(std::span<const std::byte>(frame_data.data(), frame_data.size()));
}

bool Replayer::consume_tokens(ReplaySession& session, double tokens_needed) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - session.last_token_time).count();
    
    add_tokens(session, elapsed);
    session.last_token_time = now;
    
    if (session.tokens >= tokens_needed) {
        session.tokens -= tokens_needed;
        return true;
    }
    
    return false;
}

void Replayer::add_tokens(ReplaySession& session, double elapsed_seconds) {
    // Add tokens at base rate (1000 tokens per second)
    double tokens_to_add = elapsed_seconds * 1000.0 * session.rate_multiplier;
    session.tokens = std::min(session.tokens + tokens_to_add, 10000.0); // Cap at 10k tokens
}

std::string Replayer::generate_session_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "rpl_";
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

std::string Replayer::get_virtual_topic(const std::string& session_id, const std::string& base_topic) const {
    return "replay." + session_id + "." + base_topic;
}

} // namespace md