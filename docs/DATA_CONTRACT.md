# MarketPulse Canonical Data Contract

## Overview

The **`Frame`** structure (defined in `src/common/frame.hpp`) is the single canonical data contract used across all MarketPulse modules for market data representation, transmission, and persistence.

## Design Principles

1. **Real-Time Performance**: Fixed-size packed structs (no heap allocations)
2. **Type Safety**: `std::variant` for compile-time message type checking
3. **Wire Efficiency**: Binary protocol with CRC32 validation
4. **Stability**: Versioned protocol (v1) with magic number validation
5. **Extensibility**: New message types can be added without breaking existing code

## Structure Definition

### Frame Container

```cpp
namespace md {

struct Frame {
    FrameHeader header;           // 16 bytes
    FrameBody body;              // variant: L1Body | L2Body | TradeBody | HbBody | ControlAckBody
};

using FrameBody = std::variant<L1Body, L2Body, TradeBody, HbBody, ControlAckBody>;

enum class MessageType : uint16_t {
    L1 = 1,          // Level 1 quote (BBO)
    L2 = 2,          // Level 2 order book update
    Trade = 3,       // Trade execution
    Heartbeat = 4,   // Keep-alive
    ControlAck = 5   // Control message acknowledgment
};

}
```

### Frame Header (16 bytes)

```cpp
#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;      // 0x4D444146 ('M','D','A','F')
    uint16_t version;    // Protocol version (currently 1)
    uint16_t msg_type;   // MessageType enum value
    uint32_t body_len;   // Size of body in bytes
    uint32_t crc32;      // CRC32 checksum of body
};
#pragma pack(pop)
```

**Validation:**
- Magic number prevents random data interpretation
- Version enables future protocol evolution
- CRC32 detects corruption in transmission/storage

### Message Bodies

#### L1Body - Best Bid/Offer (48 bytes)

```cpp
struct L1Body {
    uint64_t ts_ns;       // Nanosecond timestamp (UTC epoch)
    uint32_t symbol_id;   // Symbol ID from SymbolRegistry
    int64_t  bid_px;      // Scaled by 1e8 (e.g., $123.45 → 12345000000)
    uint64_t bid_sz;      // Scaled by 1e8 (e.g., 100.0 shares → 10000000000)
    int64_t  ask_px;      // Scaled by 1e8
    uint64_t ask_sz;      // Scaled by 1e8
    uint64_t seq;         // Monotonic sequence number (gap detection)
};
```

**Use Case:** Top-of-book quotes (BBO) for equities, FX, crypto

#### L2Body - Order Book Depth (40 bytes)

```cpp
enum class BookAction : uint8_t { Insert = 0, Update = 1, Delete = 2 };
enum class Side : uint8_t { Bid = 0, Ask = 1 };

struct L2Body {
    uint64_t ts_ns;       // Nanosecond timestamp
    uint32_t symbol_id;   // Symbol ID
    uint8_t  side;        // Side enum
    uint8_t  action;      // BookAction enum
    uint16_t level;       // Price level (0 = best, 1 = second best, etc.)
    int64_t  price;       // Scaled by 1e8
    uint64_t size;        // Scaled by 1e8 (0 valid for Delete)
    uint64_t seq;         // Sequence number
};
```

**Use Case:** Full order book reconstruction (L2 market depth)

#### TradeBody - Executions (40 bytes)

```cpp
struct TradeBody {
    uint64_t ts_ns;            // Nanosecond timestamp
    uint32_t symbol_id;        // Symbol ID
    int64_t  price;            // Scaled by 1e8
    uint64_t size;             // Scaled by 1e8
    uint8_t  aggressor_side;   // 0=Buy, 1=Sell, 255=Unknown
    uint64_t seq;              // Sequence number
};
```

**Use Case:** Trade tape, VWAP calculations, volume analysis

#### HbBody - Heartbeat (8 bytes)

```cpp
struct HbBody { 
    uint64_t ts_ns;  // Timestamp
};
```

**Use Case:** Connection liveness detection (30-second intervals)

#### ControlAckBody - Control Acknowledgment (8 bytes)

```cpp
struct ControlAckBody {
    uint32_t ack_code;  // 200=OK, 400=BadRequest, 404=NotFound, etc.
    uint32_t reserved;  // Future use
};
```

**Use Case:** Subscribe/unsubscribe confirmation from Publisher

