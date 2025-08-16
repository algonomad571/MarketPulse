# Market Data Feed Handler + Replay System

A high-performance C++ market data processing system with real-time streaming, recording, replay capabilities, and a modern web-based monitoring dashboard.

## 🚀 Features

### Core Functionality
- **High-throughput ingestion**: 150k+ messages/sec (L1/L2/Trade data)
- **Binary protocol**: Compact, CRC-validated frames for data integrity
- **Custom TCP pub-sub**: Topic-based routing with backpressure handling
- **Memory-mapped recording**: Efficient persistent storage with indexing
- **Deterministic replay**: 1x to 100x speed with precise timing
- **Real-time metrics**: Latency histograms, throughput, queue depths

### Architecture
- **Multi-threaded pipeline**: Lock-free queues, work-stealing pools
- **Per-symbol ordering**: Parallel processing while maintaining sequence
- **Configurable rates**: Mock feed with burst simulation
- **WebSocket metrics**: Real-time dashboard updates
- **RESTful control**: Start/stop feeds, manage replay sessions

### Performance Targets
- **Latency**: P50 < 2ms, P99 < 10ms (ingest→publish)
- **Throughput**: ≥150k L1 msg/s, ≥60k L2 msg/s on 8-core system
- **Replay**: Sustained 100x without backpressure violations

## 🏗 Architecture Overview

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

## 🛠 Technology Stack

### Backend (C++)
- **Language**: C++20 with CMake build system
- **Networking**: Boost.Asio for async I/O
- **Concurrency**: moodycamel::ConcurrentQueue (lock-free)
- **Logging**: spdlog (async mode)
- **Serialization**: Custom binary protocol
- **Storage**: Memory-mapped files with indexing
- **Metrics**: Custom histograms + Prometheus export

### Frontend (Next.js)
- **Framework**: Next.js 14 with TypeScript
- **Styling**: Tailwind CSS with custom components
- **Charts**: Recharts for real-time visualization
- **WebSocket**: Live metrics streaming
- **Icons**: Lucide React

### Infrastructure
- **Containerization**: Docker + Docker Compose
- **Monitoring**: Prometheus + Grafana
- **Optional Analytics**: ClickHouse integration
- **Development**: Hot reload, health checks

## 📁 Project Structure

```
md-system-cpp/
├── src/                          # C++ source code
│   ├── common/                   # Shared utilities
│   │   ├── frame.{hpp,cpp}       # Binary protocol
│   │   ├── symbol_registry.{hpp,cpp}
│   │   ├── config.{hpp,cpp}
│   │   └── metrics.{hpp,cpp}
│   ├── feed/                     # Data connectors
│   │   └── mock_feed.{hpp,cpp}
│   ├── normalize/                # Event processing
│   ├── publisher/                # TCP pub-sub server
│   ├── recorder/                 # Persistent storage
│   ├── replay/                   # Historical playback
│   ├── ctrl/                     # REST/WebSocket API
│   └── main_core.cpp             # Application entry
├── ui/                           # Next.js dashboard
│   ├── app/                      # App router pages
│   │   ├── components/           # Reusable UI components
│   │   ├── hooks/                # Custom React hooks
│   │   └── (pages)/              # Route pages
├── infra/                        # Infrastructure configs
│   ├── prometheus/
│   ├── grafana/
│   └── clickhouse/
├── CMakeLists.txt                # Build configuration
├── docker-compose.yml            # Full stack deployment
└── config.json                   # Runtime configuration
```

## 🚀 Quick Start

### Prerequisites
- Docker and Docker Compose
- C++20 compatible compiler (for local builds)
- Node.js 18+ (for UI development)

### Running the Complete System

1. **Clone and start all services**:
   ```bash
   git clone <repository>
   cd md-system-cpp
   docker-compose up -d
   ```

2. **Access the interfaces**:
   - **Main Dashboard**: http://localhost:3000
   - **Grafana**: http://localhost:3001 (admin/admin)
   - **Prometheus**: http://localhost:9090
   - **Control API**: http://localhost:8080

3. **Start the mock feed**:
   ```bash
   curl -X POST http://localhost:8080/feeds/start \
     -H "Content-Type: application/json" \
     -d '{"action":"start","l1_rate":50000,"l2_rate":30000,"trade_rate":5000}'
   ```

### Local Development

