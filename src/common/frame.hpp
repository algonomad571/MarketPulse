#pragma once

#include <cstdint>
#include <span>
#include <variant>
#include <memory>
#include <cstring>

namespace md {

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic = 0x4D444146;  // 'M','D','A','F'
    uint16_t version = 1;
    uint16_t msg_type;  // 1=L1, 2=L2, 3=Trade, 4=Heartbeat, 5=ControlAck
    uint32_t body_len;  // bytes of body
    uint32_t crc32;     // CRC of body
};

struct L1Body {
    uint64_t ts_ns;
    uint32_t symbol_id;
    int64_t  bid_px;    // scaled 1e-8
    uint64_t bid_sz;    // scaled 1e-8
    int64_t  ask_px;    // scaled 1e-8
    uint64_t ask_sz;    // scaled 1e-8
    uint64_t seq;
};

enum class BookAction : uint8_t { Insert = 0, Update = 1, Delete = 2 };
enum class Side : uint8_t { Bid = 0, Ask = 1 };

struct L2Body {
    uint64_t ts_ns;
    uint32_t symbol_id;
    uint8_t  side;      // Side
    uint8_t  action;    // BookAction
    uint16_t level;     // 0=best
    int64_t  price;     // 1e-8
    uint64_t size;      // 1e-8, 0 valid for delete
    uint64_t seq;
};

struct TradeBody {
    uint64_t ts_ns;
    uint32_t symbol_id;
    int64_t  price;     // 1e-8
    uint64_t size;      // 1e-8
    uint8_t  aggressor_side; // 0=Buy,1=Sell,255=Unknown
    uint64_t seq;
};

struct HbBody { 
    uint64_t ts_ns; 
};

struct ControlAckBody {
    uint32_t ack_code;  // 200=OK, 400=BadRequest, etc.
    uint32_t reserved;
};
#pragma pack(pop)

// Message types
enum class MessageType : uint16_t {
    L1 = 1,
    L2 = 2,
    Trade = 3,
    Heartbeat = 4,
    ControlAck = 5
};

// Frame variant type
using FrameBody = std::variant<L1Body, L2Body, TradeBody, HbBody, ControlAckBody>;

// Complete frame
struct Frame {
    FrameHeader header;
    FrameBody body;
    
    Frame() = default;
    explicit Frame(const L1Body& l1);
    explicit Frame(const L2Body& l2);
    explicit Frame(const TradeBody& trade);
    explicit Frame(const HbBody& hb);
    explicit Frame(const ControlAckBody& ack);
};

// Serialization helpers
std::span<const std::byte> encode_frame(const Frame& frame, std::vector<std::byte>& buffer);
std::optional<Frame> decode_frame(std::span<const std::byte> data);

// File format headers
#pragma pack(push, 1)
struct MdfHeader {
    uint32_t magic = 0x4D444649;  // 'M','D','F','I'
    uint16_t version = 1;
    uint16_t reserved = 0;
    uint64_t start_ts_ns;
    uint64_t end_ts_ns;      // updated on roll
    uint32_t symbol_count;   // snapshot at roll
    uint32_t frame_count;    // updated on roll
};

struct IndexEntry {
    uint64_t ts_ns_first;
    uint64_t file_offset;    // byte offset into .mdf file
};
#pragma pack(pop)

} // namespace md