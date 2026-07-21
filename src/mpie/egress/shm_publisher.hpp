#pragma once

#include "../core/feature_vector.hpp"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <atomic>
#include <string>
#include <cstring>
#include <stdexcept>

#if defined(_M_X64) || defined(__x86_64__)
    #include <xmmintrin.h>
    #define MPIE_PAUSE() _mm_pause()
    #define MPIE_PREFETCH_READ(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#else
    #define MPIE_PAUSE() do {} while(0)
    #define MPIE_PREFETCH_READ(addr) do {} while(0)
#endif

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
        
        // Cold-path initialization:
        // Force the OS kernel to map every 4KB physical page immediately!
        volatile char* ptr = static_cast<volatile char*>(region_.get_address());
        for (size_t offset = 0; offset < total_size; offset += 4096) {
            ptr[offset] = 0; 
        }

#if defined(__linux__)
        // On Linux, lock the memory segment in RAM to prevent the kernel from swapping it to disk
        ::mlock(region_.get_address(), total_size);
#endif
    }

    ~ShmPublisher() {
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    inline void publish(uint32_t symbol_id, const FeatureVector& fv) noexcept {
        if (symbol_id < universe_size_) [[likely]] {
            MPIE_PREFETCH_READ(&shm_base_ptr_[symbol_id]);
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
