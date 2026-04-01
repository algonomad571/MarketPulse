# MarketPulse System Lifecycle

## Overview

MarketPulse implements a robust lifecycle management system with clear startup, shutdown, and error propagation mechanisms.

## Lifecycle Phases

### 1. Initialization

**Entry Point:** `main()` in `src/main_core.cpp`

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 1: Pre-Initialization                                │
├─────────────────────────────────────────────────────────────┤
│ 1. Setup logging (spdlog)                                  │
│ 2. Load configuration from config.json                     │
│ 3. Install signal handlers (SIGINT, SIGTERM, SIGQUIT)      │
│ 4. Validate configuration                                  │
└─────────────────────────────────────────────────────────────┘
```

**Error Handling:** Configuration errors → exit code 1

### 2. Component Creation

**Class:** `MarketDataCore` constructor

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 2: Component Creation (setup_components)             │
├─────────────────────────────────────────────────────────────┤
│ 1. Initialize CRC32 lookup table                           │
│ 2. Create lock-free queues:                                │
│    - feed_to_normalizer (100k capacity)                    │
│    - normalizer_to_publisher (100k capacity)               │
│    - normalizer_to_recorder (100k capacity)                │
│ 3. Create SymbolRegistry (shared)                          │
│ 4. Create MockFeed                                         │
│ 5. Create Normalizer (4 threads default)                   │
│ 6. Create Publisher (TCP server)                           │
│ 7. Create Recorder (disk writer)                           │
│ 8. Create Replayer (disk reader)                           │
│ 9. Create ControlServer (REST API)                         │
│ 10. Wire component references                              │
│ 11. Start distribution thread (pipeline routing)           │
└─────────────────────────────────────────────────────────────┘
```

**Error Handling:** Component creation failure → exception → exit code 1

### 3. Component Startup

**Method:** `MarketDataCore::start()`

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 3: Sequential Component Startup                      │
├─────────────────────────────────────────────────────────────┤
│ Order (dependency-aware):                                   │
│                                                             │
│ 1. Normalizer.start()                                       │
│    - Spawn 4 worker threads                                │
│    - Begin consuming from feed_to_normalizer queue         │
│                                                             │
│ 2. Publisher.start()                                        │
│    - Start TCP acceptor on port 9100                       │
│    - Start heartbeat thread                                │
│    - Initialize latest frame cache                         │
│                                                             │
│ 3. Recorder.start()                                         │
│    - Create data directory                                 │
│    - Open MDF/IDX files                                    │
│    - Start write thread                                    │
│                                                             │
│ 4. ControlServer.start()                                    │
│    - Start HTTP server on port 8080                        │
│    - Start WebSocket server on port 8081                   │
│    - Start metrics broadcast thread                        │
│                                                             │
│ 5. MockFeed.start()                                         │
│    - Start event generation thread                         │
│    - Begin producing to feed_to_normalizer queue           │
│                                                             │
│ ✅ Set running_ = true                                      │
└─────────────────────────────────────────────────────────────┘
```

**Error Handling:**
- Any component fails → catch exception
- Running flag set to false
- Call `stop()` to cleanup partial startup
- Throw `std::runtime_error` with context
- Main catches → exit code 2

### 4. Runtime Operation

**Method:** `MarketDataCore::run()`

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 4: Main Event Loop                                   │
├─────────────────────────────────────────────────────────────┤
│ 1. Spawn IO context threads (default: 2)                   │
│    - Each thread runs io_context.run()                     │
│    - Exception handling per thread                         │
│    - Auto-shutdown on IO error                             │
│                                                             │
│ 2. Wait on condition variable:                             │
│    shutdown_cv_.wait(shutdown_requested_)                  │
│                                                             │
│ 3. On shutdown signal:                                     │
│    - Stop IO context                                       │
│    - Join all IO threads (with timeout awareness)          │
│    - Propagate any IO errors                               │
└─────────────────────────────────────────────────────────────┘
```

