#pragma once

#include <cstdint>
#include <string_view>
#include <span>
#include <type_traits>

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

struct alignas(64) FeatureDescriptor {
    std::string_view name;
    std::span<const FeatureId> dependencies;
    uint32_t feature_version;
    uint32_t schema_version;
    uint32_t lookback_window;
    FeatureCategory category;
    ExecutionPass pass;
    bool stateful;
    Complexity complexity;
    uint8_t _pad[8]; // Padding to explicitly reach 64 bytes if needed, though alignas handles it.
};

struct alignas(64) FeatureVector {
    uint64_t timestamp;
    uint32_t symbol_id;
    double values[6]; // Dummy placeholder for 64-byte alignment
};

struct alignas(64) ExecutionMetadata {
    uint64_t engine_timestamp;
    uint64_t latency_ns;
    uint32_t worker_id;
    uint32_t flags;
    uint64_t _pad[4]; // Padding
};

// Compiler Contract Checks (Task 2)
static_assert(sizeof(FeatureDescriptor) % 64 == 0, "FeatureDescriptor must be 64-byte aligned.");
static_assert(std::is_standard_layout_v<FeatureDescriptor>, "FeatureDescriptor must be standard layout.");
static_assert(std::is_trivially_copyable_v<FeatureDescriptor>, "FeatureDescriptor must be trivially copyable.");

static_assert(sizeof(FeatureVector) % 64 == 0, "FeatureVector must be 64-byte aligned.");
static_assert(std::is_standard_layout_v<FeatureVector>, "FeatureVector must be standard layout.");
static_assert(std::is_trivially_copyable_v<FeatureVector>, "FeatureVector must be trivially copyable.");

static_assert(sizeof(ExecutionMetadata) % 64 == 0, "ExecutionMetadata must be 64-byte aligned.");
static_assert(std::is_standard_layout_v<ExecutionMetadata>, "ExecutionMetadata must be standard layout.");
static_assert(std::is_trivially_copyable_v<ExecutionMetadata>, "ExecutionMetadata must be trivially copyable.");

} // namespace md::mpie
