#pragma once

#include "../core/feature_vector.hpp"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <atomic>
#include <string>
#include <cstring>
#include <stdexcept>

namespace md::mpie {

class ShmPublisher {
public:
    explicit ShmPublisher(uint32_t universe_size, const std::string& shm_name = "MPIE_LIVE_FEATURES")
        : universe_size_(universe_size), shm_name_(shm_name), shm_base_ptr_(nullptr)
    {
        using namespace boost::interprocess;
        
        shared_memory_object::remove(shm_name_.c_str());
        
        size_t total_size = sizeof(FeatureVector) * universe_size_;
        
        shm_obj_ = shared_memory_object(create_only, shm_name_.c_str(), read_write);
        shm_obj_.truncate(total_size);
        
        region_ = mapped_region(shm_obj_, read_write);
        shm_base_ptr_ = static_cast<FeatureVector*>(region_.get_address());
        
        std::memset(shm_base_ptr_, 0, total_size);
    }

    ~ShmPublisher() {
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    inline void publish(uint32_t symbol_id, const FeatureVector& fv) noexcept {
        if (symbol_id < universe_size_) [[likely]] {
            shm_base_ptr_[symbol_id] = fv;
            std::atomic_thread_fence(std::memory_order_release);
        }
    }

private:
    uint32_t universe_size_;
    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    FeatureVector* shm_base_ptr_;
};

} // namespace md::mpie