1. **Build C++ core**:
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   make -j$(nproc)
   ./md_core_main ../config.json
   ```

2. **Run UI in development mode**:
   ```bash
   cd ui
   npm install
   npm run dev
   ```

## 📊 Binary Protocol

### Frame Header (Little-Endian)
```cpp
struct FrameHeader {
  uint32_t magic;     // 0x4D444146 ('MDAF')
  uint16_t version;   // 1
  uint16_t msg_type;  // 1=L1, 2=L2, 3=Trade, 4=Heartbeat
  uint32_t body_len;  // bytes of body
  uint32_t crc32;     // CRC32 of body
};
```

### Message Bodies
```cpp
struct L1Body {
  uint64_t ts_ns;
  uint32_t symbol_id;
  int64_t  bid_px, ask_px;     // scaled 1e-8
  uint64_t bid_sz, ask_sz;     // scaled 1e-8
  uint64_t seq;
};

struct L2Body {
  uint64_t ts_ns;
  uint32_t symbol_id;
  uint8_t  side;        // 0=Bid, 1=Ask
  uint8_t  action;      // 0=Insert, 1=Update, 2=Delete
  uint16_t level;       // 0=best
  int64_t  price;       // scaled 1e-8
  uint64_t size;        // scaled 1e-8
  uint64_t seq;
};
```

## 🎮 Usage Examples

### Subscribe to Live Data
```bash
# Connect to TCP pub-sub (port 9100)
telnet localhost 9100

# Send subscription (JSON control messages)
{"op":"auth","token":"devtoken123"}
{"op":"subscribe","topics":["l1.BTCUSDT","trade.*"],"lossless":false}

# Receive binary frames...
```

### Start Replay Session
```bash
curl -X POST http://localhost:8080/replay/start \
  -H "Content-Type: application/json" \
  -d '{
    "action":"start",
    "from_ts_ns":1640995200000000000,
    "to_ts_ns":1640998800000000000,
    "rate":10.0,
    "topics":["l1.*"]
  }'
```

### Monitor via WebSocket
```javascript
const ws = new WebSocket('ws://localhost:8081/ws/metrics');
ws.onmessage = (event) => {
  const metrics = JSON.parse(event.data);
  console.log('P99 latency:', metrics.histograms.normalize_event_ns.p99, 'ns');
};
```

## 📈 Performance Monitoring

### Key Metrics
- **Throughput**: Messages processed per second by type
- **Latency**: P50/P95/P99 processing latencies
- **Queue Depths**: Publisher and recorder backlogs
- **Connections**: Active TCP subscribers
- **Errors**: Processing failures and drops

### Grafana Dashboards
Pre-configured dashboards include:
- System Overview (throughput, latency, connections)
- Pipeline Health (queue depths, error rates)
- Performance Analysis (latency percentiles, burst handling)

## 🔧 Configuration

### Runtime Configuration (`config.json`)
```json
{
  "network": {
    "pubsub_port": 9100,
    "ctrl_http_port": 8080,
    "ws_metrics_port": 8081
  },
  "storage": {
    "dir": "./data",
    "roll_bytes": 2147483648,
    "index_interval": 10000
  },
  "pipeline": {
    "publisher_lanes": 8,
    "normalizer_threads": 4,
    "recorder_fsync_ms": 50
  },
  "feeds": {
    "default_symbols": ["BTCUSDT", "ETHUSDT", "SOLUSDT"],
    "mock_enabled": true
  }
}
```

## 🧪 Testing & Benchmarking

### Unit Tests
```bash
cd build
make test
./tests/unit_tests
```

### Performance Benchmarks
```bash
./benchmarks/pipeline_bench
# Expected: >150k msg/s throughput, <10ms P99 latency
```

### Chaos Testing
```bash
# Test file integrity after crashes
./scripts/chaos_test.sh
```

## 📚 Documentation

- **[Protocol Specification](docs/PROTOCOL.md)**: Binary frame format details
- **[File Format](docs/FILEFORMAT.md)**: .mdf/.idx storage layout
- **[API Reference](docs/API.md)**: REST endpoints and WebSocket
- **[Runbook](docs/RUNBOOK.md)**: Operations and troubleshooting

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make changes with tests
4. Ensure benchmarks pass
5. Submit a pull request

### Development Standards
- C++20 modern idioms
- Lock-free where possible
- Comprehensive error handling
- Performance-first design
- Clean architecture principles

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

**Built for ultra-low latency market data processing with production-grade reliability.**