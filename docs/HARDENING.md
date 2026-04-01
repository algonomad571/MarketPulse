# MarketPulse System Hardening Guide

## Overview

This document describes the failure modes, hardening measures, assumptions, and limitations of the MarketPulse system. All critical paths have been analyzed and hardened against common failure scenarios.

**Last Updated:** February 23, 2026
**Hardening Level:** Production-Ready

---

## Table of Contents

1. [Failure Modes & Mitigations](#failure-modes--mitigations)
2. [Component-Specific Hardening](#component-specific-hardening)
3. [System Assumptions](#system-assumptions)
4. [Known Limitations](#known-limitations)
5. [Monitoring & Alerting](#monitoring--alerting)
6. [Recovery Procedures](#recovery-procedures)

---

## Failure Modes & Mitigations

### 1. Network Failures

#### Failure Modes:
- TCP connections timing out or stalling
- Client disconnections mid-stream
- Slow or unresponsive clients causing backpressure
- Accept errors due to resource exhaustion

#### Mitigations:
- **Write Timeout:** 5-second timeout on all TCP writes (Publisher)
- **Read Timeout:** 30-second timeout on control messages (Publisher)
- **Circuit Breaker:** Disconnect after 5 consecutive write failures
- **Connection Limit:** Maximum 1000 concurrent connections
- **Error Classification:** Distinguish between temporary vs fatal errors
- **Backpressure Handling:** Queue depth limits with configurable drop policies

**Metrics:**
- `publisher_write_errors_total` - Failed TCP writes
- `publisher_read_errors_total` - Failed TCP reads
- `publisher_accept_errors_total` - Connection accept failures
- `publisher_connections_rejected_total` - Connections rejected due to limits

---

### 2. File I/O Failures

#### Failure Modes:
- Disk full or insufficient space
- File system errors (permissions, corruption)
- Transient I/O errors
- File descriptor exhaustion

#### Mitigations:
- **Disk Space Checking:** Verify 100MB free before opening files
- **Retry with Backoff:** 3 retries with exponential backoff (100ms, 200ms, 400ms)
- **Circuit Breaker:** Enter degraded mode after 10 consecutive failures
- **Degraded Mode:** Drop frames to prevent queue backup while maintaining system stability
- **Explicit Error Logging:** All file operations log failures with context
- **Graceful Degradation:** System continues running even if recording fails

**Metrics:**
- `recorder_write_errors_total` - Write operation failures
- `recorder_file_open_errors_total` - File open failures
- `recorder_disk_full_errors_total` - Disk space exhaustion
- `recorder_degraded_mode` - Gauge (1 = degraded, 0 = normal)
- `recorder_degraded_drops_total` - Frames dropped in degraded mode

**Degraded Mode Behavior:**
- Recording stops accepting new frames
- Existing queue continues draining
- System remains operational for publishing/normalization
- Alert operators via metrics

---

### 3. Queue Failures

#### Failure Modes:
- Queue full (rare with dynamic queues)
- Memory allocation failures
- Enqueue/dequeue contention

#### Mitigations:
- **Lock-Free Queues:** moodycamel::ConcurrentQueue minimizes contention
- **Dynamic Growth:** Queues grow as needed (bounded by available memory)
- **Enqueue Validation:** Check enqueue success and log failures
- **Backpressure Monitoring:** Track queue depths via metrics
- **Bounded Caching:** Limit topic cache to prevent unbounded growth

**Metrics:**
- `normalizer_enqueue_failures_total` - Failed enqueues to output queue
- `pipeline_*_queue_approx` - Approximate queue depths (backpressure indicator)

**Queue Sizing Assumptions:**
- Feed → Normalizer: Sized for burst of 100k events
- Normalizer → Publisher: Sized for 4 threads × batch size
- Normalizer → Recorder: Sized for 4 threads × batch size

---

### 4. Thread Failures

#### Failure Modes:
- Uncaught exceptions in worker threads
- Thread deadlocks or hangs
- Resource exhaustion (thread limits)

#### Mitigations:
- **Exception Handling:** All worker threads catch and log exceptions
- **Thread Health Monitoring:** Track thread activity via metrics
- **Graceful Shutdown:** std::jthread with stop_token for clean termination
- **IO Thread Error Propagation:** IO errors trigger system shutdown
- **Emergency Cleanup:** Log all errors during cleanup, even in fatal paths

**No Silent Failures:**
- Empty catch blocks eliminated
- All exceptions logged before re-throwing
- Failed operations increment error metrics

---

### 5. Component Startup Failures

#### Failure Modes:
- Component initialization throws exception
- Partial startup (some components running, others failed)
- Resource unavailability (ports, files, memory)

#### Mitigations:
- **Dependency Order Startup:** Components start in correct dependency order
- **Rollback on Failure:** Stop successfully started components if one fails
- **Exception Propagation:** Startup exceptions bubble up with context
- **Clear Error Messages:** Log which component failed and why
- **Fail-Fast:** Don't run with partially initialized state

---

## Component-Specific Hardening

### Recorder (src/recorder/recorder.cpp)

**Circuit Breaker Pattern:**
```cpp
consecutive_failures_ counter:
  - Incremented on each write/open failure
  - Reset to 0 on successful operation
  - Triggers degraded mode at threshold (10)
```

**Degraded Mode:**
- Activated after 10 consecutive failures
- Prevents queue backup by dropping frames
- Logged as critical error
- Requires operator intervention to exit (restart)

**File Operations:**
- **open_new_files():** 
  - Returns bool (false on failure)
  - Retries 3 times with exponential backoff
  - Checks disk space before opening
  - Validates both MDF and IDX files
  
- **write_frame():**
  - Checks for active file handle
  - Validates write success
  - Tracks consecutive failures
  - Increments error metrics

**Index File Failures:**
- Non-fatal (MDF is source of truth)
- Logged but don't block recording
- Separate error counter

---

### Publisher (src/publisher/pub_server.cpp)

**Connection Hardening:**
- Maximum 1000 concurrent connections (configurable)
- Reject new connections when at limit
- Log rejected connections

**Write Loop Circuit Breaker:**
```cpp
consecutive_failures counter (per connection):
  - Incremented on write error
  - Reset to 0 on successful write
  - Disconnect at threshold (5)
```

**Timeout Configuration:**
- **Write Timeout:** 5 seconds
  - Any write taking >5s indicates stalled connection
  - Appropriate for low-latency market data
  
- **Read Timeout:** 30 seconds
  - Control messages are infrequent
  - Prevents hung connections

**Error Classification:**
- `operation_aborted` → Debug log (normal shutdown)
- `eof` → Info log (clean disconnect)
- Other errors → Warn/Error log with metrics

**Backpressure:**
- Per-client queue limit: 10,000 frames
- Lossless subscribers: Warn and drop (TODO: implement true backpressure)
- Lossy subscribers: Drop with counter increment

---

### Normalizer (src/normalize/normalizer.cpp)

**Error Handling:**
- Catch all exceptions during normalization
- Log error with context
- Increment error metrics
- Continue processing (don't crash thread)

**Enqueue Validation:**
- Check enqueue return value
- Log failures (rare with dynamic queues)
- Increment `normalizer_enqueue_failures_total`

**Thread Safety:**
- Multiple worker threads share input/output queues
- Lock-free queue implementation
- No shared mutable state (symbol_registry is thread-safe)

---

### Control Server (src/ctrl/control_server.cpp)

**HTTP Request Handling:**
- Catch all exceptions in request handlers
- Return appropriate HTTP error codes
- Log errors with request context
- Don't crash server on malformed requests

**WebSocket Connections:**
- Handle disconnect gracefully
- Clean up resources on error
- Log connection lifecycle

---

### Main Orchestration (src/main_core.cpp)

**Startup:**
- Start components in correct order
- Return early if already running
- Catch exceptions and attempt rollback
- Re-throw with context

**Shutdown:**
- Set running_ flag early
- Stop components in reverse dependency order
- Timeout each component stop operation
- Continue even if one fails (best-effort)

**IO Thread Management:**
- Track all IO thread exceptions
- Propagate first error after cleanup
- Request system shutdown on IO error
- Join all threads before exit

**Signal Handling:**
- SIGINT (Ctrl+C) triggers graceful shutdown
- SIGTERM triggers graceful shutdown
- Emergency cleanup on fatal errors

**No Silent Failures:**
- Emergency cleanup logs all exceptions
- Never empty catch blocks
- Always log before returning error codes

---

## System Assumptions

### 1. **Disk Space**
- **Assumption:** At least 100MB free when opening new files
- **Validation:** Checked before each file roll
- **Failure Mode:** Recording enters degraded mode if check fails

### 2. **Network Bandwidth**
- **Assumption:** 85k events/sec sustainable over network
- **Reality Check:** ~1 MB/s per client (depends on frame size)
- **Failure Mode:** Backpressure → queue depth increases → frames dropped

### 3. **TCP Write Timeout**
- **Assumption:** Any write taking >5 seconds indicates a stalled connection
- **Rationale:** Market data is time-sensitive, stale data is worthless
- **Trade-off:** May disconnect slow but functional clients

### 4. **Memory Availability**
- **Assumption:** Sufficient memory for dynamic queue growth
- **Reality:** Queues bounded by available RAM
- **Failure Mode:** std::bad_alloc → thread crash → system shutdown

### 5. **File System Behavior**
- **Assumption:** std::filesystem::space() accurately reports available space
- **Limitation:** May be inaccurate on networked/virtual filesystems
- **Mitigation:** Fail-open (assume space available if check fails)

### 6. **Connection Limit**
- **Assumption:** >1000 concurrent connections indicates misconfiguration or attack
- **Rationale:** Typical deployments use 10-100 connections
- **Trade-off:** Prevents legitimate high-scale deployments

### 7. **Queue Enqueue Always Succeeds**
- **Assumption:** moodycamel::ConcurrentQueue::enqueue() rarely fails
- **Reality:** May fail under extreme memory pressure
- **Mitigation:** Check return value and log failures

### 8. **Symbol Registry Thread-Safety**
- **Assumption:** SymbolRegistry::get_or_add() is thread-safe
- **Validation:** Implementation uses mutex internally
- **Consequence:** Multiple normalizer threads can safely call concurrently

### 9. **Frame Serialization Never Fails**
- **Assumption:** encode_frame() always succeeds for valid Frame
- **Reality:** May throw on invalid data (corrupt memory)
- **Mitigation:** Caught by try-catch in write_frame()

### 10. **Configuration Validity**
- **Assumption:** config.json contains valid values
- **Validation:** None currently (TODO)
- **Failure Mode:** Undefined behavior if values out of range

---

## Known Limitations

### 1. **No Persistent Retry for File Operations**
- Failed file opens after 3 retries trigger degraded mode
- System does NOT automatically recover
- **Workaround:** Operator must restart system after fixing issue

### 2. **Degraded Mode is One-Way**
- Once Recorder enters degraded mode, it stays there until restart
- **Rationale:** Prevents flip-flop behavior
- **Impact:** Data loss continues until restart

### 3. **No True Backpressure for Lossless Subscribers**
- Lossless flag is honored but not enforced with flow control
- Currently logs warning and drops frames
- **TODO:** Implement TCP-level backpressure

### 4. **Disk Space Check Timing**
- Only checked before opening new file (not during writing)
- File writes may fail if disk fills between checks
- **Mitigation:** Circuit breaker catches repeated write failures

### 5. **Connection Limit is Global**
- 1000 connection limit applies to all clients
- No per-IP or per-user limits
- **Impact:** Single badly-behaved client can exhaust capacity

### 6. **No Authentication Rate Limiting**
- Failed authentication attempts have no rate limit
- **Security Risk:** Brute force attacks possible
- **TODO:** Implement exponential backoff on auth failures

### 7. **Index File Recovery**
- If IDX file fails but MDF succeeds, index is incomplete
- System continues recording to MDF
- **Impact:** Replay performance degraded (must scan MDF)

### 8. **No Graceful Recovery from Memory Exhaustion**
- std::bad_alloc in worker thread crashes that thread
- May cascade to system shutdown
- **Limitation:** C++ has limited recovery options for OOM

### 9. **fsync() Not Actually Performed**
- Current implementation only flushes C++ stream
- Does NOT call OS-level fsync()
- **Impact:** Data may be lost on power failure
- **TODO:** Use native file descriptor and call fsync()

### 10. **No Message Ordering Guarantees Across Topics**
- Events published to different topics may arrive out of order
- **Limitation:** Single-threaded publisher, but async I/O
- **Assumption:** Clients use timestamps for ordering

---

## Monitoring & Alerting

### Critical Metrics to Monitor

#### 1. Recorder Health
```promql
# Degraded mode alert
recorder_degraded_mode > 0

# High write error rate
rate(recorder_write_errors_total[5m]) > 10

# Disk space errors
rate(recorder_disk_full_errors_total[1m]) > 0

# Files not rolling (indicator of failure)
rate(recorder_files_rolled[1h]) == 0
```

#### 2. Publisher Health
```promql
# High write error rate
rate(publisher_write_errors_total[5m]) > 100

# Connection limit reached
publisher_connections_rejected_total > 0

# High drop rate
rate(publisher_frames_dropped_backpressure[1m]) > 1000
```

#### 3. Pipeline Health
```promql
# Queue backpressure
pipeline_feed_queue_approx > 80000
pipeline_normalizer_to_publisher_queue_approx > 80000
pipeline_normalizer_to_recorder_queue_approx > 80000

# Normalization failures
rate(normalizer_failures_total[5m]) > 100

# Enqueue failures (critical!)
rate(normalizer_enqueue_failures_total[1m]) > 0
```

#### 4. System Health
```promql
# IO thread errors
rate(io_thread_errors_total[1m]) > 0

# No data flowing (pipeline stalled)
rate(feed_events_ingested_total[2m]) == 0
```

### Recommended Alerts

| Alert | Severity | Threshold | Action |
|-------|----------|-----------|--------|
| Recorder Degraded | CRITICAL | degraded_mode == 1 | Investigate disk, restart system |
| Disk Full | CRITICAL | disk_full_errors > 0 | Clear space immediately |
| Queue Backpressure | WARNING | queue_depth > 80k | Check downstream consumers |
| High Drop Rate | WARNING | drop_rate > 1k/sec | Review client behavior |
| Pipeline Stalled | CRITICAL | ingestion_rate == 0 | Check feed, restart if needed |
| Connection Limit | WARNING | rejected > 0 | Review connection limit |
| Write Errors | WARNING | error_rate > 100/sec | Check network/clients |

---

## Recovery Procedures

### Scenario 1: Recorder in Degraded Mode

**Symptoms:**
- `recorder_degraded_mode` metric = 1
- Frames being dropped
- Logs show "entering DEGRADED MODE"

**Diagnosis:**
1. Check disk space: `df -h /path/to/data/dir`
2. Check file permissions
3. Review `recorder_write_errors_total` for failure count

**Recovery:**
1. Fix underlying issue (clear disk space, fix permissions)
2. Restart MarketPulse: `systemctl restart market-pulse`
3. Verify recording resumed: `recorder_degraded_mode` should be 0
4. Check for data gaps in MDF files

**Prevention:**
- Set up disk space monitoring
- Alert at 80% disk usage
- Implement automatic cleanup of old files

---

### Scenario 2: High Queue Backpressure

**Symptoms:**
- `pipeline_*_queue_approx` metrics high (>50k)
- Increasing over time
- Latency increasing

**Diagnosis:**
1. Identify which queue is backing up
2. Check downstream consumer performance
3. Review consumer error rates

**Recovery:**
1. If Normalizer → Recorder backup:
   - Check Recorder for errors
   - Verify disk I/O performance
   - Consider increasing recorder threads (not currently supported)

2. If Normalizer → Publisher backup:
   - Check Publisher for slow clients
   - Review client queue depths
   - Disconnect slow clients if needed

3. If Feed → Normalizer backup:
   - Reduce feed rate temporarily
   - Check Normalizer thread count (config.json)
   - Review normalizer error rate

**Prevention:**
- Monitor queue depths continuously
- Alert at 50% capacity
- Right-size thread counts for workload

---

### Scenario 3: Connection Limit Reached

**Symptoms:**
- `publisher_connections_rejected_total` increasing
- Clients unable to connect
- Logs show "Maximum connection limit reached"

**Diagnosis:**
1. Count active connections: `publisher_active_clients`
2. Review client list: GET `/clients` endpoint
3. Identify source of connection surge

**Recovery:**
1. Short-term: Disconnect idle clients
2. Medium-term: Increase connection limit (recompile)
3. Long-term: Deploy multiple Publisher instances with load balancer

**Prevention:**
- Monitor connection count
- Alert at 80% of limit
- Implement connection TTL (disconnect after inactivity)

---

### Scenario 4: Pipeline Stalled

**Symptoms:**
- `feed_events_ingested_total` not increasing
- All downstream metrics flat
- System appears running but no data flowing

**Diagnosis:**
1. Check MockFeed status: GET `/feeds` endpoint
2. Review feed error logs
3. Check if feed was stopped: `mock_feed_->stop()` called

**Recovery:**
1. Restart feed: POST `/feeds/mock/start`
2. If restart fails, restart entire system
3. Verify data flowing: monitor `feed_events_ingested_total`

**Prevention:**
- Monitor feed ingestion rate
- Alert on 60-second data gap
- Implement feed health check endpoint

---

### Scenario 5: Out of Memory

**Symptoms:**
- Logs show `std::bad_alloc` exceptions
- Worker threads crashing
- System instability

**Diagnosis:**
1. Check available memory: `free -h`
2. Review queue sizes: `pipeline_*_queue_approx`
3. Check for memory leaks: `valgrind` or similar

**Recovery:**
1. Immediate: Restart system (frees memory)
2. Short-term: Reduce queue capacity (code change)
3. Long-term: Add more RAM or reduce event rate

**Prevention:**
- Monitor memory usage
- Alert at 80% memory utilization
- Implement queue size limits

---

## Testing Failure Scenarios

### File I/O Failures
```bash
# Fill disk to test degraded mode
dd if=/dev/zero of=/path/to/data/large_file bs=1M count=<size>

# Remove write permissions
chmod 000 /path/to/data/dir

# Watch for degraded mode
watch -n 1 'curl -s http://localhost:8080/metrics | grep recorder_degraded_mode'
```

### Network Failures
```bash
# Simulate slow client with tc (Linux)
tc qdisc add dev eth0 root netem delay 1000ms

# Simulate packet loss
tc qdisc add dev eth0 root netem loss 10%

# Monitor write errors
watch -n 1 'curl -s http://localhost:8080/metrics | grep publisher_write_errors_total'
```

### Connection Limit
```bash
# Create 1000+ connections
for i in {1..1100}; do
  nc localhost 9091 &
done

# Check rejected count
curl -s http://localhost:8080/metrics | grep publisher_connections_rejected_total
```

### Memory Pressure
```bash
# Limit memory with cgroups
cgcreate -g memory:/market-pulse
echo 512M > /sys/fs/cgroup/memory/market-pulse/memory.limit_in_bytes
cgexec -g memory:market-pulse ./market_pulse_core
```

---

## Code Patterns

### Pattern 1: Circuit Breaker
```cpp
std::atomic<uint32_t> consecutive_failures_{0};
constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 10;

// On operation
if (operation_succeeds()) {
    consecutive_failures_.store(0);  // Reset on success
} else {
    consecutive_failures_.fetch_add(1);
    
    if (consecutive_failures_.load() >= MAX_CONSECUTIVE_FAILURES) {
        // Enter degraded mode
        degraded_mode_.store(true);
    }
}
```

### Pattern 2: Retry with Exponential Backoff
```cpp
bool retry_operation(std::function<bool()> operation, int max_retries, int delay_ms) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        if (operation()) {
            return true;
        }
        
        if (attempt < max_retries - 1) {
            int backoff_ms = delay_ms * (1 << attempt);  // 2^attempt
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }
    }
    return false;
}
```

### Pattern 3: Error Metrics
```cpp
try {
    risky_operation();
} catch (const std::exception& e) {
    spdlog::error("Operation failed: {}", e.what());
    MetricsCollector::instance().increment_counter("operation_failures_total");
    // Decide: re-throw, return error, or continue
}
```

### Pattern 4: Timeout Configuration
```cpp
// Set socket timeout (Boost.Asio)
socket_.set_option(boost::asio::socket_base::send_timeout(5000));  // 5 seconds
```

### Pattern 5: Resource Limits
```cpp
constexpr size_t MAX_CONNECTIONS = 1000;

if (current_connections >= MAX_CONNECTIONS) {
    spdlog::warn("Connection limit reached");
    MetricsCollector::instance().increment_counter("connections_rejected_total");
    return;  // Reject new connection
}
```

---

## Future Hardening Improvements

### High Priority
1. **True fsync() Implementation**
   - Use native file descriptors
   - Call OS-level fsync() for durability
   - Add fsync failure handling

2. **Automatic Recovery from Degraded Mode**
   - Periodic retry of file operations
   - Auto-exit degraded mode on success
   - Configurable retry interval

3. **Authentication Rate Limiting**
   - Track auth failures per IP
   - Exponential backoff on failures
   - Temporary IP bans

### Medium Priority
4. **Configuration Validation**
   - Validate all config.json values
   - Provide sensible defaults
   - Fail fast on invalid config

5. **True Backpressure for Lossless Clients**
   - Block feed when queues full
   - TCP-level flow control
   - Configurable backpressure thresholds

6. **Per-IP Connection Limits**
   - Track connections per source IP
   - Configurable per-IP limit
   - Prevent single-source exhaustion

### Low Priority
7. **Index File Recovery**
   - Rebuild index from MDF on failure
   - Atomic index writes
   - Index file checksums

8. **Memory Usage Limits**
   - Track heap usage
   - Reject new connections at memory threshold
   - Configurable memory limits

9. **Graceful Degradation Levels**
   - Level 1: Drop low-priority events
   - Level 2: Reduce frame rate
   - Level 3: Enter degraded mode
   - Level 4: Shutdown

---

## Summary

The MarketPulse system has been hardened against common failure modes:

✅ **Network failures** handled with timeouts and circuit breakers  
✅ **File I/O failures** handled with retries and degraded mode  
✅ **Queue failures** detected and logged  
✅ **Thread failures** caught and propagated  
✅ **Silent failures** eliminated with explicit logging  
✅ **System assumptions** documented  
✅ **Known limitations** acknowledged  
✅ **Recovery procedures** defined  

**Production Readiness:** System is suitable for production deployment with appropriate monitoring and alerting in place.

**Recommended Next Steps:**
1. Deploy Prometheus/Grafana monitoring
2. Configure alerts for critical metrics
3. Test failure scenarios in staging environment
4. Train operations team on recovery procedures
5. Implement high-priority future improvements
