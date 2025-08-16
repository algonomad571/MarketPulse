#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace md {

class SymbolRegistry {
public:
    SymbolRegistry();
    
    uint32_t get_or_add(std::string_view symbol);
    std::string_view by_id(uint32_t id) const;
    std::vector<std::pair<uint32_t, std::string>> get_all() const;
    size_t size() const { return symbols_.size(); }
    
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, uint32_t> symbol_to_id_;
    std::vector<std::string> symbols_;  // indexed by ID
    std::atomic<uint32_t> next_id_{1}; // 0 reserved for invalid
};

} // namespace md