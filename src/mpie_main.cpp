#include "mpie/engine/feature_engine.hpp"
#include "mpie/engine/mpie_validator.hpp"
#include "mpie/storage/schema_metadata.hpp"
#include <iostream>

using namespace md::mpie;

int main() {
    std::cout << "Starting MarketPulse Intelligence Engine (MPIE) Pre-Flight Validator\n";

    SchemaMetadataGenerator::generate();

    // Create feature engine with 4 workers
    FeatureEngine engine(4);
    
    // Initialize the engine (validates registry, sets up workers and queues)
    engine.initialize(2000);

    // Start processing threads (pinned to cores)
    engine.start();

    // Run the automated validation suite (Zero Allocation, Hardware Aligned)
    validation::run_preflight_checks(engine);

    std::cout << "MPIE shutdown complete.\n";
    return 0;
}
