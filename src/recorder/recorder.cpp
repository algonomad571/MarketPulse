#include "recorder.hpp"
#include "../common/symbol_registry.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <functional>

namespace md {

Recorder::Recorder(const std::string& data_dir,
                   std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> input_queue,
                   uint64_t roll_bytes,
                   uint32_t index_interval,
                   uint32_t fsync_interval_ms)
    : data_dir_(data_dir), input_queue_(input_queue), symbol_registry_(nullptr),
      roll_bytes_(roll_bytes), index_interval_(index_interval), 
      fsync_interval_(std::chrono::milliseconds(fsync_interval_ms)) {
    
    // Create data directory if it doesn't exist
    std::filesystem::create_directories(data_dir_);
    
    write_buffer_.reserve(64 * 1024); // 64KB buffer
    
    current_file_start_ts_ = 0;
    current_file_bytes_ = 0;
    current_frame_count_ = 0;
    frames_since_last_index_ = 0;
    needs_fsync_ = false;
}

Recorder::~Recorder() {
    stop();
}

void Recorder::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    worker_thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
        recording_thread();
    });
    
    stats_.is_recording.store(true);
    spdlog::info("Recorder started, data_dir={}", data_dir_);
}

void Recorder::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->request_stop();
        worker_thread_->join();
    }
    
    close_current_files();
    stats_.is_recording.store(false);
    spdlog::info("Recorder stopped");
}

void Recorder::force_roll() {
    current_file_bytes_ = roll_bytes_; // This will trigger a roll on next write
}

