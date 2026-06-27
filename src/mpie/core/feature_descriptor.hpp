#pragma once

#include <cstdint>
#include <string_view>
#include <span>

namespace md::mpie {

enum class FeatureCategory : uint8_t {
    Price = 1,
    OrderBook = 2,
    OrderFlow = 3,
    Statistical = 4,
    Advanced = 5
};

enum class ExecutionPass : uint8_t {
    Pass1 = 1,
    Pass2 = 2,
    Pass3 = 3,
    Pass4 = 4
};

enum class Complexity : uint8_t {
    O1 = 1,
    O_N = 2
};

using FeatureId = uint32_t;

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

} // namespace md::mpie
