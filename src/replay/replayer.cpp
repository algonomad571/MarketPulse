#include "replayer.hpp"
#include "../publisher/pub_server.hpp"
#include "../common/symbol_registry.hpp"
#include "../common/metrics.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <random>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace md {

Replayer::Replayer(const std::string& data_dir, 
                   std::shared_ptr<PubServer> publisher,
                   std::shared_ptr<class SymbolRegistry> symbol_registry)
    : data_dir_(data_dir), publisher_(publisher), symbol_registry_(symbol_registry) {
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
    
    const bool valid_rate = std::fabs(rate_multiplier - 1.0) < 1e-9 ||
                            std::fabs(rate_multiplier - 10.0) < 1e-9 ||
                            std::fabs(rate_multiplier - 100.0) < 1e-9;
    if (!valid_rate) {
        throw std::invalid_argument("Invalid rate multiplier (supported: 1, 10, 100)");
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
    
    // Find matching data files
    std::vector<std::string> mdf_paths;
    std::vector<std::string> idx_paths;
    if (!find_files_for_range(from_ts_ns, to_ts_ns, mdf_paths, idx_paths)) {
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
    session->mdf_paths = mdf_paths;
    session->idx_paths = idx_paths;
    session->current_file_index = 0;
    session->mdf_path = session->mdf_paths.front();
    session->idx_path = session->idx_paths.front();
    
    // Open files
    session->mdf_file = std::make_unique<std::ifstream>(session->mdf_path, std::ios::binary);
    session->idx_file = std::make_unique<std::ifstream>(session->idx_path, std::ios::binary);
    
    if (!session->mdf_file->is_open() || !session->idx_file->is_open()) {
        throw std::runtime_error("Failed to open data files");
    }
    
    // Seek to start position
    if (!seek_to_timestamp(*session, from_ts_ns)) {
        throw std::runtime_error("Failed to seek to start timestamp");
    }
    
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
    
    spdlog::info("Started replay session {} for timestamp range {}-{} at {}x rate ({} file(s))",
                 session->session_id, from_ts_ns, to_ts_ns, rate_multiplier, session->mdf_paths.size());
    
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

    std::optional<uint64_t> first_timestamp_ns;
    std::chrono::steady_clock::time_point wall_start;
    auto last_progress_log = std::chrono::steady_clock::now();
    
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
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, L1Body> || std::is_same_v<T, L2Body> || std::is_same_v<T, TradeBody> || std::is_same_v<T, HbBody>) {
                frame_timestamp_ns = body.ts_ns;
            } else {
                frame_timestamp_ns = 0;
            }
        }, frame.body);
        
        // Check if we've reached the end time
        if (frame_timestamp_ns > session->end_ts_ns) {
            spdlog::info("Replay session {} completed (end time reached)", session_id);
            break;
        }
        
        session->current_ts_ns.store(frame_timestamp_ns);

        // Deterministic timestamp pacing: preserve event-time deltas scaled by replay rate.
        if (!first_timestamp_ns.has_value()) {
            first_timestamp_ns = frame_timestamp_ns;
            wall_start = std::chrono::steady_clock::now();
        } else if (frame_timestamp_ns > *first_timestamp_ns) {
            const uint64_t elapsed_data_ns = frame_timestamp_ns - *first_timestamp_ns;
            const uint64_t target_wall_ns = static_cast<uint64_t>(
                static_cast<double>(elapsed_data_ns) / session->rate_multiplier);
            const auto target_time = wall_start + std::chrono::nanoseconds(target_wall_ns);
            std::this_thread::sleep_until(target_time);
        }
        
        // Determine topic and publish
        std::string base_topic;
        std::visit([&base_topic, session, this](const auto& body) {
            std::string symbol = "UNKNOWN";
            using T = std::decay_t<decltype(body)>;

            if constexpr (std::is_same_v<T, L1Body> || std::is_same_v<T, L2Body> || std::is_same_v<T, TradeBody>) {
                if (symbol_registry_) {
                    auto sym_view = symbol_registry_->by_id(body.symbol_id);
                    if (!sym_view.empty()) {
                        symbol = std::string(sym_view);
                    }
                }
            }

            if constexpr (std::is_same_v<T, L1Body>) {
                base_topic = "l1." + symbol;
            } else if constexpr (std::is_same_v<T, L2Body>) {
                base_topic = "l2." + symbol;
            } else if constexpr (std::is_same_v<T, TradeBody>) {
                base_topic = "trade." + symbol;
            } else {
                base_topic.clear();
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

        const auto now = std::chrono::steady_clock::now();
        if (now - last_progress_log >= std::chrono::seconds(5)) {
            spdlog::info(
                "Replay progress session={} sent={} current_ts={} rate={}x",
                session_id,
                session->frames_sent.load(),
                session->current_ts_ns.load(),
                session->rate_multiplier);
            last_progress_log = now;
        }
    }
    
    spdlog::info("Playback thread finished for session {}", session_id);
}

bool Replayer::find_files_for_range(uint64_t from_ts_ns,
                                    uint64_t to_ts_ns,
                                    std::vector<std::string>& mdf_paths,
                                    std::vector<std::string>& idx_paths) const {
    std::vector<std::pair<std::string, std::string>> candidates;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mdf") {
                std::string mdf_path = entry.path().string();
                std::string idx_path = mdf_path;
                idx_path.replace(idx_path.length() - 4, 4, ".idx");
                if (!std::filesystem::exists(idx_path)) {
                    continue;
                }

                std::ifstream mdf_file(mdf_path, std::ios::binary);
                if (!mdf_file.is_open()) {
                    continue;
                }

                MdfHeader header;
                mdf_file.read(reinterpret_cast<char*>(&header), sizeof(MdfHeader));
                if (mdf_file.gcount() != sizeof(MdfHeader)) {
                    continue;
                }

                if (header.magic != 0x4D444649 || header.version != 1) {
                    continue;
                }

                const bool intersects_range = !(header.end_ts_ns < from_ts_ns || header.start_ts_ns > to_ts_ns);
                if (intersects_range) {
                    candidates.emplace_back(mdf_path, idx_path);
                }
            }
        }

        std::sort(candidates.begin(), candidates.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });

        for (const auto& [mdf, idx] : candidates) {
            mdf_paths.push_back(mdf);
            idx_paths.push_back(idx);
        }
    } catch (const std::exception& e) {
        spdlog::error("Error searching for data files: {}", e.what());
    }

    return !mdf_paths.empty();
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
        // EOF for this file: advance to next file in the replay range.
        if (session.current_file_index + 1 >= session.mdf_paths.size()) {
            return std::nullopt;
        }

        session.current_file_index++;
        session.mdf_path = session.mdf_paths[session.current_file_index];
        session.idx_path = session.idx_paths[session.current_file_index];
        session.mdf_file = std::make_unique<std::ifstream>(session.mdf_path, std::ios::binary);
        session.idx_file = std::make_unique<std::ifstream>(session.idx_path, std::ios::binary);

        if (!session.mdf_file->is_open()) {
            return std::nullopt;
        }

        // Start at first frame after MDF header.
        session.mdf_file->seekg(sizeof(MdfHeader));
        if (!session.mdf_file->good()) {
            return std::nullopt;
        }

        return read_next_frame(session);
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