void Recorder::recording_thread() {
    const size_t batch_size = 100;
    std::vector<Frame> frames_batch;
    frames_batch.reserve(batch_size);

    last_fsync_ = std::chrono::steady_clock::now();
    auto last_log_time = std::chrono::steady_clock::now();
    uint64_t frames_since_log = 0;
    
    while (running_.load()) {
        frames_batch.clear();
        Frame frame;
        while (frames_batch.size() < batch_size && input_queue_->try_dequeue(frame)) {
            frames_batch.push_back(std::move(frame));
        }

        size_t dequeued = frames_batch.size();
        
        if (dequeued == 0) {
            // No frames, but check if we need to fsync
            auto now = std::chrono::steady_clock::now();
            if (needs_fsync_ && (now - last_fsync_) >= fsync_interval_) {
                fsync_files();
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Process batch
        for (size_t i = 0; i < dequeued; ++i) {
            MEASURE_LATENCY("recorder_write_frame_ns");
            
            const auto& frame = frames_batch[i];
            
            // Extract timestamp and symbol from frame body
            uint64_t timestamp_ns = 0;
            uint32_t symbol_id = 0;
            std::visit([&timestamp_ns, &symbol_id](const auto& body) {
                using T = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<T, L1Body> || std::is_same_v<T, L2Body> || std::is_same_v<T, TradeBody>) {
                    timestamp_ns = body.ts_ns;
                    symbol_id = body.symbol_id;
                } else if constexpr (std::is_same_v<T, HbBody>) {
                    timestamp_ns = body.ts_ns;
                    symbol_id = 0;
                } else {
                    timestamp_ns = 0;
                    symbol_id = 0;
                }
            }, frame.body);
            
            // Track unique symbols
            if (symbol_id > 0) {
                unique_symbols_.insert(symbol_id);
            }
            
            roll_file_if_needed(timestamp_ns);
            write_frame(frame);
        }
        
        // Periodic fsync
        auto now = std::chrono::steady_clock::now();
        if (needs_fsync_ && (now - last_fsync_) >= fsync_interval_) {
            fsync_files();
        }
        
        MetricsCollector::instance().increment_counter("recorder_frames_total", dequeued);
        MetricsCollector::instance().increment_counter("recorder_records_written_total", dequeued);
        MetricsCollector::instance().set_gauge("recorder_current_file_bytes", current_file_bytes_);

        frames_since_log += dequeued;
        const auto now_for_log = std::chrono::steady_clock::now();
        if (now_for_log - last_log_time >= std::chrono::seconds(5)) {
            spdlog::info(
                "Flow[Recorder] wrote={} total_frames={} total_bytes={} active_file={}",
                frames_since_log,
                stats_.frames_written.load(),
                stats_.bytes_written.load(),
                current_mdf_path_.empty() ? std::string("<none>") : current_mdf_path_);
            frames_since_log = 0;
            last_log_time = now_for_log;
        }
    }
    
    // Final fsync on shutdown
    if (needs_fsync_) {
        fsync_files();
    }
}

void Recorder::roll_file_if_needed(uint64_t timestamp_ns) {
    // Check if we need to roll to a new file
    bool should_roll = false;
    
    if (!mdf_file_ || !idx_file_) {
        should_roll = true; // First time or after failure
    } else if (current_file_bytes_ >= roll_bytes_) {
        should_roll = true; // Size limit reached
    }
    
    if (should_roll) {
        close_current_files();
        
        // HARDENING: Check if file open succeeded
        if (!open_new_files(timestamp_ns)) {
            spdlog::error("Failed to roll to new file, continuing without active files");
            // Files will remain closed, write_frame will detect and increment failures
            consecutive_failures_.fetch_add(1);
            return;
        }
        
        stats_.files_rolled.fetch_add(1);
        
        // SUCCESS: Reset failure counter on successful roll
        consecutive_failures_.store(0);
    }
}

void Recorder::set_symbol_registry(std::shared_ptr<class SymbolRegistry> registry) {
    symbol_registry_ = registry;
}

void Recorder::write_frame(const Frame& frame) {
    // HARDENING: Check if we're in degraded mode
    if (stats_.degraded_mode.load()) {
        stats_.write_errors.fetch_add(1);
        MetricsCollector::instance().increment_counter("recorder_degraded_drops_total");
        return;  // Drop frames silently in degraded mode to prevent queue backup
    }
    
    if (!mdf_file_) {
        spdlog::error("No active MDF file for writing");
        stats_.write_errors.fetch_add(1);
        consecutive_failures_.fetch_add(1);
        
        // HARDENING: Enter degraded mode after too many consecutive failures
        if (consecutive_failures_.load() >= MAX_CONSECUTIVE_FAILURES) {
            spdlog::critical("Recorder entering DEGRADED MODE after {} consecutive failures", MAX_CONSECUTIVE_FAILURES);
            stats_.degraded_mode.store(true);
            MetricsCollector::instance().set_gauge("recorder_degraded_mode", 1);
        }
        return;
    }
    
    // Encode frame to buffer
    write_buffer_.clear();
    auto encoded = encode_frame(frame, write_buffer_);
    
    // Write to MDF file
    mdf_file_->write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if (mdf_file_->fail()) {
        spdlog::error("Failed to write frame to MDF file: {} bytes to {}", encoded.size(), current_mdf_path_);
        stats_.write_errors.fetch_add(1);
        consecutive_failures_.fetch_add(1);
        MetricsCollector::instance().increment_counter("recorder_write_errors_total");
        
        // HARDENING: Enter degraded mode if failures persist
        if (consecutive_failures_.load() >= MAX_CONSECUTIVE_FAILURES) {
            spdlog::critical("Recorder entering DEGRADED MODE after {} write failures", MAX_CONSECUTIVE_FAILURES);
            stats_.degraded_mode.store(true);
            MetricsCollector::instance().set_gauge("recorder_degraded_mode", 1);
        }
        return;
    }
    
    // SUCCESS: Reset failure counter
    consecutive_failures_.store(0);
    
    current_file_bytes_ += encoded.size();
    current_frame_count_++;
    frames_since_last_index_++;
    needs_fsync_ = true;
    
    // Update stats
    const uint64_t previous_written = stats_.frames_written.fetch_add(1);
    stats_.bytes_written.fetch_add(encoded.size());

    if (previous_written == 0) {
        spdlog::info("Flow[Recorder] first frame persisted to {}", current_mdf_path_);
    }
    
    // Write index entry if needed
    if (frames_since_last_index_ >= index_interval_) {
        uint64_t timestamp_ns = 0;
        std::visit([&timestamp_ns](const auto& body) {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, L1Body> || std::is_same_v<T, L2Body> || std::is_same_v<T, TradeBody> || std::is_same_v<T, HbBody>) {
                timestamp_ns = body.ts_ns;
            } else {
                timestamp_ns = 0;
            }
        }, frame.body);
        
        uint64_t file_offset = current_file_bytes_ - encoded.size(); // offset before this frame
        write_index_entry(timestamp_ns, file_offset);
        frames_since_last_index_ = 0;
    }
    
    // Update MDF header periodically (every 1000 frames)
    if (current_frame_count_ % 1000 == 0) {
        update_mdf_header();
    }
}

