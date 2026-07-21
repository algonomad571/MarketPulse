#include "test_framework.hpp"
#include "mpie/engine/feature_validator.hpp"
#include <limits>

using namespace md::mpie;
using namespace md::mpie::tests;

void test_nan_infinity_validation() {
    FeatureValidator validator;
    
    {
        FeatureVector fv{};
        fv.metrics[IDX_SPREAD] = std::numeric_limits<double>::quiet_NaN();
        validator.validate(fv);
        ASSERT_MATH_EQUAL("Validator_NaN", fv.is_valid ? 1.0 : 0.0, 0.0, "Validator should catch NaN");
    }
    
    {
        FeatureVector fv{};
        fv.metrics[IDX_WAP] = std::numeric_limits<double>::infinity();
        validator.validate(fv);
        ASSERT_MATH_EQUAL("Validator_Inf", fv.is_valid ? 1.0 : 0.0, 0.0, "Validator should catch Infinity");
    }
}

void run_edge_case_tests() {
    test_nan_infinity_validation();
}
