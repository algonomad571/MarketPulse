# MarketPulse

## Low-Latency Market Data Ingestion, Recording & Replay System

MarketPulse is a high-performance market data processing platform built in Modern C++.

The project focuses on the infrastructure challenges commonly found in trading and market-data systems:

- Asynchronous market data ingestion
- Concurrent event processing
- Backpressure-aware pipelines
- Persistent recording
- Historical replay
- Real-time observability
- Containerized deployment

Rather than building trading strategies, MarketPulse focuses on the underlying data infrastructure that powers them.

---

## Key Features

### High-Performance Ingestion

- Boost.Asio based asynchronous networking
- Concurrent processing pipeline
- Per-symbol ordering guarantees
- Backpressure-aware event flow
- Configurable market data generation

### Recording & Replay

- Memory-mapped file storage
- Indexed replay infrastructure
- CRC-protected binary framing
- Deterministic playback
- Replay speeds from 1× to 100×

### Observability

- Prometheus metrics export
- Grafana dashboards
- Queue depth monitoring
- Throughput tracking
- Latency instrumentation
- Backpressure visibility

### Deployment

- Dockerized services
- Docker Compose orchestration
- Health checks
- Service discovery
- Reproducible environments

---

# Architecture

*High-level architecture showing ingestion, processing, publishing, recording, replay, and monitoring components.*

### Pipeline Overview

```text
Mock Feed / Exchange Feed
            │
            ▼
     Boost.Asio Event Loop
            │
            ▼
      Ingestion Queue
            │
            ▼
      Normalizer Workers
            │
    ┌───────┴────────┐
    ▼                ▼
Publisher        Recorder
    │                │
    ▼                ▼
WebSocket      MDF / IDX Files
Clients             │
                    ▼
               Replay Engine
```

---

# Design Decisions

## Why Boost.Asio?

I evaluated:

- Blocking sockets + thread pools
- epoll
- io_uring

Boost.Asio provided:

- Asynchronous I/O
- Cross-platform support
- Mature ecosystem
- Low development overhead

while still allowing fine-grained control over networking behavior.

---

## Why Concurrent Queues?

MarketPulse uses:

```cpp
moodycamel::ConcurrentQueue
```

between pipeline stages.

Compared to a mutex-protected queue:

```cpp
std::queue
std::mutex
```

this reduces:

- lock contention
- context switching
- cache-line bouncing

under multi-threaded workloads.

---

## Why Backpressure?

The initial implementation relied on dropping events when queues became full.

The current design implements bounded queues and producer throttling.

Benefits:

- Controlled memory usage
- Improved stability during bursts
- Reduced risk of unbounded queue growth

---

## Why Memory-Mapped Files?

MarketPulse uses memory-mapped files for recording market data.

Advantages:

- Reduced syscall overhead
- Efficient sequential writes
- Fast indexed replay
- OS-managed page caching

Tradeoffs:

- Durability depends on flush policy
- Requires careful crash recovery handling


### Recording Format

Each frame contains:

```cpp
struct FrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_type;
    uint32_t body_len;
    uint32_t crc32;
};
```

Benefits:

- Corruption detection
- Partial-frame recovery
- Replay safety

---

# Benchmark Results

The project includes a dedicated benchmark harness for measuring pipeline performance.

### Benchmark Environment

- Modern C++20
- Boost.Asio
- Dockerized runtime
- Multi-threaded pipeline

### Results

| Run | Messages Processed | Throughput |
|------|-------------------|------------|
| 1 | 508,047 | 42,680 msg/s |
| 2 | 442,191 | 37,666 msg/s |
| 3 | 530,296 | 43,419 msg/s |

Average observed throughput:

**~40k+ messages/sec**

No dropped frames were observed during benchmark runs.

---

# Observability

MarketPulse exports metrics through Prometheus and visualizes them with Grafana.

Tracked metrics include:

- Ingestion rate
- Publish rate
- Replay rate
- Queue depth
- Producer pauses
- Backpressure state
- P50 latency
- P99 latency
- Dropped frames

# Technology Stack

## Backend

- C++20
- Boost.Asio
- moodycamel::ConcurrentQueue
- spdlog
- CMake

## Frontend

- Next.js
- TypeScript
- Tailwind CSS
- Recharts

## Infrastructure

- Docker
- Docker Compose
- Prometheus
- Grafana

---

# Project Structure

```text
MarketPulse/
├── src/
│   ├── common/
│   ├── feed/
│   ├── normalize/
│   ├── publisher/
│   ├── recorder/
│   ├── replay/
│   ├── ctrl/
│   └── main_core.cpp
│
├── benchmarks/
│
├── ui/
│
├── infra/
│   ├── prometheus/
│   └── grafana/
│
├── docs/
│
├── docker-compose.yml
├── CMakeLists.txt
└── config.json
```

---

# Quick Start

## Using Docker

```bash
git clone <repository-url>
cd MarketPulse

docker compose up -d
```

Services:

| Service | URL |
|----------|-----|
| Dashboard | http://localhost:3000 |
| Prometheus | http://localhost:9090 |
| Grafana | http://localhost:3001 |
| Control API | http://localhost:8080 |

---

# Running Locally

## Build

```bash
mkdir build
cd build

cmake ..
cmake --build . --config Release
```

## Run

```bash
./md_core_main ../config.json
```

---

# Engineering Lessons

Building MarketPulse taught me several important lessons:

### Backpressure Matters More Than Peak Throughput

A stable system under bursty traffic is often more valuable than a system that only achieves impressive benchmark numbers.

### Observability Is Essential

Metrics repeatedly revealed bottlenecks that were invisible during development.

### Concurrency Is About Coordination

Getting threads to run concurrently is easy.

Getting them to coordinate correctly while preserving ordering and stability is much harder.

### Storage Design Affects Performance

Persistence mechanisms directly influence throughput, latency, and recovery behavior.

---

# Future Work

Planned improvements include:

- Binance market data connector
- Coinbase market data connector
- io_uring experimentation
- Zero-copy parsing
- Stronger durability guarantees
- Advanced latency instrumentation
- Distributed replay infrastructure



Built to explore the challenges of asynchronous networking, concurrent processing, storage systems, replay infrastructure, and observability in modern C++ systems engineering.
