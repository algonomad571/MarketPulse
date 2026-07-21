#pragma once
#include "../executors/price_features.hpp"
#include "../executors/book_features.hpp"
#include "../executors/flow_features.hpp"
#include "../executors/microstructure_features.hpp"
#include <tuple>

namespace md::mpie {

class FeaturePipeline {
    std::tuple<SpreadExecutor, MidPriceExecutor, MicroPriceExecutor, LogReturnExecutor, WAPExecutor> pass1_executors_;
    std::tuple<BookImbalanceExecutor, MultiLevelImbalanceL5Executor, BookPressureRatioExecutor, TradeFlowImbalanceExecutor> pass2_executors_;
    std::tuple<OrderFlowImbalanceExecutor> pass3_executors_;

public:
    FeaturePipeline() = default;

    inline void execute_pipeline(const FeatureContext* ctx, FeatureVector& fv) noexcept {
        // Pass 1: Base Price Metrics
        std::apply([ctx, &fv](auto&&... exec) { (exec.compute(ctx, fv), ...); }, pass1_executors_);
        
        // Pass 2: Contextual Book Metrics
        std::apply([ctx, &fv](auto&&... exec) { (exec.compute(ctx, fv), ...); }, pass2_executors_);

        // Pass 3: Flow Metrics (Can safely access Pass 1 and 2 records inside fv)
        std::apply([ctx, &fv](auto&&... exec) { (exec.compute(ctx, fv), ...); }, pass3_executors_);
    }
};

} // namespace md::mpie
