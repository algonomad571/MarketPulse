# RFC-002: Feature Engine Design

**Project:** MarketPulse v2 – Market Intelligence Engine (MPIE)

**Status:** Draft v2.0

**Author:** Muskan Srivastav

---

# 1. Purpose

The Feature Engine is the computational core of the MarketPulse Intelligence Engine (MPIE).

Its responsibility is to transform a deterministic stream of normalized `MarketEvent` objects into research-grade quantitative features that can be consumed by downstream analytics, historical replay systems, and future alpha models.

Unlike MarketPulse v1, which focuses on networking, ingestion, persistence, replay, and observability, the Feature Engine is purely responsible for mathematical computation.

The engine must never perform networking, storage management, or user interaction. It exists solely to convert normalized market events into deterministic feature vectors.

---

# 2. Design Goals

The architecture is designed around the following principles.

## Deterministic Execution

Identical MarketEvent streams must always produce identical FeatureVectors.

Replay at 1× speed and replay at 100× speed must generate exactly the same outputs.

No feature may depend on wall-clock time. All temporal logic is derived exclusively from the market timestamp embedded in the MarketEvent.

---

## Zero Allocation Hot Path

The processing pipeline performs no runtime memory allocation.

The hot path must never invoke:

* `new`
* `delete`
* `malloc`
* `free`

All state, rolling windows, ring buffers, and lookup tables are allocated during engine initialization.

---

## Lock-Free Processing

Feature computation must never block waiting for synchronization primitives.

Each worker owns its own queue and symbol state, eliminating contention between worker threads.

---

## Cache Efficiency

The engine follows Data-Oriented Design (DoD).

Per-symbol state is stored contiguously.

Frequently accessed data remains cache resident.

False sharing is prevented through cache-line alignment and worker isolation.

---

## Replay Parity

The Feature Engine must not know whether an event originated from:

* Live Exchange Feed
* Historical Replay
* Benchmark Harness

Every event follows the exact same execution pipeline.

---

# 3. High-Level Architecture

The Feature Engine consists of seven independent subsystems.

```
MarketEvent
      │
      ▼
Scheduler
      │
      ▼
State Manager
      │
      ▼
Context Builder
      │
      ▼
Feature Registry
      │
      ▼
Feature Executors
      │
      ▼
Feature Validator
      │
      ▼
Feature Publisher
```

Each subsystem has exactly one responsibility.

No subsystem performs responsibilities belonging to another stage.

---

# 4. Scheduler

The Scheduler orchestrates pipeline execution.

Responsibilities:

* Consume MarketEvents from the assigned SPSC queue.
* Preserve deterministic ordering.
* Invoke processing stages.
* Dispatch events to worker-local components.

The Scheduler never performs feature computation.

---

# 5. State Manager

The State Manager owns all mutable state.

Every active symbol has one SymbolState.

Example:

```
SymbolState

├── PriceState
├── BookState
├── TradeState
└── FeatureScratchpad
```

Responsibilities include:

* Previous market event
* Previous order book
* Rolling windows
* Ring buffers
* Cached statistics
* Historical snapshots

Only the State Manager may mutate SymbolState.

Feature plugins receive read-only access.

---

# 6. Context Builder

The Context Builder converts mutable SymbolState into an immutable FeatureContext.

Example:

```
FeatureContext

Current MarketEvent

Previous MarketEvent

Reference to SymbolState

Historical spans

Execution metadata

Market timestamp
```

FeatureContext is stack allocated.

Historical windows are exposed through `std::span`.

No memory copies occur.

---

# 7. Feature Registry

The Feature Registry owns all feature metadata.

Responsibilities include:

* Register plugins
* Validate plugin dependencies
* Validate execution ordering
* Store plugin metadata
* Construct execution passes

The registry contains no computational logic.

Every plugin exposes a FeatureDescriptor containing:

* Feature name
* Version
* Category
* Computational complexity
* Stateful / Stateless
* Required lookback window
* Dependency list
* Schema version

---

## Startup Validation

Before processing begins, the registry validates:

* Duplicate feature names
* Missing dependencies
* Circular dependencies
* Invalid execution ordering

The engine refuses to start if validation fails.

Execution ordering is therefore guaranteed before the first MarketEvent is processed.

---

# 8. Feature Executors

Feature Executors implement mathematical feature computation.

Each executor computes exactly one feature.

Examples include:

* Spread
* MidPrice
* MicroPrice
* Queue Imbalance
* Order Flow Imbalance
* VWAP
* Realized Volatility

Executors are pure computational units.

They:

* Read FeatureContext.
* Write FeatureVector.
* Never allocate memory.
* Never modify global state.
* Never perform logging.
* Never access storage.
* Never perform networking.

Feature plugins are intentionally side-effect free.

