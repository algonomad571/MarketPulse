#pragma once
#include <fstream>
#include <string>
#include "../core/feature_vector.hpp"

namespace md::mpie {

struct SchemaMetadataGenerator {
    static void generate(const std::string& filepath = "mpie_schema.meta") {
        std::ofstream meta(filepath, std::ios::trunc);
        if (!meta.is_open()) return;
        
        meta << "{\n"
             << "  \"schema_version\": 1,\n"
             << "  \"struct_size\": " << sizeof(FeatureVector) << ",\n"
             << "  \"fields\": [\n"
             << "    {\"name\": \"engine_timestamp\", \"type\": \"uint64\", \"offset\": 0},\n"
             << "    {\"name\": \"symbol_id\", \"type\": \"uint32\", \"offset\": 8},\n"
             << "    {\"name\": \"version\", \"type\": \"uint32\", \"offset\": 12},\n"
             << "    {\"name\": \"is_valid\", \"type\": \"bool\", \"offset\": 16},\n"
             << "    {\"name\": \"metrics\", \"type\": \"double[" << TOTAL_METRICS << "]\", \"offset\": 24}\n"
             << "  ]\n"
             << "}\n";
    }
};

} // namespace md::mpie
