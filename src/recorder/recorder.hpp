#pragma once

// Recorder: Binary persistence component for MarketPulse
// 
// ASSUMPTIONS:
// - Disk has sufficient space (checked before each file roll)
// - File system supports standard POSIX operations
// - MDF files are the source of truth (IDX is optimization)
// 
// FAILURE MODES:
// - Disk full → enters degraded mode after 10 consecutive failures
// - File open failures → retries 3 times with exponential backoff
// - Write failures → tracked, triggers degraded mode at threshold
// 
// LIMITATIONS:
// - Degraded mode is one-way (requires restart to exit)
// - fsync() only flushes C++ stream (not OS-level fsync)
// - Index file failures are non-fatal (MDF can be scanned)
// 
// RECOVERY:
// - Monitor recorder_degraded_mode metric
// - Fix disk space/permissions and restart system

#include "../common/frame.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <concurrentqueue.h>

namespace md {

class MappedAppendFile;

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
    void set_symbol_registry(std::shared_ptr<class SymbolRegistry> registry);
    
    struct Stats {
        std::atomic<uint64_t> frames_written{0};
        std::atomic<uint64_t> bytes_written{0};
        std::atomic<uint64_t> fsyncs_total{0};
        std::atomic<uint64_t> files_rolled{0};
        std::atomic<uint64_t> write_errors{0};  // Track write failures
        std::atomic<uint64_t> file_open_failures{0};  // Track file open failures
        std::atomic<bool> is_recording{false};
        std::atomic<bool> degraded_mode{false};  // Set when experiencing persistent failures
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
    bool open_new_files(uint64_t timestamp_ns);  // Returns false on failure
    void close_current_files();
    bool check_disk_space(const std::string& path, uint64_t required_bytes) const;
    bool retry_operation(std::function<bool()> operation, int max_retries, int delay_ms);
    
    std::string data_dir_;
    std::shared_ptr<moodycamel::ConcurrentQueue<Frame>> input_queue_;
    std::shared_ptr<class SymbolRegistry> symbol_registry_;
    uint64_t roll_bytes_;
    uint32_t index_interval_;
    std::chrono::milliseconds fsync_interval_;
    
    std::unique_ptr<std::jthread> worker_thread_;
    std::atomic<bool> running_{false};
    
    // Current file state
    std::unique_ptr<MappedAppendFile> mdf_file_;
    std::unique_ptr<MappedAppendFile> idx_file_;
    std::string current_mdf_path_;
    std::string current_idx_path_;
    
    uint64_t current_file_start_ts_;
    uint64_t current_file_bytes_;
    uint64_t current_idx_bytes_;
    uint32_t current_frame_count_;
    uint32_t frames_since_last_index_;
    std::unordered_set<uint32_t> unique_symbols_;
    
    std::vector<std::byte> write_buffer_;
    
    // Timing for fsync
    std::chrono::steady_clock::time_point last_fsync_;
    bool needs_fsync_;
    
    // Failure tracking - circuit breaker pattern for persistent failures
    std::atomic<uint32_t> consecutive_failures_{0};  
    static constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 10;  // Enter degraded mode after this many failures
    static constexpr uint32_t FILE_OPEN_RETRY_COUNT = 3;
    static constexpr uint32_t FILE_OPEN_RETRY_DELAY_MS = 100;
    
    Stats stats_;
};

} // namespace md