---

# 9. FeatureVector Contract

FeatureVector is the stable output contract of the Feature Engine.

It represents the interface between:

* Feature Engine
* Shared Memory
* Feature Store
* Future Signal Engine
* Research Environment

FeatureVector is defined as:

* Fixed-size
* Trivially copyable
* Standard-layout POD
* No heap allocations
* No pointers
* No virtual functions
* No dynamically sized containers

Its binary layout remains stable across runtime components.

Any structural modification requires a schema version increment.

This guarantees:

* Zero-copy shared memory publication
* Deterministic persistence
* Direct memory mapping
* Binary compatibility

---

# 10. Feature Validator

Every FeatureVector passes through the validation stage.

Validation protects downstream consumers from corrupted outputs.

Examples:

* Spread ≥ 0
* MicroPrice within Bid / Ask
* No NaN values
* No Infinity
* Volatility ≥ 0
* Queue imbalance within valid range

Validation failures generate metrics and logs while allowing the engine to continue whenever recovery is possible.

---

# 11. Feature Publisher

The Feature Publisher is responsible for distributing completed FeatureVectors.

The hot path performs only:

1. Publish to Shared Memory.
2. Push FeatureVector to an asynchronous persistence queue.

Disk writes never occur on the compute thread.

Persistence is handled entirely by a dedicated background worker.

---

# 12. Event Lifecycle

Every MarketEvent follows one deterministic execution path.

```
Receive MarketEvent

↓

Scheduler

↓

State Manager Update

↓

Context Builder

↓

Pass 1 Features

↓

Pass 2 Features

↓

Pass 3 Features

↓

Pass 4 Features

↓

Feature Validation

↓

Feature Publisher

↓

Shared Memory

↓

Asynchronous Feature Store
```

This execution order never changes.

---

# 13. Execution Passes

Features execute in deterministic stages.

## Pass 1 – Price Features

* Spread
* MidPrice
* MicroPrice

## Pass 2 – Order Book Features

* Queue Imbalance
* Market Depth
* Liquidity

## Pass 3 – Flow Features

* Signed Volume
* Order Flow Imbalance
* Trade Intensity

## Pass 4 – Statistical Features

* Returns
* VWAP
* Rolling Variance
* Realized Volatility

A feature may depend only on outputs from previous passes.

Circular dependencies are forbidden.

Dependency validation is performed during engine startup.

---

# 14. Plugin Lifecycle

Every plugin follows the same lifecycle.

```
Construction

↓

Initialize()

↓

Register()

↓

Validate()

↓

Execute()

↓

Shutdown()
```

Initialization allocates any required state.

Execution performs only mathematical computation.

Shutdown releases resources.

---

# 15. Worker Shutdown Protocol

Fatal errors must terminate workers safely while preserving observability.

Examples of fatal failures include:

* Memory corruption
* Queue corruption
* Invalid SymbolState
* Internal consistency violations

Worker shutdown sequence:

```
Fatal Error

↓

Reject New Events

↓

Drain Processing Queue

↓

Flush Feature Store Queue

↓

Persist Crash Metadata

↓

Flush Metrics

↓

Notify Orchestrator

↓

Graceful Worker Shutdown
```

The orchestrator may then decide whether to restart the worker or terminate the entire engine.

This protocol guarantees maximum recovery while preserving debugging information.

---

# 16. Concurrency Model

Each worker owns:

* One Scheduler
* One SPSC Queue
* One State Manager
* One Feature Engine

Symbols are permanently assigned to workers.

Symbols never migrate during execution.

Benefits:

* Deterministic ordering
* Excellent cache locality
* No inter-worker synchronization
* Linear scalability

---

# 17. Versioning

Every feature is versioned independently.

FeatureDescriptor includes:

* Feature Version
* Schema Version
* Introduced Version
* Deprecated Version

This guarantees reproducible research.

Historical feature files always retain the exact computation version used to generate them.

---

# 18. Future Extensions

The architecture intentionally supports future additions without modification to the runtime.

Planned extensions include:

* Additional feature plugins
* Signal generation engine
* Risk metrics
* Alpha research
* Machine learning feature export
* Backtesting integration

These modules consume FeatureVectors without modifying the execution pipeline.

---

# 19. Summary

The Feature Engine separates scheduling, mutable state management, context construction, feature computation, validation, and publication into independent components.

This architecture provides:

* Deterministic execution
* Replay parity
* Zero-allocation processing
* Lock-free concurrency
* Cache-efficient execution
* Stable binary contracts
* Side-effect-free feature computation
* Extensible plugin architecture
* Research reproducibility

The Feature Engine forms the computational foundation of MarketPulse v2 and establishes the platform for future quantitative research, signal generation, and execution analytics.
