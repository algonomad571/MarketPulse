#pragma once

#include <cstdint>
#include <cstddef>

namespace md {

uint32_t calculate_crc32(const std::byte* data, size_t length);
void initialize_crc32_table();

} // namespace md