**Data Flow During Runtime:**
```
MockFeed
   ↓ RawEvent
Normalizer (4 threads)
   ↓ Frame
distribution_thread
   ├─→ Publisher → TCP Clients
   └─→ Recorder → Disk (MDF)
```

**Error Propagation:**
- IO thread exception → caught → trigger shutdown → rethrow
- Component thread error → logged → component stops
- Main loop catches final errors → exit code 3

### 5. Signal Handling

**Function:** `signal_handler()`

```
┌─────────────────────────────────────────────────────────────┐
│ Signal Handler Behavior                                     │
├─────────────────────────────────────────────────────────────┤
│ First Signal (SIGINT/SIGTERM):                             │
│   - Set shutdown_in_progress flag                          │
│   - Log shutdown message to stderr                         │
│   - Call g_core->request_shutdown()                        │
│   - Notify shutdown condition variable                     │
│   - Begin graceful shutdown                                │
│                                                             │
│ Second Signal (force exit):                                 │
│   - Increment signal_count                                 │
│   - Log force exit message                                 │
│   - Call std::_Exit(1) immediately                         │
│   - No cleanup, immediate termination                      │
└─────────────────────────────────────────────────────────────┘
```

**Thread Safety:**
- Handler uses atomic operations only
- No spdlog in handler (not async-signal-safe)
- Uses std::cerr for immediate output

### 6. Graceful Shutdown

**Method:** `MarketDataCore::stop()`

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 6: Ordered Component Shutdown                        │
├─────────────────────────────────────────────────────────────┤
│ Stop Order (reverse dependency):                            │
│                                                             │
│ 1. MockFeed.stop() [2s timeout]                            │
│    - Stop event generation                                 │
│    - No more data entering pipeline                        │
│                                                             │
│ 2. Wait 500ms for pipeline drain                           │
│    - Allow in-flight events to complete                    │
│                                                             │
│ 3. distribution_thread.stop()                              │
│    - Request stop token                                    │
│    - Join thread                                           │
│    - No more routing                                       │
│                                                             │
│ 4. Recorder.stop() [5s timeout]                            │
│    - Flush pending writes                                  │
│    - Close MDF/IDX files                                   │
│    - Final fsync                                           │
│                                                             │
│ 5. Publisher.stop() [3s timeout]                           │
│    - Close client connections                              │
│    - Stop heartbeat thread                                 │
│    - Clear frame cache                                     │
│                                                             │
│ 6. Normalizer.stop() [2s timeout]                          │
│    - Stop worker threads                                   │
│    - Drain remaining queue items                           │
│                                                             │
│ 7. ControlServer.stop() [2s timeout]                       │
│    - Close HTTP acceptor                                   │
│    - Close WebSocket connections                           │
│    - Stop metrics broadcast                                │
│                                                             │
│ 8. io_context.stop()                                       │
│    - Stop all async operations                             │
│                                                             │
│ ✅ Set running_ = false                                     │
└─────────────────────────────────────────────────────────────┘
```

**Error Handling:**
- Each component stop wrapped in try-catch
- Log errors but continue shutdown sequence
- No exceptions leave stop() method
- Shutdown always completes

**Timing Information:**
- Total shutdown time: ~15 seconds max
- Critical path: Recorder flush (up to 5s for large buffers)

### 7. Cleanup

**Phase:** Main function cleanup block

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 7: Resource Cleanup                                  │
├─────────────────────────────────────────────────────────────┤
│ 1. g_core.reset()                                           │
│    - Calls ~MarketDataCore()                               │
│    - Calls stop() if not already stopped                   │
│    - Destroys all components                               │
│    - Releases all memory                                   │
│                                                             │
│ 2. Log final statistics                                    │
│                                                             │
│ 3. Return exit code                                        │
└─────────────────────────────────────────────────────────────┘
```

## Exit Codes

