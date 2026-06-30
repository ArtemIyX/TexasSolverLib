#pragma once

#include "games/hunl.hpp"
#include "ranges/source.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

/**
 * @brief Canonical combo ordering for exact-hand ranges on a given board.
 */
struct ComboIndex {
    std::vector<std::array<std::uint8_t, 2>> hands;
    std::unordered_map<std::uint16_t, std::size_t> hand_to_index;
    std::vector<std::uint8_t> board;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(const std::array<std::uint8_t, 2>& hole) const noexcept;
    [[nodiscard]] std::size_t index_of(const std::array<std::uint8_t, 2>& hole) const;

    static ComboIndex from_board(const std::vector<std::uint8_t>& board);
};

/**
 * @brief Per-action weights or masks used to filter a parent range into a child range.
 */
struct ActionRangeFilter {
    ActionId action = ACTION_CHECK;
    RangeMask mask;
    std::vector<Probability> multipliers;
};

/**
 * @brief Resulting child range for a specific action.
 */
struct ActionRangeTransition {
    ActionId action = ACTION_CHECK;
    CanonicalRange range;
};

/**
 * @brief Resulting child range for a public-card chance outcome.
 */
struct ChanceRangeTransition {
    std::uint8_t card = 0;
    Probability probability = 0.0;
    CanonicalRange range;
};

ComboIndex enumerate_combos(const std::vector<std::uint8_t>& board);
RangeMask board_mask(const ComboIndex& combos, const std::vector<std::uint8_t>& board);
RangeMask card_mask(const ComboIndex& combos, std::uint8_t card);
RangeMask dead_card_mask(const ComboIndex& combos, const std::vector<std::uint8_t>& dead_cards);

CanonicalRange propagate_range_to_action(
    const CanonicalRange& parent,
    const ActionRangeFilter& filter);
std::vector<ActionRangeTransition> propagate_range_to_actions(
    const CanonicalRange& parent,
    const std::vector<ActionRangeFilter>& filters);

ChanceRangeTransition propagate_range_to_chance_card(
    const CanonicalRange& parent,
    const ComboIndex& combos,
    std::uint8_t card,
    Probability probability);
std::vector<ChanceRangeTransition> propagate_range_to_chance_outcomes(
    const CanonicalRange& parent,
    const ComboIndex& combos,
    const std::vector<ChanceOutcome>& outcomes);

}  // namespace core
