#include "../math/test_framework.hpp"
#include "mpie/storage/feature_store_worker.hpp"
#include "mpie/storage/schema_metadata.hpp"
#include <filesystem>
#include <vector>

using namespace md::mpie;
using namespace md::mpie::tests;

void test_async_binary_write() {
    std::string test_file = "test_features.bin";
    
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
    
    {
        FeatureStoreWorker worker(test_file);
        worker.start();
        
        for (int i = 0; i < 100; ++i) {
            FeatureVector fv{};
            fv.engine_timestamp = 1000 + i;
            fv.symbol_id = i;
            fv.metrics[0] = i * 1.5;
            worker.enqueue(fv);
        }
        
        // Stop will flush
        worker.stop();
    }
    
    ASSERT_MATH_EQUAL("Storage", std::filesystem::exists(test_file) ? 1.0 : 0.0, 1.0, "File exists");
    auto size = std::filesystem::file_size(test_file);
    ASSERT_MATH_EQUAL("Storage", static_cast<double>(size), static_cast<double>(100 * sizeof(FeatureVector)), "Size exactly matches written_count * sizeof(FeatureVector)");
    
    std::filesystem::remove(test_file);
}

void test_schema_metadata() {
    std::string test_file = "test_mpie_schema.meta";
    
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
    
    SchemaMetadataGenerator::generate(test_file);
    
    ASSERT_MATH_EQUAL("Storage", std::filesystem::exists(test_file) ? 1.0 : 0.0, 1.0, "Schema exists");
    auto size = std::filesystem::file_size(test_file);
    ASSERT_MATH_EQUAL("Storage", size > 0 ? 1.0 : 0.0, 1.0, "Schema not empty");
    
    std::filesystem::remove(test_file);
}

void run_feature_store_tests() {
    test_async_binary_write();
    test_schema_metadata();
}