| Code | Meaning | Trigger |
|------|---------|---------|
| 0 | Success | Normal shutdown |
| 1 | Fatal error | Uncaught exception in main |
| 2 | Startup failure | Component failed to start |
| 3 | Runtime error | Error during main event loop |
| 4 | Shutdown error | Error during graceful shutdown |
| 255 | Unknown error | Unknown exception caught |

## Error Propagation Mechanisms

### 1. Component-Level Errors

**Pattern:** Each component handles its own errors

```cpp
// Example: Normalizer worker thread
void worker_thread() {
    while (running_) {
        try {
            // Process events
            Frame frame = normalize_event(event);
            output_queue_->enqueue(frame);
            stats_.frames_output++;
            
        } catch (const std::exception& e) {
            spdlog::warn("Normalization error: {}", e.what());
            stats_.errors++;
            MetricsCollector::instance().increment_counter("normalizer_errors_total");
            // Continue processing (non-fatal)
        }
    }
}
```

**Behavior:**
- Log error with context
- Increment error counter
- Update metrics
- Continue operation (graceful degradation)

### 2. IO Context Errors

**Pattern:** IO thread exception handling

```cpp
io_threads.emplace_back([&io_error, &io_error_msg]() {
    try {
        io_context_.run();
    } catch (const std::exception& e) {
        spdlog::error("IO thread error: {}", e.what());
        io_error.store(true);
        io_error_msg = e.what();
        request_shutdown();  // Trigger graceful shutdown
    }
});

// After run loop
if (io_error.load()) {
    throw std::runtime_error("IO thread error: " + io_error_msg);
}
```

**Behavior:**
- Catch IO exceptions
- Store error state atomically
- Trigger shutdown
- Propagate to main

### 3. Startup Errors

**Pattern:** Early validation and failure

```cpp
void start() {
    try {
        normalizer_->start();
        publisher_->start();
        // ... other components
        
    } catch (const std::exception& e) {
        spdlog::error("Startup failed: {}", e.what());
        running_ = false;
        stop();  // Cleanup partial startup
        throw std::runtime_error("System startup failed: " + e.what());
    }
}
```

**Behavior:**
- Stop on first failure
- Cleanup started components
- Rethrow with context
- Main catches → exit

## Monitoring & Health Checks

### Built-in Health Endpoints

**REST API:** `GET /health`

Returns:
```json
{
  "status": "ok",
  "timestamp": 1708691234,
  "components": {
    "mock_feed": {
      "l1_count": 1234567,
      "l2_count": 890123,
      "trade_count": 123456,
      "total_events": 2248146
    },
    "normalizer": {
      "events_processed": 2248146,
      "frames_output": 2248146,
      "errors": 0
    },
    "publisher": {
      "total_connections": 5,
      "active_connections": 3,
      "frames_published": 2248146
    },
    "recorder": {
      "frames_written": 2248146,
      "files_created": 2,
      "bytes_written": 1073741824
    }
  }
}
```

### Prometheus Metrics

**Endpoint:** `GET /metrics`

Key metrics:
- `normalizer_events_total` - Total events processed
- `normalizer_errors_total` - Errors during normalization
- `publisher_frames_published_total` - Frames sent to clients
- `recorder_frames_written_total` - Frames persisted
- `publisher_clients_connected` - Active TCP connections
- `frame_distribution_total` - Frames routed

### Validation Method

**Function:** `MarketDataCore::validate_health()`

```cpp
void validate_health() const {
    if (!running_.load()) {
        throw std::runtime_error("System is not running");
    }
    
    if (!mock_feed_ || !normalizer_ || !pub_server_ || 
        !recorder_ || !control_server_) {
        throw std::runtime_error("Component not initialized");
    }
}
```

## Testing Lifecycle

### Manual Testing

```bash
# Start system
./market_pulse_core

# Wait for startup (logs will show progress)

# Send SIGINT (Ctrl+C)
# Observe graceful shutdown logs

# Force exit test - start again, send SIGINT twice
# Should see force exit message
```

