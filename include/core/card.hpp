#pragma once

#include "core/namespaces.hpp"

#include <cstdint>
#include <stdexcept>

namespace texas::core {

class Card {
public:
    static constexpr std::uint8_t COUNT = 52U;

    constexpr Card() noexcept = default;

    [[nodiscard]] static Card from_index(std::uint8_t index) {
        if (index >= COUNT) throw std::invalid_argument("card index must be in [0, 51]");
        return Card(index);
    }

    [[nodiscard]] static Card from_rank_suit(std::uint8_t rank, std::uint8_t suit) {
        if (rank < 2U || rank > 14U || suit >= 4U) {
            throw std::invalid_argument("card rank or suit is invalid");
        }
        return Card(static_cast<std::uint8_t>((rank - 2U) * 4U + suit));
    }

    [[nodiscard]] constexpr std::uint8_t index() const noexcept { return index_; }
    [[nodiscard]] constexpr std::uint8_t rank() const noexcept {
        return static_cast<std::uint8_t>(index_ / 4U + 2U);
    }
    [[nodiscard]] constexpr std::uint8_t suit() const noexcept {
        return static_cast<std::uint8_t>(index_ % 4U);
    }

    friend constexpr bool operator==(Card left, Card right) noexcept { return left.index_ == right.index_; }
    friend constexpr bool operator!=(Card left, Card right) noexcept { return !(left == right); }
    friend constexpr bool operator<(Card left, Card right) noexcept { return left.index_ < right.index_; }

private:
    explicit constexpr Card(std::uint8_t index) noexcept : index_(index) {}
    std::uint8_t index_ = 0U;
};

static_assert(sizeof(Card) == sizeof(std::uint8_t), "Card must remain byte-sized");

}  // namespace texas::core
