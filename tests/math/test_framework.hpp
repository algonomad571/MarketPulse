#pragma once

#include <iostream>
#include <iomanip>
#include <cmath>
#include <string>
#include <unordered_map>
#include <atomic>

namespace md::mpie::tests {

constexpr double EPS = 1e-9;

struct TestStats {
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> passed{0};
    std::atomic<uint64_t> failed{0};
};

inline TestStats global_stats;

struct FeatureStats {
    uint64_t total{0};
    uint64_t passed{0};
    uint64_t failed{0};
};

inline std::unordered_map<std::string, FeatureStats> feature_stats;

inline void report_failure(const std::string& feature_name, double actual, double expected, const std::string& msg) {
    double abs_err = std::abs(actual - expected);
    double rel_err = (expected != 0.0) ? (abs_err / std::abs(expected)) : abs_err;
    
    std::cerr << "\n[FAIL] " << msg << "\n"
              << std::left << std::setw(20) << "Feature Name" << ": " << feature_name << "\n"
              << std::left << std::setw(20) << "Expected" << ": " << std::fixed << std::setprecision(10) << expected << "\n"
              << std::left << std::setw(20) << "Actual" << ": " << actual << "\n"
              << std::left << std::setw(20) << "Absolute Error" << ": " << abs_err << "\n"
              << std::left << std::setw(20) << "Relative Error" << ": " << rel_err << "\n"
              << std::left << std::setw(20) << "Tolerance" << ": " << EPS << "\n\n";
}

inline void assert_math_equal(const std::string& feature_name, double actual, double expected, const std::string& msg) {
    global_stats.total++;
    feature_stats[feature_name].total++;
    
    bool is_match = false;
    if (std::isnan(expected)) {
        is_match = std::isnan(actual);
    } else if (std::isinf(expected)) {
        is_match = std::isinf(actual) && ((expected > 0) == (actual > 0));
    } else {
        is_match = (std::abs(actual - expected) < EPS);
    }

    if (is_match) {
        global_stats.passed++;
        feature_stats[feature_name].passed++;
    } else {
        global_stats.failed++;
        feature_stats[feature_name].failed++;
        report_failure(feature_name, actual, expected, msg);
    }
}

inline void print_feature_summary(const std::string& feature_name) {
    auto& stats = feature_stats[feature_name];
    if (stats.failed == 0) {
        std::cout << "[PASS] " << feature_name << " (" << stats.total << " test cases)\n";
    } else {
        std::cout << "[FAIL] " << feature_name << " (" << stats.failed << "/" << stats.total << " failed)\n";
    }
}

} // namespace md::mpie::tests

#define ASSERT_MATH_EQUAL(feature, actual, expected, msg) \
    md::mpie::tests::assert_math_equal(feature, actual, expected, msg)
