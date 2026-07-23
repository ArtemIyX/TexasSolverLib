#include "games/hunl.hpp"
#include "ranges/propagation.hpp"
#include "ranges/range.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

TEST_CASE(ranges_normalization_sums_to_one) {
    core::RangeVector range;
    range.weights = {2.0, 3.0, 5.0};
    range.normalize();

    EXPECT_NEAR(range.sum(), 1.0, 1e-12);
    EXPECT_NEAR(range.weights[0], 0.2, 1e-12);
    EXPECT_NEAR(range.weights[1], 0.3, 1e-12);
    EXPECT_NEAR(range.weights[2], 0.5, 1e-12);
}

TEST_CASE(ranges_zero_mass_falls_back_to_uniform_distribution) {
    core::RangeVector range;
    range.weights = {0.0, 0.0, 0.0, 0.0};
    range.renormalize();

    EXPECT_NEAR(range.sum(), 1.0, 1e-12);
    for (const auto weight : range.weights) {
        EXPECT_NEAR(weight, 0.25, 1e-12);
    }
}

TEST_CASE(ranges_illegal_hands_are_removed_by_blocker_masking) {
    const std::vector<std::uint8_t> board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)};
    const auto combos = core::enumerate_combos(board);
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();

    const auto mask = core::dead_card_mask(combos, {core::card_to_int(2, 0)});
    core::apply_mask(range, mask);

    for (std::size_t i = 0; i < combos.hands.size(); ++i) {
        if (combos.hands[i][0] == core::card_to_int(2, 0) ||
            combos.hands[i][1] == core::card_to_int(2, 0)) {
            EXPECT_NEAR(range.weights[i], 0.0, 1e-12);
        }
    }
    EXPECT_NEAR(range.sum(), 1.0, 1e-12);
}

TEST_CASE(ranges_twenty_all_blocked_posteriors_fail_without_resurrecting_hands) {
    for (std::size_t count = 1; count <= 20; ++count) {
        core::RangeVector range;
        range.weights.assign(count, 1.0);
        const auto original = range.weights;
        core::RangeMask mask;
        mask.enabled.assign(count, 0U);

        EXPECT_THROW(core::apply_mask(range, mask), std::invalid_argument);
        EXPECT_EQ(range.weights, original);
    }
}

TEST_CASE(ranges_twenty_sparse_masks_preserve_zeroed_entries) {
    for (std::size_t survivor = 0; survivor < 20; ++survivor) {
        core::RangeVector range;
        range.weights.assign(20, 1.0);
        core::RangeMask mask;
        mask.enabled.assign(20, 0U);
        mask.enabled[survivor] = 1U;

        core::apply_mask(range, mask);
        EXPECT_NEAR(range.sum(), 1.0, 1e-12);
        for (std::size_t index = 0; index < 20; ++index) {
            EXPECT_NEAR(
                range.weights[index],
                index == survivor ? 1.0 : 0.0,
                1e-12);
        }
    }
}

TEST_CASE(ranges_reject_twenty_non_finite_or_negative_prior_weights) {
    for (std::size_t index = 0; index < 20; ++index) {
        core::RangeVector range;
        range.weights.assign(20, 1.0);
        range.weights[index] = index % 3U == 0U
            ? -1.0
            : (index % 3U == 1U
                ? std::numeric_limits<double>::infinity()
                : std::numeric_limits<double>::quiet_NaN());
        EXPECT_THROW(range.renormalize(), std::invalid_argument);

        core::RangeMask mask;
        mask.enabled.assign(20, 1U);
        EXPECT_THROW(core::apply_mask(range, mask), std::invalid_argument);
    }
}

TEST_CASE(ranges_board_cards_remove_impossible_combos) {
    const std::vector<std::uint8_t> board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)};
    const auto combos = core::enumerate_combos(board);
    const auto board_block = core::board_mask(combos, board);

    for (std::size_t i = 0; i < combos.hands.size(); ++i) {
        EXPECT_TRUE(board_block.allows(i));
        for (const auto card : board) {
            EXPECT_TRUE(combos.hands[i][0] != card);
            EXPECT_TRUE(combos.hands[i][1] != card);
        }
    }
}

TEST_CASE(ranges_identical_inputs_produce_identical_normalized_ranges) {
    core::RangeVector lhs;
    lhs.weights = {4.0, 1.0, 5.0};
    core::RangeVector rhs = lhs;

    lhs.normalize();
    rhs.normalize();

    EXPECT_EQ(lhs.weights.size(), rhs.weights.size());
    for (std::size_t i = 0; i < lhs.weights.size(); ++i) {
        EXPECT_NEAR(lhs.weights[i], rhs.weights[i], 1e-12);
    }
}

}  // namespace
