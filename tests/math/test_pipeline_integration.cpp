#include "test_framework.hpp"
#include "golden_dataset.hpp"
#include "mpie/engine/feature_pipeline.hpp"
#include "mpie/engine/state_manager.hpp"
#include "mpie/engine/context_builder.hpp"
#include "mpie/engine/feature_validator.hpp"

using namespace md::mpie;
using namespace md::mpie::tests;

void run_pipeline_integration_tests() {
    auto dataset = generate_golden_dataset();
    
    StateManager state_manager(10);
    FeaturePipeline pipeline;
    FeatureValidator validator;
    ContextBuilder builder;
    
    for (const auto& gs : dataset) {
        state_manager.update_state(gs.event);
        const auto& state = state_manager.get_state(gs.event.symbol_id);
        
        FeatureContext ctx = builder.build(gs.event, state, 0);
        FeatureVector fv{};
        
        pipeline.execute_pipeline(&ctx, fv);
        validator.validate(fv);
        
        ASSERT_MATH_EQUAL("Integration_Spread", fv.metrics[IDX_SPREAD], gs.expected.spread, gs.description);
        ASSERT_MATH_EQUAL("Integration_MidPrice", fv.metrics[IDX_MID_PRICE], gs.expected.mid, gs.description);
        ASSERT_MATH_EQUAL("Integration_MicroPrice", fv.metrics[IDX_MICRO_PRICE], gs.expected.micro, gs.description);
        ASSERT_MATH_EQUAL("Integration_WAP", fv.metrics[IDX_WAP], gs.expected.wap, gs.description);
        ASSERT_MATH_EQUAL("Integration_LogReturn", fv.metrics[IDX_LOG_RETURN], gs.expected.log_ret, gs.description);
    }
}
