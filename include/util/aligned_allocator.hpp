#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace core {

template <class T, std::size_t Alignment>
class AlignedAllocator {
public:
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <class U>
    constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        void* ptr = ::operator new(n * sizeof(T), std::align_val_t{Alignment});
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept {
        ::operator delete(ptr, std::align_val_t{Alignment});
    }

    template <class U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
};

template <class T, class U, std::size_t Alignment>
constexpr bool operator==(const AlignedAllocator<T, Alignment>&, const AlignedAllocator<U, Alignment>&) noexcept {
    return true;
}

template <class T, class U, std::size_t Alignment>
constexpr bool operator!=(const AlignedAllocator<T, Alignment>&, const AlignedAllocator<U, Alignment>&) noexcept {
    return false;
}

}  // namespace core