void Recorder::write_index_entry(uint64_t timestamp_ns, uint64_t file_offset) {
    if (!idx_file_) {
        spdlog::warn("No active IDX file for writing index entry");
        return;
    }
    
    IndexEntry entry;
    entry.ts_ns_first = timestamp_ns;
    entry.file_offset = file_offset;
    
    idx_file_->write(reinterpret_cast<const char*>(&entry), sizeof(IndexEntry));
    if (idx_file_->fail()) {
        // HARDENING: Log index write failures but don't fail the entire write
        // Index is for optimization only, MDF file is the source of truth
        spdlog::error("Failed to write index entry at offset {}", file_offset);
        MetricsCollector::instance().increment_counter("recorder_index_write_errors_total");
    }
}

void Recorder::update_mdf_header() {
    if (!mdf_file_) {
        return;
    }
    
    // Save current position
    auto current_pos = mdf_file_->tellp();
    
    // Seek to header and update
    mdf_file_->seekp(0);
    
    MdfHeader header;
    header.start_ts_ns = current_file_start_ts_;
    header.end_ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    header.symbol_count = static_cast<uint32_t>(unique_symbols_.size());
    header.frame_count = current_frame_count_;
    
    mdf_file_->write(reinterpret_cast<const char*>(&header), sizeof(MdfHeader));
    
    // Restore position
    mdf_file_->seekp(current_pos);
}

void Recorder::fsync_files() {
    if (mdf_file_) {
        mdf_file_->flush();
        // In a real implementation, we'd call fsync() on the file descriptor
        // For now, just flush the stream
    }
    
    if (idx_file_) {
        idx_file_->flush();
    }
    
    last_fsync_ = std::chrono::steady_clock::now();
    needs_fsync_ = false;
    stats_.fsyncs_total.fetch_add(1);
    
    MetricsCollector::instance().increment_counter("recorder_fsyncs_total");
}

