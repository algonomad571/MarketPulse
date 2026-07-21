#pragma once
#include "../core/feature_vector.hpp"
#include <cmath>

namespace md::mpie {

class FeatureValidator {
public:
    inline void validate(FeatureVector& fv) const noexcept {
        for (size_t i = 0; i < TOTAL_METRICS; ++i) {
            double val = fv.metrics[i];
            if (std::isnan(val) || std::isinf(val)) [[unlikely]] {
                fv.is_valid = false;
                return;
            }
        }
    }
};

} // namespace md::mpie