## Data Scaling Convention

**All prices and sizes use 1e8 (100,000,000) scaling:**

| Raw Value      | Encoded Value | Decoding                     |
|----------------|---------------|------------------------------|
| $123.45        | 12345000000   | 12345000000 / 1e8 = $123.45 |
| 100.0 shares   | 10000000000   | 10000000000 / 1e8 = 100.0   |
| $0.00000001    | 1             | 1 / 1e8 = $0.00000001       |

**Advantages:**
- Exact decimal representation (no floating-point rounding)
- Supports 8 decimal places (sufficient for crypto, FX, micro-pennies)
- Fast integer arithmetic in hot paths

## Serialization API

### Encoding

```cpp
// Convert Frame to binary format
std::vector<std::byte> buffer;
std::span<const std::byte> wire_data = encode_frame(frame, buffer);

// Send over network or write to disk
socket.send(wire_data);
```

**Operation:**
1. Serialize body to buffer
2. Calculate CRC32 over body
3. Write header with CRC32
4. Return span over complete frame

### Decoding

```cpp
// Parse binary data into Frame
std::optional<Frame> frame = decode_frame(data_span);

if (frame) {
    std::visit([](const auto& body) {
        // Process specific body type
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, L1Body>) {
            // Handle L1
        } else if constexpr (std::is_same_v<T, TradeBody>) {
            // Handle Trade
        }
        // ...
    }, frame->body);
}
```

**Validation:**
1. Magic number check (0x4D444146)
2. Version check (v1)
3. Buffer size validation
4. CRC32 verification
5. Message type validation

Returns `std::nullopt` on any validation failure.

## Module Data Flow

### Producers

| Module          | Creates Frame From            | Location                      |
|-----------------|-------------------------------|-------------------------------|
| **Normalizer**  | `RawEvent` (feed adapter)     | `src/normalize/normalizer.cpp` Line 104-128 |
| **Replayer**    | Disk (MDF files)              | `src/replay/replayer.cpp` Line 186-196 |

### Consumers

| Module          | Consumes Frame For            | Location                      |
|-----------------|-------------------------------|-------------------------------|
| **Recorder**    | Binary persistence (MDF)      | `src/recorder/recorder.cpp` Line 89-115 |
| **Publisher**   | TCP transmission to clients   | `src/publisher/pub_server.cpp` Line 80-104 |
| **Publisher**   | Topic routing/filtering       | `src/main_core.cpp` Line 185-228 |

### Feed Adapter (Not Canonical Contract)

**`RawEvent`** (defined in `src/feed/mock_feed.hpp`) is **NOT** part of the canonical contract:
- **Purpose:** Feed-specific adapter for external data sources
- **Location:** `feed/` module only
- **Characteristics:** Uses `std::string` symbols and `double` prices (external representation)
- **Lifecycle:** Created by feeds → immediately converted to `Frame` by Normalizer → discarded

**Rationale:** Each feed provider has different data formats (FIX, ITCH, proprietary). `RawEvent` is the abstraction layer that normalizes these into the canonical `Frame` format.

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         FEED LAYER                              │
│  ┌──────────┐                                                   │
│  │ MockFeed │ → RawEvent → Normalizer → Frame                   │
│  └──────────┘              (converts string→ID,                 │
│  (External)                 double→scaled int)                  │
└──────────────────────────────┬──────────────────────────────────┘
                               │ CANONICAL FRAME
                               │
                ┌──────────────┼──────────────┐
                │              │              │
                ▼              ▼              ▼
        ┌───────────┐  ┌──────────────┐  ┌──────────┐
        │ Recorder  │  │  Publisher   │  │ Metrics  │
        │           │  │              │  │          │
        │ Disk      │  │ TCP Clients  │  │ Counters │
        │ (MDF)     │  │              │  │          │
        └─────┬─────┘  └──────────────┘  └──────────┘
              │
              │ Frame (from disk)
              │
        ┌─────▼─────┐
        │ Replayer  │ → Frame → Publisher
        │           │
        └───────────┘
