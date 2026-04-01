# MarketPulse - Build & Deployment Guide

MarketPulse is a high-performance market data processing system with pipeline: **Feed → Normalize → Record → Publish → Metrics**

## System Architecture

```
┌─────────────┐    ┌──────────────┐    ┌──────────────┐
│ Mock Feed   │───▶│ Normalizer   │───▶│ Publisher    │
│ (Poisson)   │    │ (N threads)  │    │ (TCP server) │
└─────────────┘    └──────────────┘    └──────────────┘
                          │                     │
                          ▼                     │
                   ┌──────────────┐            │
                   │ Recorder     │            │
                   │ (mmap files) │            │
                   └──────────────┘            │
                          │                     │
                          ▼                     │
                   ┌──────────────┐            │
                   │ Replayer     │────────────┘
                   │ (rate ctrl)  │
                   └──────────────┘
```

## Quick Start: Docker (Recommended)

### Prerequisites
- Docker and Docker Compose installed
- ~2GB disk space for data

### Build and Run

```bash
# Clone/navigate to project
cd MarketPulse

# Build all services
docker-compose build

# Start the system
docker-compose up

# In another terminal, check health
curl http://localhost:8080/health
```

### Access Points
- **REST Control API**: `http://localhost:8080`
- **WebSocket Metrics**: `ws://localhost:8081`
- **TCP Pub-Sub**: `localhost:9100` (binary protocol)
- **Prometheus Metrics**: `http://localhost:9090`

---

## Native Build: Windows/Linux/macOS

### Prerequisites

**Required:**
- CMake ≥ 3.20
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2019+)
- Boost headers (system, filesystem, asio, beast, thread)
- Python 3.7+ (for dependency setup)

**Recommended:**
- ninja (faster builds than Make)
- ccache (faster rebuilds)

### Step 1: Setup External Dependencies

The project requires three header-only C++ libraries:

```bash
# From project root
python scripts/setup_dependencies.py

# This creates ./external/ with:
#   - spdlog/       (logging)
#   - nlohmann_json (JSON parsing)
#   - concurrentqueue/ (lock-free queues)
```

### Step 2: Install System Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    build-essential \
    libboost-all-dev \
    python3
```

**macOS (Homebrew):**
```bash
brew install cmake boost python3
```

**Windows (vcpkg):**
```powershell
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

# Install dependencies
.\vcpkg\vcpkg install boost-asio boost-beast boost-system --triplet x64-windows
```

### Step 3: Configure Build

```bash
# Create build directory
mkdir build && cd build

# Generate build files (Unix/macOS)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Or on Windows with vcpkg
cmake .. -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```

### Step 4: Compile

```bash
# With cmake
cmake --build . --config Release --parallel

# Or with make (Unix/macOS)
make -j$(nproc)

# Or with ninja (faster)
ninja
```

### Step 5: Run

```bash
# From project root (data/ directory will be created)
./build/md_core_main

# Or with custom config
./build/md_core_main /path/to/config.json
```

---

## Configuration

Edit `config.json` to customize:

```json
{
  "network": {
    "pubsub_port": 9100,        // TCP pub-sub server
    "ctrl_http_port": 8080,     // Control API
    "ws_metrics_port": 8081     // WebSocket metrics
  },
  "security": {
    "token": "devtoken123"      // Auth token for control API
  },
  "storage": {
    "dir": "./data",            // Data file directory
    "roll_bytes": 2147483648,   // Roll file every 2GB
    "index_interval": 10000     // Index entry every 10k frames
  },
  "pipeline": {
    "publisher_lanes": 8,       // I/O context threads
    "recorder_fsync_ms": 50,    // Fsync interval (ms)
    "normalizer_threads": 4     // Processing threads
  },
  "feeds": {
    "default_symbols": ["BTCUSDT", "ETHUSDT", "SOLUSDT"],
    "mock_enabled": true,
    "binance_enabled": false    // Future: real data connector
  }
}
```

---

## Usage: REST API

### Health Check
```bash
curl http://localhost:8080/health
```

Response:
```json
{
  "status": "ok",
  "timestamp": 1234567890,
  "components": {
    "mock_feed": {"l1_count": 12345, ...},
    "normalizer": {"frames_output": 12345, ...},
    "publisher": {"active_connections": 1, ...},
    "recorder": {"frames_written": 12345, ...}
  }
}
```

### Get Registered Symbols
```bash
curl http://localhost:8080/symbols
```

### Control Mock Feed
```bash
# Start feed with custom rates
curl -X POST http://localhost:8080/feeds/mock \
  -H "Content-Type: application/json" \
  -d '{
    "action": "start",
    "l1_rate": 50000,
    "l2_rate": 30000,
    "trade_rate": 5000
  }'

# Stop feed
curl -X POST http://localhost:8080/feeds/mock \
  -H "Content-Type: application/json" \
  -d '{"action": "stop"}'
```

### Start Replay Session
```bash
curl -X POST http://localhost:8080/replay/start \
  -H "Content-Type: application/json" \
  -d '{
    "action": "start",
    "from_ts_ns": 1640000000000000000,
    "to_ts_ns": 1640086400000000000,
    "rate": 10.0,
    "topics": ["l1.*", "trade.*"]
  }'
