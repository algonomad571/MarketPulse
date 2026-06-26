# RFC-004: Data Contracts

**Project:** MarketPulse v2 – Market Intelligence Engine (MPIE)

**Status:** Draft v1.0

**Author:** Muskan Srivastav

---

# 1. Purpose

This document defines the immutable data contracts used throughout the MarketPulse platform.

A data contract specifies:

* Structure
* Ownership
* Lifetime
* Mutability
* Binary layout
* Versioning

The contracts described here form the communication interface between every subsystem of MarketPulse.

Once released, changes to these contracts must be treated as schema changes.

---

# 2. Design Principles

Every contract follows the same architectural rules.

## Deterministic

Objects must always represent the same information regardless of replay speed or execution environment.

---

## POD Layout

All runtime contracts are:

* Standard Layout
* Trivially Copyable
* POD-compatible

No contract contains:

* pointers
* virtual methods
* dynamic containers
* heap allocations

---

## Immutable by Default

Objects flow through the pipeline.

They are never modified after publication.

Only the owner may mutate its object before publishing.

---

## Stable Binary ABI

Every runtime contract has a deterministic binary layout.

This enables:

* Zero-copy shared memory
* Binary persistence
* Memory mapping
* Replay compatibility

Changing a runtime contract requires a schema version increment.

---

# 3. Contract Overview

The runtime uses four core contracts.

```text
MarketEvent
      │
      ▼
FeatureContext
      │
      ▼
FeatureVector

FeatureDescriptor
(metadata only)
```

Each contract has one responsibility.

---

# 4. MarketEvent

## Purpose

Represents one normalized market update.

Produced by:

Infrastructure Layer (MarketPulse v1)

Consumed by:

MarketPulse Intelligence Engine

---

## Ownership

Owner:

Infrastructure Layer

Consumers:

Scheduler

State Manager

Replay Engine

Feature Engine

MarketEvent is immutable after publication.

---

## Structure

```cpp
struct MarketEvent
{
    uint64_t timestamp;

    uint32_t symbol_id;

    EventType event_type;

    double bid_price;

    double ask_price;

    uint64_t bid_size;

    uint64_t ask_size;

    double trade_price;

    uint64_t trade_size;
};
```

---

## Requirements

✓ Standard Layout

✓ Trivially Copyable

✓ Fixed Size

✓ Heap Free

---

# 5. FeatureContext

## Purpose

FeatureContext provides a read-only computational view over the current market state.

It is created by the Context Builder.

It exists only during feature execution.

Unlike MarketEvent, FeatureContext is never persisted.

---

## Ownership

Owner:

Context Builder

Consumers:

Feature Executors

Destroyed after processing completes.

---

## Structure

```cpp
struct FeatureContext
{
    const MarketEvent& current_event;

    const MarketEvent& previous_event;

    const SymbolState& state;

    uint32_t symbol_id;

    uint64_t market_timestamp;

    ExecutionMetadata metadata;

    std::span<const double> price_history;

    std::span<const uint64_t> volume_history;
};
```

---

## Lifetime

FeatureContext is stack allocated.

It is valid only during execution of one FeatureVector.

Plugins must never store references beyond the execution scope.

---

# 6. FeatureVector

## Purpose

FeatureVector represents the final output of the Feature Engine.

It is consumed by:

* Shared Memory
* Feature Store
* Future Signal Engine
* Research Environment

FeatureVector is the primary binary interface of MarketPulse v2.

---

## Ownership

Owner:

Feature Engine

Consumers:

Feature Publisher

Feature Store

Research API

Signal Engine (future)

---

## Binary Contract

FeatureVector is:

* Fixed Size
* Trivially Copyable
* Standard Layout
* Heap Free

Its layout must remain stable across all runtime components.

Any structural modification increments the schema version.

---

## Structure

```cpp
struct FeatureVector
{
    uint64_t timestamp;

    uint32_t symbol_id;

    uint32_t schema_version;

    float spread;

    float mid_price;

    float micro_price;

    float queue_imbalance;

    float order_flow_imbalance;

    float signed_volume;

    float returns;

    float realized_volatility;
};
```

The layout is intentionally flat.

Nested structures are avoided to improve binary compatibility and cache locality.

---

## Invariants

FeatureVector must satisfy:

Spread ≥ 0

Finite floating-point values

No NaN

No Infinity

MicroPrice within Bid / Ask

Queue Imbalance ∈ [-1,1]

Realized Volatility ≥ 0

---

# 7. FeatureDescriptor

## Purpose

FeatureDescriptor provides metadata describing a feature plugin.

Unlike FeatureVector, it is not part of the hot path.

It exists to support:

* Plugin discovery
* Validation
* Documentation
* Dependency analysis

---

## Ownership

Owner:

Feature Registry

Consumers:

Scheduler

Validation Engine

Documentation

Future Plugin Loader

---

## Structure

```cpp
struct FeatureDescriptor
{
    std::string_view name;

    uint32_t feature_version;

    uint32_t schema_version;

    FeatureCategory category;

    ExecutionPass pass;

    bool stateful;

    uint32_t lookback_window;

    Complexity complexity;

    std::span<const FeatureId> dependencies;
};
```

FeatureDescriptor is created during startup.

It remains constant during execution.

---

# 8. ExecutionMetadata

ExecutionMetadata provides runtime information to the FeatureContext.

It is not persisted.

```cpp
struct ExecutionMetadata
{
    bool replay_mode;

    uint64_t replay_sequence;

    uint32_t worker_id;

    uint32_t engine_version;
};
```

This metadata allows identical feature code to execute in both live and replay modes.

---

# 9. Ownership Rules

Each contract has exactly one owner.

| Contract          | Owner                | Mutable                  |
| ----------------- | -------------------- | ------------------------ |
| MarketEvent       | Infrastructure Layer | Before publication only  |
| FeatureContext    | Context Builder      | During construction only |
| FeatureVector     | Feature Engine       | Before publication only  |
| FeatureDescriptor | Feature Registry     | Startup only             |

After publication, every contract becomes immutable.

---

# 10. Versioning

Two independent version numbers exist.

## Engine Version

Represents the overall MPIE release.

Example:

2.0.0

---

## Schema Version

Represents binary compatibility.

Changing:

* field order
* field type
* field size
* alignment

requires a schema version increment.

This guarantees replay compatibility across releases.

---

# 11. Serialization Rules

Runtime contracts are serialized as raw binary.

No JSON serialization occurs in the execution path.

Persistent storage always preserves:

* field ordering
* alignment
* schema version
* timestamp precision

This guarantees deterministic replay.

---

# 12. Validation Rules

Before publication, runtime contracts must satisfy:

No uninitialized fields

No invalid timestamps

No NaN

No Infinity

Correct schema version

Valid symbol identifier

Validation failures are reported through metrics.

Corrupted contracts are never published.

---

# 13. Future Extensions

Future versions may introduce:

SignalSnapshot

RiskSnapshot

PortfolioSnapshot

AlphaSnapshot

These will be additional contracts.

Existing runtime contracts should remain unchanged.

---

# 14. Summary

The MarketPulse platform is built around a small set of stable, immutable runtime contracts.

These contracts separate infrastructure from computation while enabling zero-copy communication, deterministic replay, binary compatibility, and future extensibility.

By freezing these contracts early, future versions of MarketPulse can evolve without redesigning the interfaces between subsystems.
