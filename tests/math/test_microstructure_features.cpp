#include "test_framework.hpp"
#include "mpie/executors/microstructure_features.hpp"
#include "mpie/core/feature_context.hpp"

using namespace md::mpie;
using namespace md::mpie::tests;

void test_multilevel_imbalance() {
    // Single-level fallback (levels 1-4 are zero)
    {
        SymbolState state{};
        state.book.bids[0] = 100.0;
        state.book.asks[0] = 100.2;
        state.book.bid_sizes[0] = 100;
        state.book.ask_sizes[0] = 50;
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        MultiLevelImbalanceL5Executor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MultiLevelImbalanceL5", fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5], 1.0/3.0, "Single-level fallback (skewed)");
    }
    
    // Symmetric book 5-levels
    {
        SymbolState state{};
        for (size_t i = 0; i < 5; ++i) {
            state.book.bids[i] = 100.0 - (i * 0.1);
            state.book.asks[i] = 100.1 + (i * 0.1);
            state.book.bid_sizes[i] = 10 * (i + 1);
            state.book.ask_sizes[i] = 10 * (i + 1);
        }
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        MultiLevelImbalanceL5Executor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MultiLevelImbalanceL5", fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5], 0.0, "Symmetric 5-level book");
    }
    
    // Fully bid-skewed
    {
        SymbolState state{};
        for (size_t i = 0; i < 5; ++i) {
            state.book.bids[i] = 100.0 - (i * 0.1);
            state.book.bid_sizes[i] = 10 * (i + 1);
            state.book.ask_sizes[i] = 0;
        }
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        MultiLevelImbalanceL5Executor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MultiLevelImbalanceL5", fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5], 1.0, "Fully bid-skewed 5-level book");
    }
    
    // Zero depth
    {
        SymbolState state{}; // all 0
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        MultiLevelImbalanceL5Executor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("MultiLevelImbalanceL5", fv.metrics[IDX_MULTILEVEL_IMBALANCE_L5], 0.0, "Zero depth protection");
    }
}

void test_book_pressure_ratio() {
    {
        SymbolState state{};
        state.book.bids[0] = 100.0;
        state.book.asks[0] = 100.2;
        state.book.bid_sizes[0] = 100;
        state.book.ask_sizes[0] = 100;
        
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        fv.metrics[IDX_MID_PRICE] = 100.1;
        BookPressureRatioExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("BookPressureRatio", fv.metrics[IDX_BOOK_PRESSURE_RATIO], 0.0, "Symmetric L1 Pressure");
    }
    
    {
        SymbolState state{};
        FeatureContext ctx{ nullptr, nullptr, &state };
        FeatureVector fv{};
        fv.metrics[IDX_MID_PRICE] = 100.1;
        BookPressureRatioExecutor{}.compute(&ctx, fv);
        ASSERT_MATH_EQUAL("BookPressureRatio", fv.metrics[IDX_BOOK_PRESSURE_RATIO], 0.0, "Zero depth protection");
    }
}

void test_trade_flow_imbalance() {
    MarketEvent ev{};
    ev.event_type = EventType::Trade;
    ev.trade_size = 50;
    ev.trade_price = 100.2; 
    
    FeatureContext ctx{ &ev, nullptr, nullptr };
    FeatureVector fv{};
    fv.metrics[IDX_MID_PRICE] = 100.1;
    
    TradeFlowImbalanceExecutor{}.compute(&ctx, fv);
    ASSERT_MATH_EQUAL("TradeFlowImbalance", fv.metrics[IDX_TRADE_FLOW_IMBALANCE], 50.0, "Buyer initiated trade");
    ASSERT_MATH_EQUAL("EffectiveSpread", fv.metrics[IDX_EFFECTIVE_SPREAD], 0.2, "Buyer initiated effective spread");
    
    ev.trade_price = 100.0;
    TradeFlowImbalanceExecutor{}.compute(&ctx, fv);
    ASSERT_MATH_EQUAL("TradeFlowImbalance", fv.metrics[IDX_TRADE_FLOW_IMBALANCE], -50.0, "Seller initiated trade");
    ASSERT_MATH_EQUAL("EffectiveSpread", fv.metrics[IDX_EFFECTIVE_SPREAD], 0.2, "Seller initiated effective spread");
    
    ev.event_type = EventType::L2;
    TradeFlowImbalanceExecutor{}.compute(&ctx, fv);
    ASSERT_MATH_EQUAL("TradeFlowImbalance", fv.metrics[IDX_TRADE_FLOW_IMBALANCE], 0.0, "Non-trade fallback");
    ASSERT_MATH_EQUAL("EffectiveSpread", fv.metrics[IDX_EFFECTIVE_SPREAD], 0.0, "Non-trade fallback");
}

void run_microstructure_feature_tests() {
    test_multilevel_imbalance();
    test_book_pressure_ratio();
    test_trade_flow_imbalance();
}