```

---

## Key Data Formats

### Binary Protocol
All messages use a common frame format (see `src/common/frame.hpp`):

- **Header**: 16 bytes (magic, version, type, length, CRC32)
- **Body**: Type-specific (L1, L2, Trade, Heartbeat, etc.)
- **Encoding**: Binary with CRC32 validation

### Message Types
1. **L1**: Best bid/ask with sizes
2. **L2**: Order book level updates
3. **Trade**: Trade execution events
4. **Heartbeat**: Keep-alive signals
5. **ControlAck**: ACK from control operations

### Storage Format
- **MDF Files** (.mdf): Streaming market data frames
- **Index Files** (.idx): Timestamp-offset lookup tables
- **Rolling**: New file every 2GB (configurable)

---

## Performance Targets

- **Ingest**: ≥150k messages/second
- **Latency**: P99 < 10ms (feed → publish)
- **Threads**:
  - 4 normalizer threads (default)
  - 8 publisher I/O lanes (default)
  - 1 recorder thread
  - 1 replayer thread per session

---

## Troubleshooting

### Build Issues

**CMake not found:**
- Windows: Add CMake to PATH or install via chocolatey: `choco install cmake`
- Linux: `sudo apt-get install cmake`
- macOS: `brew install cmake`

**Boost not found:**
- Windows: Use vcpkg or download pre-built boost binaries
- Linux: `sudo apt-get install libboost-all-dev`
- macOS: `brew install boost`

**External dependencies missing:**
```bash
# Re-run setup with verbose output
python scripts/setup_dependencies.py 2>&1 | tee setup.log
```

### Runtime Issues

**"Address already in use":**
- Port 8080/8081/9100 occupied. Change in config.json or kill process:
  - Linux/macOS: `lsof -i :8080`
  - Windows: `netstat -ano | findstr :8080`

**"Cannot find data directory":**
- Ensure write permissions: `mkdir -p ./data && chmod 777 ./data`

**Out of memory:**
- Large data files consume RAM. Reduce `roll_bytes` in config.json
- Or increase system limits

---

## Development: Testing the MVP

### Manual Integration Test

```bash
# 1. Start the system
./build/md_core_main &

# 2. Verify health
curl http://localhost:8080/health

# 3. Check symbols registered
curl http://localhost:8080/symbols

# 4. Start mock feed
curl -X POST http://localhost:8080/feeds/mock \
  -H "Content-Type: application/json" \
  -d '{"action": "start", "l1_rate": 10000}'

# 5. Monitor in another terminal
watch -n 1 'curl -s http://localhost:8080/health | jq .'

# 6. Check recorded files
ls -lh ./data/

# 7. Stop and inspect data integrity
curl -X POST http://localhost:8080/feeds/mock \
  -H "Content-Type: application/json" \
  -d '{"action": "stop"}'
```

### Unit Tests
```bash
cd build
ctest --verbose
```

---

## Files Structure

```
MarketPulse/
├── src/
│   ├── common/          # Shared utilities
│   │   ├── frame.{hpp,cpp}         # Binary protocol
│   │   ├── crc32.{hpp,cpp}         # Checksums
│   │   ├── symbol_registry.{hpp,cpp}
│   │   ├── config.{hpp,cpp}
│   │   └── metrics.{hpp,cpp}
│   ├── feed/            # Data ingestion
│   │   └── mock_feed.{hpp,cpp}
│   ├── normalize/       # Schema unification
│   │   └── normalizer.{hpp,cpp}
│   ├── publisher/       # TCP pub-sub server
│   │   └── pub_server.{hpp,cpp}
│   ├── recorder/        # Persistence
│   │   └── recorder.{hpp,cpp}
│   ├── replay/          # Playback
│   │   └── replayer.{hpp,cpp}
│   ├── ctrl/            # REST/WebSocket control
│   │   └── control_server.{hpp,cpp}
│   └── main_core.cpp    # Application entry
├── external/            # Third-party headers (setup via script)
├── CMakeLists.txt       # Build configuration
├── config.json          # Runtime configuration
├── docker-compose.yml   # Full-stack deployment
└── Dockerfile.core      # C++ build environment
```

---

## Next Steps

### For Production:
1. Enable SSL/TLS in pub-sub server
2. Add authentication beyond simple token
3. Implement backpressure handling
4. Add monitoring dashboard (next.js UI)
5. Integrate real data feeds (Binance WebSocket)
6. Add persistence snapshots (ClickHouse)

### For Testing:
1. Write integration tests with test fixtures
2. Add performance benchmarks
3. Stress-test with high message rates
4. Test recovery scenarios

### For Deployment:
1. Use Kubernetes instead of docker-compose
2. Add health checks and auto-restart
3. Implement graceful shutdown
4. Monitor metrics with Grafana

---

## Support & Issues

For problems or questions:
1. Check troubleshooting section above
2. Review logs for error messages
3. Verify config.json syntax (use jsonlint)
4. Check ports are accessible: `curl http://localhost:8080/health`

