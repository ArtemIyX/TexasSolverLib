#pragma once

#include "core/hunl.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace core {

struct Strength {
    std::uint64_t value = 0;

    constexpr bool operator==(const Strength& other) const noexcept {
        return value == other.value;
    }
    constexpr bool operator<(const Strength& other) const noexcept {
        return value < other.value;
    }
    constexpr bool operator>(const Strength& other) const noexcept {
        return value > other.value;
    }
    constexpr bool operator<=(const Strength& other) const noexcept {
        return value <= other.value;
    }
    constexpr bool operator>=(const Strength& other) const noexcept {
        return value >= other.value;
    }

    static Strength evaluate_5(const std::array<std::uint8_t, 5>& cards);
    static Strength evaluate_7(const std::array<std::uint8_t, 7>& cards);
};

Strength evaluate_n(const std::vector<std::uint8_t>& cards);

}  // namespace core
