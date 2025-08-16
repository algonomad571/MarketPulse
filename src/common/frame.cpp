#include "frame.hpp"
#include "crc32.hpp"
#include <cstring>

namespace md {

Frame::Frame(const L1Body& l1) : body(l1) {
    header.msg_type = static_cast<uint16_t>(MessageType::L1);
    header.body_len = sizeof(L1Body);
}

Frame::Frame(const L2Body& l2) : body(l2) {
    header.msg_type = static_cast<uint16_t>(MessageType::L2);
    header.body_len = sizeof(L2Body);
}

Frame::Frame(const TradeBody& trade) : body(trade) {
    header.msg_type = static_cast<uint16_t>(MessageType::Trade);
    header.body_len = sizeof(TradeBody);
}

Frame::Frame(const HbBody& hb) : body(hb) {
    header.msg_type = static_cast<uint16_t>(MessageType::Heartbeat);
    header.body_len = sizeof(HbBody);
}

Frame::Frame(const ControlAckBody& ack) : body(ack) {
    header.msg_type = static_cast<uint16_t>(MessageType::ControlAck);
    header.body_len = sizeof(ControlAckBody);
}

std::span<const std::byte> encode_frame(const Frame& frame, std::vector<std::byte>& buffer) {
    const size_t total_size = sizeof(FrameHeader) + frame.header.body_len;
    buffer.resize(total_size);
    
    // Copy header (will update CRC after body)
    FrameHeader header = frame.header;
    
    // Serialize body based on type
    std::byte* body_ptr = buffer.data() + sizeof(FrameHeader);
    std::visit([body_ptr](const auto& body) {
        std::memcpy(body_ptr, &body, sizeof(body));
    }, frame.body);
    
    // Calculate CRC32 of body
    header.crc32 = calculate_crc32(body_ptr, frame.header.body_len);
    
    // Copy final header with correct CRC
    std::memcpy(buffer.data(), &header, sizeof(FrameHeader));
    
    return std::span<const std::byte>(buffer.data(), total_size);
}

std::optional<Frame> decode_frame(std::span<const std::byte> data) {
    if (data.size() < sizeof(FrameHeader)) {
        return std::nullopt;
    }
    
    FrameHeader header;
    std::memcpy(&header, data.data(), sizeof(FrameHeader));
    
    // Validate magic and size
    if (header.magic != 0x4D444146 || header.version != 1) {
        return std::nullopt;
    }
    
    if (data.size() < sizeof(FrameHeader) + header.body_len) {
        return std::nullopt;
    }
    
    const std::byte* body_ptr = data.data() + sizeof(FrameHeader);
    
    // Verify CRC32
    uint32_t calculated_crc = calculate_crc32(body_ptr, header.body_len);
    if (calculated_crc != header.crc32) {
        return std::nullopt;
    }
    
    Frame frame;
    frame.header = header;
    
    // Decode body based on message type
    switch (static_cast<MessageType>(header.msg_type)) {
        case MessageType::L1: {
            if (header.body_len != sizeof(L1Body)) return std::nullopt;
            L1Body body;
            std::memcpy(&body, body_ptr, sizeof(L1Body));
            frame.body = body;
            break;
        }
        case MessageType::L2: {
            if (header.body_len != sizeof(L2Body)) return std::nullopt;
            L2Body body;
            std::memcpy(&body, body_ptr, sizeof(L2Body));
            frame.body = body;
            break;
        }
        case MessageType::Trade: {
            if (header.body_len != sizeof(TradeBody)) return std::nullopt;
            TradeBody body;
            std::memcpy(&body, body_ptr, sizeof(TradeBody));
            frame.body = body;
            break;
        }
        case MessageType::Heartbeat: {
            if (header.body_len != sizeof(HbBody)) return std::nullopt;
            HbBody body;
            std::memcpy(&body, body_ptr, sizeof(HbBody));
            frame.body = body;
            break;
        }
        case MessageType::ControlAck: {
            if (header.body_len != sizeof(ControlAckBody)) return std::nullopt;
            ControlAckBody body;
            std::memcpy(&body, body_ptr, sizeof(ControlAckBody));
            frame.body = body;
            break;
        }
        default:
            return std::nullopt;
    }
    
    return frame;
}

} // namespace md