#include "recorder.hpp"
#include "../common/metrics.hpp"
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace md {

Recorder::Recorder(const std::string& data_dir,
                   std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> input_queue,
                   uint64_t roll_bytes,
                   uint32_t index_interval,
                   uint32_t fsync_interval_ms)
    : data_dir_(data_dir), input_queue_(input_queue), roll_bytes_(roll_bytes),
      index_interval_(index_interval), fsync_interval_(std::chrono::milliseconds(fsync_interval_ms)) {
    
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
    Frame frame;
    const size_t batch_size = 100;
    std::vector<Frame> frames_batch;
    frames_batch.reserve(batch_size);
    
    last_fsync_ = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        size_t dequeued = input_queue_->try_dequeue_bulk(frames_batch.data(), batch_size);
        
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
            
            // Extract timestamp from frame body for roll decision
            uint64_t timestamp_ns = 0;
            std::visit([&timestamp_ns](const auto& body) {
                timestamp_ns = body.ts_ns;
            }, frame.body);
            
            roll_file_if_needed(timestamp_ns);
            write_frame(frame);
        }
        
        // Periodic fsync
        auto now = std::chrono::steady_clock::now();
        if (needs_fsync_ && (now - last_fsync_) >= fsync_interval_) {
            fsync_files();
        }
        
        MetricsCollector::instance().increment_counter("recorder_frames_total", dequeued);
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
        should_roll = true; // First time
    } else if (current_file_bytes_ >= roll_bytes_) {
        should_roll = true; // Size limit reached
    }
    
    if (should_roll) {
        close_current_files();
        open_new_files(timestamp_ns);
        stats_.files_rolled.fetch_add(1);
    }
}

void Recorder::write_frame(const Frame& frame) {
    if (!mdf_file_) {
        spdlog::error("No active MDF file for writing");
        return;
    }
    
    // Encode frame to buffer
    write_buffer_.clear();
    auto encoded = encode_frame(frame, write_buffer_);
    
    // Write to MDF file
    mdf_file_->write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if (mdf_file_->fail()) {
        spdlog::error("Failed to write frame to MDF file");
        return;
    }
    
    current_file_bytes_ += encoded.size();
    current_frame_count_++;
    frames_since_last_index_++;
    needs_fsync_ = true;
    
    // Update stats
    stats_.frames_written.fetch_add(1);
    stats_.bytes_written.fetch_add(encoded.size());
    
    // Write index entry if needed
    if (frames_since_last_index_ >= index_interval_) {
        uint64_t timestamp_ns = 0;
        std::visit([&timestamp_ns](const auto& body) {
            timestamp_ns = body.ts_ns;
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
        return;
    }
    
    IndexEntry entry;
    entry.ts_ns_first = timestamp_ns;
    entry.file_offset = file_offset;
    
    idx_file_->write(reinterpret_cast<const char*>(&entry), sizeof(IndexEntry));
    if (idx_file_->fail()) {
        spdlog::error("Failed to write index entry");
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
    header.symbol_count = 0; // TODO: track unique symbols
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

void Recorder::open_new_files(uint64_t timestamp_ns) {
    std::string base_path = generate_filename(timestamp_ns);
    current_mdf_path_ = base_path + ".mdf";
    current_idx_path_ = base_path + ".idx";
    
    // Open MDF file
    mdf_file_ = std::make_unique<std::ofstream>(current_mdf_path_, std::ios::binary);
    if (!mdf_file_->is_open()) {
        spdlog::error("Failed to open MDF file: {}", current_mdf_path_);
        mdf_file_.reset();
        return;
    }
    
    // Open IDX file
    idx_file_ = std::make_unique<std::ofstream>(current_idx_path_, std::ios::binary);
    if (!idx_file_->is_open()) {
        spdlog::error("Failed to open IDX file: {}", current_idx_path_);
        idx_file_.reset();
        mdf_file_.reset();
        return;
    }
    
    // Write MDF header
    MdfHeader header;
    header.start_ts_ns = timestamp_ns;
    header.end_ts_ns = timestamp_ns;
    header.symbol_count = 0;
    header.frame_count = 0;
    
    mdf_file_->write(reinterpret_cast<const char*>(&header), sizeof(MdfHeader));
    
    // Reset counters
    current_file_start_ts_ = timestamp_ns;
    current_file_bytes_ = sizeof(MdfHeader);
    current_frame_count_ = 0;
    frames_since_last_index_ = 0;
    
    spdlog::info("Opened new files: {} and {}", current_mdf_path_, current_idx_path_);
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

} // namespace md