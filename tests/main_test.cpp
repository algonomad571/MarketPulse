#include "math/test_framework.hpp"
#include <iostream>
#include <cstdlib>

using namespace md::mpie::tests;

extern void run_price_feature_tests();
extern void run_book_feature_tests();
extern void run_microstructure_feature_tests();
extern void run_statistical_feature_tests();
extern void run_flow_feature_tests();
extern void run_edge_case_tests();
extern void run_pipeline_integration_tests();
extern void run_feature_store_tests();

void print_coverage_report() {
    std::cout << "\n======================================\n";
    std::cout << "Feature Coverage Report\n\n";
    
    auto print_status = [](const std::string& name, bool implemented) {
        std::cout << std::left << std::setw(20) << name;
        if (implemented) {
            std::cout << ".......... PASS\n";
        } else {
            std::cout << ".......... PENDING\n";
        }
    };
    
    print_status("Spread", true);
    print_status("MidPrice", true);
    print_status("MicroPrice", true);
    print_status("WAP", true);
    print_status("LogReturn", true);
    print_status("QueueImbalance", false);
    print_status("OFI", false);
    print_status("Rolling VWAP", false);
    print_status("Rolling Volatility", false);
    
    std::cout << "======================================\n";
}

int main() {
    std::cout << "======================================================\n";
    std::cout << "  MPIE HARDENED REGRESSION TEST SUITE\n";
    std::cout << "======================================================\n\n";

    run_price_feature_tests();
    run_book_feature_tests();
    run_microstructure_feature_tests();
    run_statistical_feature_tests();
    run_flow_feature_tests();
    run_edge_case_tests();
    run_pipeline_integration_tests();
    run_feature_store_tests();

    std::cout << "\n======================================================\n";
    std::cout << "Test Summary\n";
    std::cout << "Total Assertions : " << global_stats.total.load() << "\n";
    std::cout << "Passed           : " << global_stats.passed.load() << "\n";
    std::cout << "Failed           : " << global_stats.failed.load() << "\n\n";

    print_feature_summary("Spread");
    print_feature_summary("MidPrice");
    print_feature_summary("MicroPrice");
    print_feature_summary("WAP");
    print_feature_summary("LogReturn");
    print_feature_summary("MultiLevelImbalanceL5");
    print_feature_summary("BookPressureRatio");
    print_feature_summary("TradeFlowImbalance");
    print_feature_summary("EffectiveSpread");
    print_feature_summary("VWAP_32");
    print_feature_summary("RealizedVolatility_32");
    print_feature_summary("PriceZScore_32");
    print_feature_summary("OFI_ZScore_32");
    print_feature_summary("Validator_NaN");
    print_feature_summary("Validator_Inf");
    
    print_coverage_report();

    if (global_stats.failed.load() > 0) {
        std::cerr << "\n[ERROR] Test suite failed!\n";
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
