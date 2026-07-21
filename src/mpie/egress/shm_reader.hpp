#pragma once

#include "../core/feature_vector.hpp"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <atomic>
#include <string>
#include <stdexcept>

namespace md::mpie {

class ShmReader {
public:
    explicit ShmReader(uint32_t universe_size, const std::string& shm_name = "MPIE_LIVE_FEATURES")
        : universe_size_(universe_size), shm_name_(shm_name), shm_base_ptr_(nullptr)
    {
        using namespace boost::interprocess;
        
        try {
            shm_obj_ = shared_memory_object(open_only, shm_name_.c_str(), read_only);
            region_ = mapped_region(shm_obj_, read_only);
            shm_base_ptr_ = static_cast<const FeatureVector*>(region_.get_address());
        } catch (const interprocess_exception& e) {
            throw std::runtime_error("Failed to attach to SHM: " + std::string(e.what()));
        }
    }

    inline bool read(uint32_t symbol_id, FeatureVector& out_fv) const noexcept {
        if (symbol_id < universe_size_ && shm_base_ptr_) [[likely]] {
            std::atomic_thread_fence(std::memory_order_acquire);
            out_fv = shm_base_ptr_[symbol_id];
            return out_fv.is_valid;
        }
        return false;
    }

private:
    uint32_t universe_size_;
    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    const FeatureVector* shm_base_ptr_;
};

} // namespace md::mpie
