#pragma once

#include "../common/frame.hpp"
#include <memory>
#include <string>
#include <fstream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

// Forward declarations
namespace moodycamel {
    template<typename T>
    class ConcurrentQueue;
}

namespace md {

class Recorder {
public:
    explicit Recorder(const std::string& data_dir, 
                      std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> input_queue,
                      uint64_t roll_bytes = 2147483648ULL, // 2GB
                      uint32_t index_interval = 10000,
                      uint32_t fsync_interval_ms = 50);
    
    ~Recorder();
    
    void start();
    void stop();
    
    struct Stats {
        std::atomic<uint64_t> frames_written{0};
        std::atomic<uint64_t> bytes_written{0};
        std::atomic<uint64_t> fsyncs_total{0};
        std::atomic<uint64_t> files_rolled{0};
        std::atomic<bool> is_recording{false};
    };
    
    const Stats& get_stats() const { return stats_; }
    
    // Force a file roll (useful for testing)
    void force_roll();

private:
    void recording_thread();
    void roll_file_if_needed(uint64_t timestamp_ns);
    void write_frame(const Frame& frame);
    void write_index_entry(uint64_t timestamp_ns, uint64_t file_offset);
    void update_mdf_header();
    void fsync_files();
    
    std::string generate_filename(uint64_t timestamp_ns) const;
    void open_new_files(uint64_t timestamp_ns);
    void close_current_files();
    
    std::string data_dir_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> input_queue_;
    uint64_t roll_bytes_;
    uint32_t index_interval_;
    std::chrono::milliseconds fsync_interval_;
    
    std::unique_ptr<std::jthread> worker_thread_;
    std::atomic<bool> running_{false};
    
    // Current file state
    std::unique_ptr<std::ofstream> mdf_file_;
    std::unique_ptr<std::ofstream> idx_file_;
    std::string current_mdf_path_;
    std::string current_idx_path_;
    
    uint64_t current_file_start_ts_;
    uint64_t current_file_bytes_;
    uint32_t current_frame_count_;
    uint32_t frames_since_last_index_;
    
    std::vector<std::byte> write_buffer_;
    
    // Timing for fsync
    std::chrono::steady_clock::time_point last_fsync_;
    bool needs_fsync_;
    
    Stats stats_;
};

} // namespace md