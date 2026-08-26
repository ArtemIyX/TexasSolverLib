#pragma once

#include "games/hunl.hpp"

#include <array>
#include <cstdint>

namespace texas::games::hunl {

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
    static Strength evaluate_6(const std::array<std::uint8_t, 6>& cards);
    static Strength evaluate_7(const std::array<std::uint8_t, 7>& cards);
};
int compare_7(
    const std::array<std::uint8_t, 7>& lhs,
    const std::array<std::uint8_t, 7>& rhs) noexcept;

}  // namespace texas::games::hunl


