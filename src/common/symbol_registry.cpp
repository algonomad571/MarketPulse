#include "symbol_registry.hpp"

namespace md {

SymbolRegistry::SymbolRegistry() {
    // Reserve slot 0 as invalid
    symbols_.emplace_back("");
}

uint32_t SymbolRegistry::get_or_add(std::string_view symbol) {
    {
        std::shared_lock lock(mutex_);
        auto it = symbol_to_id_.find(std::string(symbol));
        if (it != symbol_to_id_.end()) {
            return it->second;
        }
    }
    
    // Need to add new symbol
    std::unique_lock lock(mutex_);
    
    // Double-check in case another thread added it
    auto it = symbol_to_id_.find(std::string(symbol));
    if (it != symbol_to_id_.end()) {
        return it->second;
    }
    
    uint32_t id = next_id_.fetch_add(1);
    std::string symbol_str(symbol);
    
    symbol_to_id_[symbol_str] = id;
    
    // Ensure vector is large enough
    if (symbols_.size() <= id) {
        symbols_.resize(id + 1);
    }
    symbols_[id] = std::move(symbol_str);
    
    return id;
}

std::string_view SymbolRegistry::by_id(uint32_t id) const {
    std::shared_lock lock(mutex_);
    if (id >= symbols_.size()) {
        return {};
    }
    return symbols_[id];
}

std::vector<std::pair<uint32_t, std::string>> SymbolRegistry::get_all() const {
    std::shared_lock lock(mutex_);
    std::vector<std::pair<uint32_t, std::string>> result;
    result.reserve(symbols_.size());
    
    for (uint32_t i = 1; i < symbols_.size(); ++i) {
        if (!symbols_[i].empty()) {
            result.emplace_back(i, symbols_[i]);
        }
    }
    
    return result;
}

} // namespace md