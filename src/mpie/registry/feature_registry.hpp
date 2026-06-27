#pragma once

#include "../core/feature_descriptor.hpp"
#include <vector>
#include <unordered_map>
#include <string>

namespace md::mpie {

class FeatureRegistry {
public:
    FeatureRegistry() = default;
    ~FeatureRegistry() = default;

    // Register a feature by descriptor. 
    // Note: Assumes string_view and span in descriptor point to static memory.
    bool register_feature(const FeatureDescriptor& descriptor);

    // Validate the registry (e.g., check for circular dependencies, pass ordering)
    bool validate() const;

    // Get all registered features
    const std::vector<FeatureDescriptor>& get_features() const;

private:
    std::vector<FeatureDescriptor> features_;
    std::unordered_map<std::string, FeatureId> feature_name_to_id_;
    FeatureId next_id_{1};
};

} // namespace md::mpie