std::string Recorder::generate_filename(uint64_t timestamp_ns) const {
    // Convert nanoseconds to time_t for formatting
    auto timestamp_s = timestamp_ns / 1000000000ULL;
    auto time = std::time_t(timestamp_s);
    
    std::stringstream ss;
    ss << data_dir_ << "/md_" << std::put_time(std::gmtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

bool Recorder::open_new_files(uint64_t timestamp_ns) {
    std::string base_path = generate_filename(timestamp_ns);
    std::string new_mdf_path = base_path + ".mdf";
    std::string new_idx_path = base_path + ".idx";
    
    // HARDENING: Check disk space before attempting to open files
    // Require at least 100MB free space
    if (!check_disk_space(data_dir_, 100 * 1024 * 1024)) {
        spdlog::error("Insufficient disk space in {}, cannot open new files", data_dir_);
        stats_.file_open_failures.fetch_add(1);
        MetricsCollector::instance().increment_counter("recorder_disk_full_errors_total");
        return false;
    }
    
    // HARDENING: Retry file open with exponential backoff
    auto open_mdf = [&]() -> bool {
        auto mdf_file = std::make_unique<std::ofstream>(new_mdf_path, std::ios::binary);
        if (!mdf_file->is_open()) {
            spdlog::warn("Failed to open MDF file: {}", new_mdf_path);
            return false;
        }
        
        // Write MDF header
        MdfHeader header;
        header.start_ts_ns = timestamp_ns;
        header.end_ts_ns = timestamp_ns;
        header.symbol_count = 0;
        header.frame_count = 0;
        
        mdf_file->write(reinterpret_cast<const char*>(&header), sizeof(MdfHeader));
        if (mdf_file->fail()) {
            spdlog::warn("Failed to write MDF header to {}", new_mdf_path);
            mdf_file->close();
            return false;
        }
        
        mdf_file_ = std::move(mdf_file);
        current_mdf_path_ = new_mdf_path;
        return true;
    };
    
    auto open_idx = [&]() -> bool {
        auto idx_file = std::make_unique<std::ofstream>(new_idx_path, std::ios::binary);
        if (!idx_file->is_open()) {
            spdlog::warn("Failed to open IDX file: {}", new_idx_path);
            return false;
        }
        idx_file_ = std::move(idx_file);
        current_idx_path_ = new_idx_path;
        return true;
    };
    
    // HARDENING: Retry opening MDF file
    if (!retry_operation(open_mdf, FILE_OPEN_RETRY_COUNT, FILE_OPEN_RETRY_DELAY_MS)) {
        spdlog::error("Failed to open MDF file after {} retries: {}", FILE_OPEN_RETRY_COUNT, new_mdf_path);
        stats_.file_open_failures.fetch_add(1);
        MetricsCollector::instance().increment_counter("recorder_file_open_errors_total");
        return false;
    }
    
    // HARDENING: Retry opening IDX file
    if (!retry_operation(open_idx, FILE_OPEN_RETRY_COUNT, FILE_OPEN_RETRY_DELAY_MS)) {
        spdlog::error("Failed to open IDX file after {} retries: {}", FILE_OPEN_RETRY_COUNT, new_idx_path);
        stats_.file_open_failures.fetch_add(1);
        MetricsCollector::instance().increment_counter("recorder_file_open_errors_total");
        
        // Clean up MDF file if IDX failed
        if (mdf_file_) {
            mdf_file_->close();
            mdf_file_.reset();
            current_mdf_path_.clear();
        }
        return false;
    }
    
    // Reset counters
    current_file_start_ts_ = timestamp_ns;
    current_file_bytes_ = sizeof(MdfHeader);
    current_frame_count_ = 0;
    frames_since_last_index_ = 0;
    unique_symbols_.clear();
    
    spdlog::info("Opened new files: {} and {}", current_mdf_path_, current_idx_path_);
    return true;
}

void Recorder::close_current_files() {
    if (mdf_file_) {
        update_mdf_header(); // Final header update
        mdf_file_->close();
        mdf_file_.reset();
    }
    
    if (idx_file_) {
        idx_file_->close();
        idx_file_.reset();
    }
    
    if (!current_mdf_path_.empty()) {
        spdlog::info("Closed files: {} ({}MB)", current_mdf_path_, current_file_bytes_ / (1024*1024));
        current_mdf_path_.clear();
        current_idx_path_.clear();
    }
}

// HARDENING: Retry operation with exponential backoff
bool Recorder::retry_operation(std::function<bool()> operation, int max_retries, int delay_ms) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        if (operation()) {
            return true;
        }
        
        if (attempt < max_retries - 1) {
            // Exponential backoff: delay_ms * 2^attempt
            int backoff_ms = delay_ms * (1 << attempt);
            spdlog::debug("Retry attempt {}/{}, backing off for {}ms", attempt + 1, max_retries, backoff_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }
    }
    
    return false;
}

// HARDENING: Check available disk space
// ASSUMPTION: Requires at least required_bytes free on the filesystem
// LIMITATION: Uses std::filesystem which may not be accurate on networked filesystems
bool Recorder::check_disk_space(const std::string& path, uint64_t required_bytes) const {
    try {
        auto space_info = std::filesystem::space(path);
        uint64_t available = space_info.available;
        
        if (available < required_bytes) {
            spdlog::warn("Low disk space: {} bytes available, {} required", available, required_bytes);
            return false;
        }
        
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Failed to check disk space for {}: {}", path, e.what());
        // Assume we have space if we can't check (fail-open)
        return true;
    }
}

} // namespace md