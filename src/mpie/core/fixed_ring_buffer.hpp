#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace md::mpie {

// N must be a power of 2 to allow fast bitmask wrapping.
template <typename T, size_t N>
class alignas(64) FixedRingBuffer {
    static_assert((N & (N - 1)) == 0, "FixedRingBuffer capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "FixedRingBuffer requires trivially copyable types");

public:
    FixedRingBuffer() noexcept : head_(0), count_(0) {}

    inline void push(const T& value) noexcept {
        data_[head_ & MASK] = value;
        head_++;
        if (count_ < N) count_++;
    }

    [[nodiscard]] inline const T& lookback(size_t i) const noexcept {
        // i = 0 is the most recently pushed element.
        return data_[(head_ - 1 - i) & MASK];
    }

    [[nodiscard]] inline size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return N; }

private:
    static constexpr size_t MASK = N - 1;
    std::array<T, N> data_{};
    size_t head_;
    size_t count_;
};

} // namespace md::mpie
