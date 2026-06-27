#include "feature_registry.hpp"
#include <iostream>

namespace md::mpie {

bool FeatureRegistry::register_feature(const FeatureDescriptor& descriptor) {
    std::string name_str(descriptor.name);
    if (feature_name_to_id_.find(name_str) != feature_name_to_id_.end()) {
        std::cerr << "Feature already registered: " << descriptor.name << "\n";
        return false;
    }
    
    features_.push_back(descriptor);
    feature_name_to_id_[name_str] = next_id_++;
    return true;
}

bool FeatureRegistry::validate() const {
    // M1 Placeholder: validation logic for dependencies and passes will go here.
    std::cout << "[FeatureRegistry] Validating " << features_.size() << " features.\n";
    return true;
}

const std::vector<FeatureDescriptor>& FeatureRegistry::get_features() const {
    return features_;
}

} // namespace md::mpie
