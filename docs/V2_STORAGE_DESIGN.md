# RFC-005: Storage Design

**Project:** MarketPulse v2 – Market Intelligence Engine (MPIE)

**Status:** Draft v1.0

**Author:** Muskan Srivastav

---

# 1. Purpose

The Storage Layer is responsible for persisting computed FeatureVectors while ensuring that storage operations never interfere with real-time feature computation.

Unlike the recorder in MarketPulse v1, which stores raw market events, the Storage Layer stores derived quantitative features.

Its primary objectives are:

* Preserve deterministic feature history
* Enable zero-copy research workflows
* Support replay and future backtesting
* Isolate disk I/O from the compute pipeline

The Storage Layer is not part of the execution hot path.

---

# 2. Design Goals

The storage subsystem is designed around five principles.

## Non-Blocking

Feature computation must never wait for disk operations.

Persistence is completely asynchronous.

---

## Deterministic

Identical FeatureVectors must always produce identical storage output.

File layout must be independent of replay speed.

---

## Binary Efficient

Storage favors fixed-width binary layouts.

No JSON or text serialization is performed in the execution path.

---

## Research Friendly

Historical feature files should be readable directly by Python, NumPy, or memory-mapped applications without conversion.

---

## Version Aware

Every persisted file contains schema information allowing historical datasets to remain compatible with future engine versions.

---

# 3. High-Level Architecture

```text
Feature Engine
      │
      ▼
Feature Publisher
      │
 ┌────┴───────────────┐
 ▼                    ▼
Shared Memory   Storage Queue
                     │
                     ▼
          Storage Worker Thread
                     │
                     ▼
        Market Feature Format (MFF)
```

Only the Storage Worker performs disk I/O.

The Feature Engine never writes directly to disk.

---

# 4. Storage Pipeline

Every FeatureVector follows the same lifecycle.

```text
FeatureVector

↓

Feature Publisher

↓

Lock-Free SPSC Queue

↓

Storage Worker

↓

MFF Writer

↓

Persistent Storage
```

The Storage Queue acts as the isolation boundary between computation and persistence.

---

# 5. Market Feature Format (MFF)

FeatureVectors are stored using the Market Feature Format (MFF).

MFF is a binary append-only file format designed specifically for quantitative feature storage.

Each file consists of:

```text
MFF Header

↓

Feature Records

↓

Optional Footer
```

The layout is intentionally simple to maximize sequential write performance.

---

# 6. MFF Header

Every file begins with a fixed-size header.

Example:

```cpp
struct MFFHeader
{
    char magic[4];              // "MFF1"

    uint32_t schema_version;

    uint32_t engine_version;

    uint32_t feature_count;

    uint64_t creation_timestamp;

    uint64_t reserved;
};
```

The header provides sufficient metadata for future compatibility.

---

# 7. Feature Records

Feature records are stored sequentially.

Each record corresponds to one FeatureVector.

Example:

```cpp
struct FeatureRecord
{
    FeatureVector feature;
};
```

Records are fixed width.

No variable-length fields are permitted.

This guarantees:

* Constant-time seeking
* Efficient memory mapping
* Excellent cache behavior

---

# 8. Append-Only Storage

Feature files are append-only.

Records are never modified after being written.

Benefits include:

* Crash resilience
* Sequential disk writes
* High throughput
* Simplified recovery

Updates always generate new records.

Existing records remain immutable.

---

# 9. Shared Memory

Live consumers should never read feature files directly.

Instead:

```text
Feature Publisher

↓

Shared Memory

↓

Strategies

↓

Dashboards

↓

Monitoring
```

Shared Memory represents the real-time interface.

MFF represents the historical interface.

These two responsibilities remain completely independent.

---

# 10. Storage Queue

Persistence is decoupled through a dedicated lock-free SPSC queue.

Responsibilities:

* Buffer outgoing FeatureVectors
* Absorb temporary disk latency
* Protect compute threads from blocking

If the storage worker becomes temporarily slower than the compute pipeline, the queue absorbs the burst.

Queue overflow is treated as an operational metric rather than a compute failure.

---

# 11. Storage Worker

The Storage Worker owns:

* File handles
* Buffer flushing
* File rotation
* Error reporting

It never performs feature computation.

Likewise, the Feature Engine never performs persistence.

This strict separation keeps the execution pipeline deterministic.

---

# 12. Versioning

Every MFF file stores:

* Engine Version
* Schema Version
* Creation Timestamp

Schema changes require:

* Header version increment
* FeatureVector schema increment

Historical files remain readable because the storage format is self-describing.

---

# 13. Binary Compatibility

FeatureRecord layout must remain stable within a schema version.

Changing:

* field ordering
* alignment
* field size
* field type

requires a new schema version.

No compatibility is guaranteed across schema revisions without explicit migration.

---

# 14. Failure Handling

Storage failures must never terminate feature computation.

Recoverable failures:

* Temporary disk saturation
* Flush timeout
* Delayed writes

Fatal failures:

* File corruption
* Header corruption
* Persistent write failure

Recoverable failures are logged and exposed through metrics.

Fatal failures trigger the Worker Shutdown Protocol defined in RFC-002.

---

# 15. Future Extensions

The storage architecture intentionally supports future capabilities.

Planned extensions include:

* Compression
* Partitioned feature files
* Apache Parquet export
* Apache Arrow export
* ClickHouse ingestion
* Remote object storage
* Feature replay
* Backtesting datasets

These extensions should be implemented without modifying the Feature Engine.

---

# 16. Design Trade-offs

| Decision          | Chosen Approach          | Rationale                                         |
| ----------------- | ------------------------ | ------------------------------------------------- |
| Storage Format    | Binary append-only (MFF) | Maximum write throughput and deterministic layout |
| Persistence       | Asynchronous worker      | Prevents disk I/O from blocking computation       |
| Live Access       | Shared Memory            | Zero-copy, low-latency consumption                |
| Historical Access | MFF files                | Efficient replay and research workflows           |
| File Structure    | Fixed-width records      | Enables memory mapping and constant-time seeks    |
| Serialization     | Raw binary               | Eliminates parsing overhead                       |

---

# 17. Summary

The Storage Layer is designed as an asynchronous persistence pipeline rather than a traditional file writer.

By separating live publication from historical persistence, the MarketPulse Intelligence Engine maintains deterministic execution while producing research-grade feature datasets.

The Market Feature Format (MFF) provides a stable binary representation of computed FeatureVectors and serves as the foundation for future replay, backtesting, analytics, and quantitative research.
