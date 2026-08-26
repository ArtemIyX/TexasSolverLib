#include "core/canonical_combo.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

bool contains_card(const texas::CanonicalComboCards& hole, std::uint8_t card) {
    return hole[0] == card || hole[1] == card;
}

}  // namespace

TEST_CASE(canonical_combos_exhaustively_round_trip_all_hunl_holes) {
    const auto& combos = texas::canonical_combos();
    std::array<bool, texas::CANONICAL_HOLE_COMBINATION_COUNT> seen = {};

    for (std::size_t raw_id = 0U; raw_id < texas::CANONICAL_HOLE_COMBINATION_COUNT; ++raw_id) {
        const auto id = static_cast<texas::CanonicalComboId>(raw_id);
        const auto hole = combos.cards(id);
        EXPECT_TRUE(texas::is_card_index(hole[0]));
        EXPECT_TRUE(texas::is_card_index(hole[1]));
        EXPECT_TRUE(hole[0] < hole[1]);
        EXPECT_EQ(combos.id(hole), id);
        EXPECT_EQ(combos.id({hole[1], hole[0]}), id);
        EXPECT_TRUE(!seen[id]);
        seen[id] = true;
    }

    for (const auto was_seen : seen) EXPECT_TRUE(was_seen);

    const texas::CanonicalComboCards first = {0U, 1U};
    const texas::CanonicalComboCards last = {50U, 51U};
    EXPECT_EQ(combos.cards(0U), first);
    EXPECT_EQ(combos.cards(static_cast<texas::CanonicalComboId>(
                  texas::CANONICAL_HOLE_COMBINATION_COUNT - 1U)),
              last);
    EXPECT_THROW(combos.cards(static_cast<texas::CanonicalComboId>(
                     texas::CANONICAL_HOLE_COMBINATION_COUNT)),
                 std::out_of_range);
}

TEST_CASE(canonical_combos_reject_invalid_cards_and_dead_card_lists) {
    const auto& combos = texas::canonical_combos();
    EXPECT_THROW(combos.id({52U, 8U}), std::invalid_argument);
    EXPECT_THROW(combos.id({7U, 52U}), std::invalid_argument);
    EXPECT_THROW(combos.id({0U, 0U}), std::invalid_argument);
    EXPECT_THROW(combos.id({0U, 52U}), std::invalid_argument);
    EXPECT_THROW(combos.id({0U, 255U}), std::invalid_argument);

    const std::array<std::uint8_t, 2> duplicate_dead = {0U, 0U};
    const std::array<std::uint8_t, 1> invalid_dead = {52U};
    EXPECT_THROW(combos.legal_mask(nullptr, 1U), std::invalid_argument);
    EXPECT_THROW(combos.legal_mask(duplicate_dead.data(), duplicate_dead.size()), std::invalid_argument);
    EXPECT_THROW(combos.legal_mask(invalid_dead.data(), invalid_dead.size()), std::invalid_argument);
}

TEST_CASE(canonical_combo_legal_masks_match_exhaustive_blocker_rules) {
    const auto& combos = texas::canonical_combos();
    const auto no_dead = combos.legal_mask(nullptr, 0U);
    EXPECT_EQ(no_dead.count(), texas::CANONICAL_HOLE_COMBINATION_COUNT);

    const std::array<std::uint8_t, 3> flop = {0U, 25U, 50U};
    const auto flop_mask = combos.legal_mask(flop.data(), flop.size());
    EXPECT_EQ(flop_mask.count(), std::size_t{1176U});

    const std::array<std::uint8_t, 5> river = {0U, 1U, 2U, 3U, 4U};
    const auto river_mask = combos.legal_mask(river.data(), river.size());
    EXPECT_EQ(river_mask.count(), std::size_t{1081U});

    for (std::size_t raw_id = 0U; raw_id < texas::CANONICAL_HOLE_COMBINATION_COUNT; ++raw_id) {
        const auto hole = combos.cards(static_cast<texas::CanonicalComboId>(raw_id));
        const auto blocked = std::find(flop.begin(), flop.end(), hole[0]) != flop.end() ||
            std::find(flop.begin(), flop.end(), hole[1]) != flop.end();
        EXPECT_EQ(flop_mask.test(raw_id), !blocked);
    }
}

TEST_CASE(canonical_combo_range_enumeration_and_masks_preserve_canonical_order) {
    const std::vector<std::uint8_t> board = {0U, 25U, 50U};
    const auto index = texas::enumerate_combos(board);
    const auto& combos = texas::canonical_combos();
    const auto legal = combos.legal_mask(board.data(), board.size());

    EXPECT_EQ(index.size(), std::size_t{1176U});
    std::size_t index_position = 0U;
    for (std::size_t raw_id = 0U; raw_id < texas::CANONICAL_HOLE_COMBINATION_COUNT; ++raw_id) {
        if (!legal.test(raw_id)) continue;
        const auto hole = combos.cards(static_cast<texas::CanonicalComboId>(raw_id));
        EXPECT_EQ(index.hands[index_position], hole);
        EXPECT_EQ(index.index_of({hole[1], hole[0]}), index_position);
        ++index_position;
    }
    EXPECT_EQ(index_position, index.size());

    const std::uint8_t dealt = 9U;
    const auto mask = texas::dead_card_mask(index, {dealt});
    EXPECT_EQ(mask.size(), index.size());
    for (std::size_t index_id = 0U; index_id < index.size(); ++index_id) {
        EXPECT_EQ(mask.enabled[index_id] != 0U, !contains_card(index.hands[index_id], dealt));
    }
}
