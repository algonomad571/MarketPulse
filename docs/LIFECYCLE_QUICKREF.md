# MarketPulse Lifecycle - Quick Reference

## Exit Codes

| Code | Description | Action |
|------|-------------|--------|
| 0 | Clean shutdown | None - success |
| 1 | Fatal startup error | Check config.json and logs |
| 2 | Component startup failure | Check ports, permissions, dependencies |
| 3 | Runtime error | Review logs for exception details |
| 4 | Shutdown error | May have incomplete writes, check data/ |
| 255 | Unknown error | Check system logs |

## Signal Behavior

```bash
# Graceful shutdown (recommended)
Ctrl+C (once)
kill -SIGTERM <pid>
kill -SIGINT <pid>

# Force exit (may lose data)
Ctrl+C (twice within 2 seconds)
kill -9 <pid>  # Last resort only
```

## Startup Sequence

```
1. Load config.json
2. Create components
3. Start Normalizer → Publisher → Recorder → Control → Feed
4. Spawn IO threads
5. Enter event loop
```

**Expected time:** <1 second

## Shutdown Sequence

```
1. Signal received
2. Stop Feed (no new data)
3. Drain pipeline (500ms)
4. Stop distribution thread
5. Stop Recorder → Publisher → Normalizer → Control
6. Stop IO threads
7. Exit
```

**Expected time:** 2-7 seconds (depends on buffer size)

## Health Check

```bash
# Quick health check
curl http://localhost:8080/health

# Expected response
{
  "status": "ok",
  "components": {
    "mock_feed": { "total_events": 1234567 },
    "normalizer": { "frames_output": 1234567 },
    "publisher": { "active_connections": 3 },
    "recorder": { "frames_written": 1234567 }
  }
}
```

## Common Issues

### "Address already in use"
```bash
# Find process using port
netstat -ano | findstr :8080
netstat -ano | findstr :9100

# Kill process
taskkill /PID <pid> /F
```

### "Permission denied" on data directory
```bash
# Create directory
mkdir data

# Fix permissions (Linux)
chmod 755 data
```

### Startup timeout / hangs
1. Check network.pubsub_port is available
2. Check storage.dir is writable
3. Review logs for blocked component
4. Kill and restart with --verbose

## Monitoring During Operation

```bash
# Watch health
watch -n 1 'curl -s http://localhost:8080/health | jq'

# Watch metrics
watch -n 1 'curl -s http://localhost:8080/metrics | grep total'

# Follow logs (if using file output)
tail -f marketpulse.log
```

## Testing Lifecycle

```bash
# Automated test
python scripts/test_lifecycle.py

# Manual test
./market_pulse_core &
sleep 5
curl http://localhost:8080/health
kill %1
wait
echo "Exit code: $?"
```

## Component Startup Order

| Component | Depends On | Timeout |
|-----------|------------|---------|
| Normalizer | Queues, SymbolRegistry | 2s |
| Publisher | IO Context, Queues | 3s |
| Recorder | Queues, SymbolRegistry | 5s |
| ControlServer | All components | 2s |
| MockFeed | Queues | 2s |

## Component Shutdown Order

| Component | Drains | Timeout |
|-----------|--------|---------|
| MockFeed | - | 2s |
| Distribution Thread | Yes | Instant |
| Recorder | Yes (critical) | 5s |
| Publisher | Yes | 3s |
| Normalizer | Yes | 2s |
| ControlServer | - | 2s |

## Best Practices

✅ **DO:**
- Always use graceful shutdown (Ctrl+C once)
- Wait for "shutdown complete" message
- Monitor exit code
- Check logs for errors
- Verify data/ files are closed properly

❌ **DON'T:**
- Use kill -9 except emergency
- Ignore exit codes
- Skip health checks after startup
- Restart immediately after crash (check logs first)

## Debugging Lifecycle Issues

### Startup failure
```bash
# Run with verbose logging
LOG_LEVEL=debug ./market_pulse_core

# Check configuration
jq . config.json

# Verify dependencies
ldd market_pulse_core  # Linux
otool -L market_pulse_core  # macOS
```

### Shutdown hang
```bash
# Check what's blocking
strace -p <pid>  # Linux
dtruss -p <pid>  # macOS

# Force after 30s if necessary
timeout 30s ./market_pulse_core || kill -9 $!
```

### Runtime crash
```bash
# Check core dump
gdb ./market_pulse_core core

# Review last logs
tail -100 /var/log/marketpulse.log

# Check metrics before crash
curl http://localhost:8080/metrics > pre-crash-metrics.txt
```

## Environment Variables

```bash
# Override log level
export SPDLOG_LEVEL=debug

# Change config path
./market_pulse_core /path/to/custom/config.json

# Disable signal handlers (for debugging)
export MARKETPULSE_NO_SIGNALS=1
```

## Production Deployment

```bash
# Systemd service (recommended)
systemctl start marketpulse
systemctl status marketpulse
systemctl stop marketpulse  # Sends SIGTERM

# With supervisor
supervisorctl start marketpulse
supervisorctl stop marketpulse

# Docker
docker run -d marketpulse
docker stop marketpulse  # Sends SIGTERM, waits 10s
```

## Metrics to Monitor

| Metric | Healthy Range | Alert If |
|--------|---------------|----------|
| normalizer_errors_total | 0-10 | >100 |
| publisher_clients_connected | 1-100 | >1000 |
| recorder_frames_written_total | Growing | Stalled >10s |
| frame_distribution_total | Growing | Stalled >5s |

## Quick Commands

```bash
# Start
./market_pulse_core

# Start with custom config
./market_pulse_core /path/to/config.json

# Check if running
curl -s http://localhost:8080/health | jq '.status'

# Stop gracefully
kill -SIGTERM $(pgrep market_pulse)

# View real-time stats
while true; do curl -s http://localhost:8080/health | jq '.components.mock_feed.total_events'; sleep 1; done
```

---

**For detailed lifecycle documentation, see:** [LIFECYCLE.md](LIFECYCLE.md)
