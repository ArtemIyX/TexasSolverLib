#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace core::detail {

inline std::uint32_t checked_u32_count(std::size_t value, const char* name) {
    if (value > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::overflow_error(name);
    }
    return static_cast<std::uint32_t>(value);
}

inline std::size_t checked_size_add(std::size_t left, std::size_t right, const char* name) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::overflow_error(name);
    }
    return left + right;
}

}  // namespace core::detail
