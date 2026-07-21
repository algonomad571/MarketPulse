#include "../math/test_framework.hpp"
#include "mpie/egress/shm_publisher.hpp"
#include "mpie/egress/shm_reader.hpp"
#include <thread>
#include <vector>
#include <exception>

using namespace md::mpie;
using namespace md::mpie::tests;

void test_shm_lifecycle_and_read() {
    uint32_t universe = 10;
    std::string test_shm_name = "TEST_MPIE_LIVE_FEATURES";
    
    // Create publisher
    {
        ShmPublisher pub(universe, test_shm_name);
        
        FeatureVector fv{};
        fv.engine_timestamp = 12345;
        fv.symbol_id = 5;
        fv.is_valid = true;
        fv.metrics[0] = 150.5;
        
        pub.publish(5, fv);
        
        // Attach reader
        ShmReader reader(universe, test_shm_name);
        FeatureVector out_fv{};
        bool success = reader.read(5, out_fv);
        
        ASSERT_MATH_EQUAL("SHM", success ? 1.0 : 0.0, 1.0, "Reader attaches and reads valid feature");
        ASSERT_MATH_EQUAL("SHM", static_cast<double>(out_fv.engine_timestamp), 12345.0, "Timestamp matches");
        ASSERT_MATH_EQUAL("SHM", out_fv.metrics[0], 150.5, "Data matches exactly");
        
        bool success_empty = reader.read(2, out_fv);
        ASSERT_MATH_EQUAL("SHM", success_empty ? 1.0 : 0.0, 0.0, "Empty region is invalid");
    }
    
    // Verify removal
    bool threw = false;
    try {
        ShmReader reader(universe, test_shm_name);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_MATH_EQUAL("SHM", threw ? 1.0 : 0.0, 1.0, "Reader throws if SHM does not exist");
}

void run_shm_egress_tests() {
    test_shm_lifecycle_and_read();
}
