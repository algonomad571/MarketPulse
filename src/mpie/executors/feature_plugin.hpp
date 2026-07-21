#pragma once
#include "../core/feature_context.hpp"
#include "../core/feature_vector.hpp"
#include <string_view>
#include <concepts>

namespace md::mpie {

template<typename T>
concept FeaturePlugin = requires(T plugin, const FeatureContext* ctx, FeatureVector& fv) {
    { T::name() } -> std::same_as<std::string_view>;
    { T::version() } -> std::same_as<uint32_t>;
    { plugin.compute(ctx, fv) } -> std::same_as<void>;
};

} // namespace md::mpie