```

## Topic Naming Convention

When routing Frames through Publisher, topics are derived from the Frame content:

```cpp
// In distribution_thread (main_core.cpp)
std::string topic = std::visit([](auto& body) -> std::string {
    using T = std::decay_t<decltype(body)>;
    
    std::string symbol = registry->get_symbol(body.symbol_id);
    
    if constexpr (std::is_same_v<T, L1Body>) {
        return symbol + ".L1";
    } else if constexpr (std::is_same_v<T, L2Body>) {
        return symbol + ".L2";
    } else if constexpr (std::is_same_v<T, TradeBody>) {
        return symbol + ".TRADE";
    }
    return symbol + ".HB";
}, frame.body);
```

**Examples:**
- `AAPL.L1` - Apple Level 1 quotes
- `MSFT.L2` - Microsoft order book updates
- `TSLA.TRADE` - Tesla trades
- `*.L1` - All Level 1 quotes (wildcard)

## Extension Guidelines

### Adding New Message Types

1. **Define body struct** in `frame.hpp`:
```cpp
struct NewEventBody {
    uint64_t ts_ns;
    uint32_t symbol_id;
    // ... additional fields
};
```

2. **Add to variant**:
```cpp
using FrameBody = std::variant<L1Body, L2Body, TradeBody, HbBody, ControlAckBody, NewEventBody>;
```

3. **Add MessageType enum value**:
```cpp
enum class MessageType : uint16_t {
    // ... existing
    NewEvent = 6
};
```

4. **Add constructor** in `frame.cpp`:
```cpp
Frame::Frame(const NewEventBody& evt) : body(evt) {
    header.msg_type = static_cast<uint16_t>(MessageType::NewEvent);
    header.body_len = sizeof(NewEventBody);
}
```

5. **Update decode_frame** in `frame.cpp`:
```cpp
case MessageType::NewEvent: {
    if (header.body_len != sizeof(NewEventBody)) return std::nullopt;
    NewEventBody body;
    std::memcpy(&body, body_ptr, sizeof(NewEventBody));
    frame.body = body;
    break;
}
```

### Design Constraints

- **Fixed Size:** All body structs must be fixed-size (no variable-length fields)
- **Pack Directive:** Always use `#pragma pack(push, 1)` for maximum density
- **Alignment:** Prefer natural alignment within structs for performance
- **No Pointers:** Structs cannot contain pointers (not serializable)
- **Integers Only:** Use scaled integers instead of floating-point

## Performance Characteristics

| Operation            | Time Complexity | Notes                           |
|----------------------|-----------------|---------------------------------|
| Frame construction   | O(1)            | Stack-only, no allocations      |
| Encode to binary     | O(1)            | Memcpy + CRC32 lookup table     |
| Decode from binary   | O(1)            | Memcpy + CRC32 validation       |
| Type dispatch        | O(1)            | std::variant compile-time       |
| Symbol lookup        | O(1)            | Atomic lookup in SymbolRegistry |

**Throughput (measured on single thread):**
- Encode: ~10M frames/sec
- Decode: ~8M frames/sec
- CRC32 calculation: ~20 GB/sec

## Thread Safety

- **Frame objects:** Not thread-safe (value type, pass by copy or move)
- **encode_frame:** Thread-safe (reentrant, uses thread-local buffer)
- **decode_frame:** Thread-safe (read-only operation)
- **SymbolRegistry:** Thread-safe (atomic operations)

**Recommendation:** Each thread should have its own Frame instances. Use lock-free queues (`moodycamel::ConcurrentQueue<Frame>`) for inter-thread communication.

## Validation Checklist

When working with Frame:

- [ ] Timestamps are in nanoseconds since Unix epoch (UTC)
- [ ] Prices/sizes are scaled by 1e8 before encoding
- [ ] Symbol IDs come from SymbolRegistry (not raw integers)
- [ ] Sequence numbers are monotonic per symbol
- [ ] CRC32 is calculated after body is serialized
- [ ] Magic number is 0x4D444146
- [ ] Version is set to 1
- [ ] Body length matches sizeof(BodyType)

## References

- **Implementation:** [src/common/frame.hpp](../src/common/frame.hpp), [src/common/frame.cpp](../src/common/frame.cpp)
- **Usage Examples:** [src/normalize/normalizer.cpp](../src/normalize/normalizer.cpp#L104-128), [src/publisher/pub_server.cpp](../src/publisher/pub_server.cpp#L80-104)
- **Binary Format:** [docs/END_TO_END_FLOW.md](END_TO_END_FLOW.md#binary-protocol)
- **File Persistence:** [docs/BUILD.md](BUILD.md) (MDF Format section)
