#pragma once
#include <cstdint>
#include <array>
#include <type_traits>

namespace md::mpie {

// Statically defined indices for our flat feature array.
// This guarantees O(1) data access via simple array offsets.
enum FeatureIndex : size_t {
    IDX_SPREAD = 0,
    IDX_MID_PRICE = 1,
    IDX_MICRO_PRICE = 2,
    IDX_LOG_RETURN = 3,
    IDX_WAP = 4,
    IDX_BOOK_IMBALANCE = 5,
    IDX_ORDER_FLOW_IMBALANCE = 6,
    IDX_MULTILEVEL_IMBALANCE_L5 = 7,
    IDX_BOOK_PRESSURE_RATIO = 8,
    IDX_TRADE_FLOW_IMBALANCE = 9,
    IDX_EFFECTIVE_SPREAD = 10,
    TOTAL_METRICS = 11
};

struct alignas(64) FeatureVector {
    uint64_t engine_timestamp{0};
    uint32_t symbol_id{0};
    uint32_t version{1};
    bool is_valid{true};
    
    // Contiguous metrics storage block
    std::array<double, TOTAL_METRICS> metrics{};
};

static_assert(std::is_standard_layout_v<FeatureVector>, "FeatureVector must maintain standard layout!");
static_assert(std::is_trivially_copyable_v<FeatureVector>, "FeatureVector must be trivially copyable for zero-copy IPC mapping!");

} // namespace md::mpie
