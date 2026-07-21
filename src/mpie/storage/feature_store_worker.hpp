#pragma once

#include "../core/feature_vector.hpp"
#include <boost/lockfree/spsc_queue.hpp>
#include <thread>
#include <atomic>
#include <fstream>
#include <string>
#include <memory>

namespace md::mpie {

class FeatureStoreWorker {
public:
    explicit FeatureStoreWorker(const std::string& filename)
        : queue_(262144), filename_(filename), running_(false) {}

    ~FeatureStoreWorker() {
        stop();
    }

    void start() noexcept {
        if (running_.exchange(true, std::memory_order_acquire)) {
            return;
        }
        
        file_.open(filename_, std::ios::binary | std::ios::out | std::ios::trunc);
        
        thread_ = std::make_unique<std::jthread>([this](std::stop_token token) {
            run(std::move(token));
        });
    }

    void stop() noexcept {
        if (running_.exchange(false, std::memory_order_release)) {
            if (thread_) {
                thread_->request_stop();
                thread_->join();
                thread_.reset();
            }
            
            // Drain remaining gracefully
            FeatureVector fv;
            while (queue_.pop(fv)) {
                if (file_.is_open()) {
                    file_.write(reinterpret_cast<const char*>(&fv), sizeof(FeatureVector));
                }
            }
            
            if (file_.is_open()) {
                file_.flush();
                file_.close();
            }
        }
    }

    inline void enqueue(const FeatureVector& fv) noexcept {
        // Non-blocking wait-free push. Dropped if full (handled by 256k capacity)
        queue_.push(fv);
    }

private:
    void run(std::stop_token token) noexcept {
        FeatureVector fv;
        while (!token.stop_requested()) {
            if (queue_.pop(fv)) {
                if (file_.is_open()) {
                    file_.write(reinterpret_cast<const char*>(&fv), sizeof(FeatureVector));
                }
            } else {
                std::this_thread::yield();
            }
        }
    }

    boost::lockfree::spsc_queue<FeatureVector> queue_;
    std::string filename_;
    std::ofstream file_;
    std::unique_ptr<std::jthread> thread_;
    std::atomic<bool> running_;
};

} // namespace md::mpie
