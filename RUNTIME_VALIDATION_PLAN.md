# MarketPulse Runtime Validation Plan

**Date**: May 19, 2026  
**Status**: Environmental Constraints Detected  
**Docker Status**: Available (29.3.1) but terminal context issue

---

## Environmental Findings

### Current Situation
- ✅ Windows 10/11 with Docker Desktop 29.3.1
- ✅ Docker Compose v5.1.1 installed
- ⚠️ PowerShell terminal PATH issue (Docker not in PATH after start)
- ⚠️ Docker daemon started but shell context needs refresh

### Solution
Use Git Bash terminal which has proper Docker integration.

---

## Phase 1 — Build Validation (PENDING)

### Build Targets
```
├── core               (Dockerfile.core - C++ application)
├── benchmark          (Dockerfile.benchmark - throughput_benchmark)
├── ui                 (ui/Dockerfile - Next.js frontend)
├── prometheus         (official image)
└── grafana            (official image)
```

### Build Command
```bash
cd /f/MUSKAN/Projects/MarketPulse
docker compose build
```

### Expected Duration
- First build: ~15-20 minutes (vcpkg compilation)
- Subsequent builds: ~5 minutes (cached layers)

### Validation Checkpoints
- [ ] Core builds without errors
- [ ] Benchmark compiles and links
- [ ] UI builds successfully
- [ ] Docker images created
- [ ] All services ready to start

---

## Phase 2 — Core Pipeline Validation (PENDING)

### Start Command
```bash
docker compose up -d core prometheus grafana
```

### Verification Points
```
Feed (MockFeed)
  ├─ Generates L1, L2, Trade events
  ├─ Produces data at configurable rate
  └─ Metric: ingestion_rate

↓

Normalizer
  ├─ Receives raw events
  ├─ Partitions by symbol via hash()
  ├─ Routes to worker-specific queues
  └─ Metrics: normalization_rate, partition_queue_depth

↓

Publisher (Pub/Sub Server)
  ├─ Receives normalized frames
  ├─ Broadcasts to TCP clients
  └─ Metric: publish_rate

↓

Recorder
  ├─ Persists frames to disk
  ├─ Binary format with CRC32
  └─ Metric: recorder_frames_total

↓

Control Server
  ├─ HTTP metrics endpoint (:8080/metrics)
  ├─ WebSocket metrics broadcast (:8081/ws/metrics)
  └─ REST API for replay control
```

### Log Inspection
```bash
docker compose logs core -f --tail 100
```

### Expected Logs
- "[Feed] Starting mock feed simulation"
- "[Normalizer] Starting normalizer with X workers"
- "[Publisher] Listening on 0.0.0.0:8081"
- "[Recorder] Recording frames to /app/data"
- "[ControlServer] HTTP server listening on 0.0.0.0:8080"

---

## Phase 3 — Backpressure Validation (PENDING)

### Force High Queue Pressure
1. Increase producer rate: Increase MockFeed event rate
2. Monitor backpressure metrics
3. Verify producer pauses trigger

### Metrics to Monitor
```
backpressure_active       (should toggle 0 ↔ 1)
queue_depth              (should spike then stabilize)
producer_pauses          (should increment)
producer_resume_count    (should increment)
```

### Test Command
```bash
# High throughput: 10,000 events/sec
curl http://localhost:8080/metrics | grep -E "backpressure|queue_depth|producer"
```

---

## Phase 4 — Prometheus Validation (PENDING)

### Scrape Verification
```bash
curl http://localhost:9090/api/v1/targets
curl http://localhost:9090/api/v1/query?query=up{job="marketpulse-core"}
```

### Metric Availability Check
```bash
curl http://localhost:8080/metrics | grep -E "ingestion_rate|normalization_rate|publish_rate|replay_rate|queue_depth|backpressure_active|producer_pauses|producer_resume_count|dropped_frames|p50_latency|p99_latency"
```

### Expected Metrics
All 11 Phase 4 metrics should be present:
- ✅ ingestion_rate
- ✅ normalization_rate
- ✅ publish_rate
- ✅ replay_rate
- ✅ queue_depth
- ✅ backpressure_active
- ✅ producer_pauses
- ✅ producer_resume_count
- ✅ dropped_frames
- ✅ p50_latency
- ✅ p99_latency

---

## Phase 5 — Grafana Validation (PENDING)

### Access Point
```
http://localhost:3001
Credentials: admin / admin
```

### Dashboard Import
1. Dashboards → Import
2. Upload: `infra/grafana/dashboards/marketpulse-observability.json`
3. Select Prometheus data source
4. Click Import

### Panel Validation
| Panel | Query | Expected Status |
|-------|-------|-----------------|
| 1. Throughput | `rate(feed_events_ingested_total[1m])` | ✅ |
| 2. Ingestion Rate | `ingestion_rate` | ✅ |
| 3. Publish Rate | `publish_rate` | ✅ |
| 4. Replay Rate | `replay_rate` | ✅ |
| 5. Queue Depth | `queue_depth` | ✅ |
| 6. Backpressure Active | `backpressure_active` | ✅ |
| 7. Producer Pauses | `rate(producer_pauses[1m])` | ✅ |
| 8. Dropped Frames | `increase(dropped_frames[5m])` | ✅ |
| 9. P50 Latency | `p50_latency / 1000` | ✅ |
| 10. P99 Latency | `p99_latency / 1000` | ✅ |

---

## Phase 6 — Benchmark Validation (PENDING)

### Run Single Producer
```bash
docker compose run benchmark --producers=1 --duration=10
```

### Expected Output
```
================================
Messages Processed: XXXX
Throughput: XXX.XX msgs/sec
Average Latency: X.XX us
P50 Latency: X.XX us
P99 Latency: X.XX us
Queue Depth: XXX
Dropped Frames: 0
Runtime: 10.00 s
================================
```

### Run Multi-Producer
```bash
docker compose run benchmark --producers=4 --duration=60
docker compose run benchmark --producers=8 --duration=60
```

### Collect Metrics
For each run:
- Messages Processed
- Throughput (msgs/sec)
- P50 Latency (µs)
- P99 Latency (µs)
- Runtime (seconds)

---

## Phase 7 — UI Validation (PENDING)

### Start UI
```bash
docker compose up -d ui
```

### Access
```
http://localhost:3000
```

### Verification Checklist
- [ ] Page loads without errors
- [ ] WebSocket connects (check console)
- [ ] Real-time metrics update
- [ ] Dashboard renders charts
- [ ] Throughput gauge visible
- [ ] Queue depth graph visible
- [ ] No console errors (F12)

---

## Phase 8 — Final Report Template

| Component | Build | Runtime | Metrics | UI | Overall |
|-----------|-------|---------|---------|----|---------| 
| Core | ? | ? | ? | N/A | ? |
| Benchmark | ? | ? | ? | N/A | ? |
| Prometheus | ✅ | ? | ? | N/A | ? |
| Grafana | ✅ | ? | ? | ? | ? |
| UI | ? | ? | ? | ? | ? |

**Legend**: ✅ = Pass | ❌ = Fail | ? = Pending

---

## Known Constraints

1. **CMake not installed locally** - Using Docker for builds
2. **Docker PATH issue in PowerShell** - Solution: Use Git Bash
3. **First build may take 20 minutes** - vcpkg compilation is slow

---

## Next Step

Open a **Git Bash** terminal (not PowerShell) and run:

```bash
cd /f/MUSKAN/Projects/MarketPulse
docker compose build
```

Then proceed through phases sequentially.

---

**Validation Owner**: GitHub Copilot  
**Target Completion**: When all phases complete or hard blocker found
