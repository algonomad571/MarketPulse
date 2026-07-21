#include "test_framework.hpp"
#include "golden_dataset.hpp"
#include "mpie/executors/price_features.hpp"
#include "mpie/core/feature_context.hpp"

using namespace md::mpie;
using namespace md::mpie::tests;

void test_spread() {
    auto dataset = generate_golden_dataset();
    for (const auto& gs : dataset) {
        SymbolState state{};
        FeatureContext ctx{ &gs.event, nullptr, &state };
        FeatureVector fv{};
        SpreadExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("Spread", fv.metrics[IDX_SPREAD], gs.expected.spread, gs.description);
    }
}

void test_mid_price() {
    auto dataset = generate_golden_dataset();
    for (const auto& gs : dataset) {
        SymbolState state{};
        FeatureContext ctx{ &gs.event, nullptr, &state };
        FeatureVector fv{};
        MidPriceExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MidPrice", fv.metrics[IDX_MID_PRICE], gs.expected.mid, gs.description);
    }
}

void test_micro_price() {
    auto dataset = generate_golden_dataset();
    for (const auto& gs : dataset) {
        SymbolState state{};
        FeatureContext ctx{ &gs.event, nullptr, &state };
        FeatureVector fv{};
        MicroPriceExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MicroPrice", fv.metrics[IDX_MICRO_PRICE], gs.expected.micro, gs.description);
    }
}

void test_wap() {
    auto dataset = generate_golden_dataset();
    for (const auto& gs : dataset) {
        SymbolState state{};
        FeatureContext ctx{ &gs.event, nullptr, &state };
        FeatureVector fv{};
        WAPExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("WAP", fv.metrics[IDX_WAP], gs.expected.wap, gs.description);
    }
}

void test_log_return() {
    auto dataset = generate_golden_dataset();
    SymbolState state{}; 
    
    for (const auto& gs : dataset) {
        if (gs.event.ask_price > 0.0 && gs.event.bid_price > 0.0) {
            state.book.implied_mid = (gs.event.bid_price + gs.event.ask_price) / 2.0;
            state.book.mid_history.push(state.book.implied_mid);
        }
        
        FeatureContext ctx{ &gs.event, nullptr, &state };
        FeatureVector fv{};
        
        LogReturnExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("LogReturn", fv.metrics[IDX_LOG_RETURN], gs.expected.log_ret, gs.description);
    }
}

void run_price_feature_tests() {
    test_spread();
    test_mid_price();
    test_micro_price();
    test_wap();
    test_log_return();
}
