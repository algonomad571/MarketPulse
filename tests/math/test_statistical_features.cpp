#include "test_framework.hpp"
#include "mpie/executors/statistical_features.hpp"
#include "mpie/core/feature_context.hpp"

using namespace md::mpie;
using namespace md::mpie::tests;

void test_vwap32() {
    // Zero volume fallback
    {
        SymbolState state{};
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        fv.metrics[IDX_MID_PRICE] = 100.5; // fallback
        
        VWAP32Executor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("VWAP_32", fv.metrics[IDX_VWAP_32], 100.5, "Zero depth fallback");
    }

    // Normal math
    {
        SymbolState state{};
        state.trade.price_history.push(100.0);
        state.trade.volume_history.push(10);
        state.trade.price_history.push(102.0);
        state.trade.volume_history.push(10);
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        VWAP32Executor{}.compute(&ctx, fv);
        
        // (100*10 + 102*10) / 20 = 2020 / 20 = 101.0
        ASSERT_MATH_EQUAL("VWAP_32", fv.metrics[IDX_VWAP_32], 101.0, "Simple 2-tick VWAP");
    }
}

void test_realized_volatility32() {
    // Constant price history
    {
        SymbolState state{};
        for (int i = 0; i < 32; ++i) {
            state.book.mid_history.push(100.0);
        }
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        RealizedVolatility32Executor{}.compute(&ctx, fv);
        
        ASSERT_MATH_EQUAL("RealizedVolatility_32", fv.metrics[IDX_REALIZED_VOLATILITY_32], 0.0, "Constant Volatility");
        ASSERT_MATH_EQUAL("PriceZScore_32", fv.metrics[IDX_PRICE_ZSCORE_32], 0.0, "Constant Z-Score fallback");
    }

    // Linear escalation (prices: 1, 2, 3)
    {
        SymbolState state{};
        state.book.mid_history.push(1.0);
        state.book.mid_history.push(2.0);
        state.book.mid_history.push(3.0);
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        RealizedVolatility32Executor{}.compute(&ctx, fv);
        
        // Count: 3. Prices (lookback 0, 1, 2): 3, 2, 1
        // Mean: 2.0
        // Variance: ((3-2)^2 + (2-2)^2 + (1-2)^2) / 2 = (1 + 0 + 1) / 2 = 1.0
        // StdDev: 1.0
        // ZScore of latest (3): (3 - 2) / 1 = 1.0
        ASSERT_MATH_EQUAL("RealizedVolatility_32", fv.metrics[IDX_REALIZED_VOLATILITY_32], 1.0, "Linear StdDev");
        ASSERT_MATH_EQUAL("PriceZScore_32", fv.metrics[IDX_PRICE_ZSCORE_32], 1.0, "Linear Z-Score");
    }
}

void test_ofi_zscore32() {
    // Constant
    {
        SymbolState state{};
        for (int i = 0; i < 32; ++i) {
            state.flow.ofi_history.push(50.0);
        }
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        OFIZScore32Executor{}.compute(&ctx, fv);
        
        ASSERT_MATH_EQUAL("OFI_ZScore_32", fv.metrics[IDX_OFI_ZSCORE_32], 0.0, "Constant OFI ZScore");
    }

    // Simple diff
    {
        SymbolState state{};
        state.flow.ofi_history.push(-10.0);
        state.flow.ofi_history.push(10.0);
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        OFIZScore32Executor{}.compute(&ctx, fv);
        
        // Count: 2. Values: 10, -10
        // Mean: 0.0
        // Variance: ((10-0)^2 + (-10-0)^2) / 1 = (100 + 100) / 1 = 200
        // StdDev: sqrt(200) ~ 14.1421356237
        // ZScore of 10: 10 / 14.1421356237 ~ 0.70710678118
        double std_dev = std::sqrt(200.0);
        double expected_z = 10.0 / std_dev;
        ASSERT_MATH_EQUAL("OFI_ZScore_32", fv.metrics[IDX_OFI_ZSCORE_32], expected_z, "Oscillating OFI ZScore");
    }
}

void run_statistical_feature_tests() {
    test_vwap32();
    test_realized_volatility32();
    test_ofi_zscore32();
}
