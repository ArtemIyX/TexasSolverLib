#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace core {

class Arena {
public:
    void reset() noexcept;
    std::size_t mark() const noexcept;
    void release(std::size_t mark) noexcept;

    template <class T>
    T* allocate(std::size_t count = 1);

private:
    std::vector<std::byte> buffer_;
    std::size_t offset_ = 0;
};

template <class T>
T* Arena::allocate(std::size_t count) {
    static_assert(std::is_trivially_destructible_v<T>, "Arena only supports trivially destructible types");

    const auto alignment = alignof(T);
    const auto bytes = sizeof(T) * count;
    const auto aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);
    const auto required = aligned_offset + bytes;
    if (required < aligned_offset) {
        throw std::overflow_error("Arena allocation overflow");
    }

    if (buffer_.size() < required) {
        buffer_.resize(required);
    }

    auto* ptr = reinterpret_cast<T*>(buffer_.data() + aligned_offset);
    offset_ = required;
    return ptr;
}

}  // namespace core