### Automated Testing

```bash
# Run lifecycle test
python scripts/test_pipeline.py

# Tests:
# 1. System starts successfully
# 2. All components healthy
# 3. Data flows end-to-end
# 4. Graceful shutdown works
```

### Startup Timing

Typical startup times (development machine):

| Phase | Time |
|-------|------|
| Configuration load | <10ms |
| Component creation | 50-100ms |
| Component startup | 100-500ms |
| **Total startup** | **<1 second** |

### Shutdown Timing

Typical shutdown times:

| Component | Time |
|-----------|------|
| MockFeed | <100ms |
| Pipeline drain | 500ms |
| Distribution thread | <100ms |
| Recorder | 1-5s (depends on buffer size) |
| Publisher | <500ms |
| Normalizer | <200ms |
| ControlServer | <100ms |
| **Total shutdown** | **2-7 seconds** |

## Best Practices

### 1. Always Use Graceful Shutdown

❌ **Don't:**
```bash
kill -9 <pid>  # Force kill, data loss possible
```

✅ **Do:**
```bash
kill <pid>     # SIGTERM, graceful shutdown
# or Ctrl+C    # SIGINT, graceful shutdown
```

### 2. Monitor Startup Logs

Watch for these success indicators:
```
[INFO] All components started successfully
[INFO] === System Running ===
[INFO] Entering main event loop...
```

### 3. Check Shutdown Logs

Verify clean shutdown:
```
[INFO] Shutdown requested
[INFO] Stopping MarketData Core System...
[INFO] MockFeed stopped in 50ms
[INFO] Distribution thread stopped
[INFO] Recorder stopped in 1250ms
[INFO] All components stopped gracefully
[INFO] MarketPulse Core System Stopped
[INFO] Exit code: 0
```

### 4. Handle Errors Appropriately

- **Non-fatal errors:** Log + increment counter + continue
- **Fatal errors:** Log + cleanup + exit
- **Resource errors:** Retry with backoff
- **Network errors:** Disconnect client, continue system

## Troubleshooting

### System Won't Start

**Symptom:** Exit code 2

**Check:**
1. Configuration valid? `cat config.json | jq`
2. Ports available? `netstat -an | grep 8080`
3. Data directory writable? `ls -ld data/`
4. Dependencies present? `ldd market_pulse_core`

### System Crashes During Runtime

**Symptom:** Exit code 3

**Check:**
1. Check logs for exception message
2. Review `/metrics` for anomalies before crash
3. Check disk space: `df -h`
4. Check memory: `free -h`

### Shutdown Hangs

**Symptom:** Shutdown takes >30 seconds

**Action:**
1. Wait (Recorder may be flushing GB of data)
2. Press Ctrl+C again to force exit
3. Check data directory for incomplete files
4. Reduce buffer sizes in config.json

## Configuration for Lifecycle

**config.json options:**

```json
{
  "pipeline": {
    "normalizer_threads": 4,
    "publisher_lanes": 2,
    "recorder_fsync_ms": 1000
  },
  "storage": {
    "roll_bytes": 2147483648
  }
}
```

**Impact on lifecycle:**
- More threads = longer shutdown time
- Larger roll_bytes = slower Recorder shutdown
- Higher fsync_ms = faster writes, slower shutdown

## Summary

MarketPulse implements industrial-strength lifecycle management:

✅ **Clear startup sequence** - Dependency-aware component ordering
✅ **Graceful shutdown** - Pipeline drain → component stop → cleanup
✅ **Error propagation** - Context preserved through call stack
✅ **Signal handling** - Graceful on first signal, force on second
✅ **Timeout awareness** - Each component has shutdown deadline
✅ **Exception safety** - All errors caught and logged
✅ **Resource cleanup** - RAII ensures proper cleanup
✅ **Observability** - Detailed lifecycle logging

The system is production-ready with predictable behavior in all scenarios.
