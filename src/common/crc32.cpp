#include "crc32.hpp"
#include <array>

namespace md {

namespace {
std::array<uint32_t, 256> crc32_table;
bool table_initialized = false;

void init_table() {
    const uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
    table_initialized = true;
}
}

void initialize_crc32_table() {
    if (!table_initialized) {
        init_table();
    }
}

uint32_t calculate_crc32(const std::byte* data, size_t length) {
    if (!table_initialized) {
        init_table();
    }
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = static_cast<uint8_t>(data[i]);
        crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ byte];
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace md