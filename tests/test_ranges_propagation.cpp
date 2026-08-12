#include "games/hunl.hpp"
#include "ranges/propagation.hpp"
#include "ranges/source.hpp"
#include "test_harness.hpp"

#include <limits>
#include <vector>

namespace {

texas::CanonicalRange make_two_combo_parent() {
    const auto combos = texas::enumerate_combos({
        texas::card_to_int(14, 0), texas::card_to_int(13, 1), texas::card_to_int(12, 2)});
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 0.0);
    range.weights[0] = 0.4;
    range.weights[1] = 0.6;
    range.normalize();
    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    return texas::make_canonical_range_from_values(texas::RangeSourceKind::UniformPrior, range, mask);
}

TEST_CASE(ranges_parent_splits_correctly_across_actions) {
    const auto combos = texas::enumerate_combos({
        texas::card_to_int(14, 0), texas::card_to_int(13, 1), texas::card_to_int(12, 2)});
    const auto parent = make_two_combo_parent();

    texas::RangeMask first_only;
    first_only.kind = texas::RangeVector::Kind::Combo;
    first_only.enabled.assign(combos.size(), 0U);
    first_only.enabled[0] = 1U;

    texas::RangeMask second_only;
    second_only.kind = texas::RangeVector::Kind::Combo;
    second_only.enabled.assign(combos.size(), 0U);
    second_only.enabled[1] = 1U;

    const auto children = texas::propagate_range_to_actions(parent, {
        texas::ActionRangeFilter{texas::ACTION_CHECK, first_only, {}},
        texas::ActionRangeFilter{texas::ACTION_BET_75, second_only, {}},
    });

    EXPECT_EQ(children.size(), 2U);
    EXPECT_NEAR(children[0].range.range.weights[0], 1.0, 1e-12);
    EXPECT_NEAR(children[0].range.range.weights[1], 0.0, 1e-12);
    EXPECT_NEAR(children[1].range.range.weights[0], 0.0, 1e-12);
    EXPECT_NEAR(children[1].range.range.weights[1], 1.0, 1e-12);
}

TEST_CASE(ranges_child_ranges_preserve_total_mass_after_renormalization) {
    const auto parent = make_two_combo_parent();
    texas::ActionRangeFilter filter;
    filter.action = texas::ACTION_CALL;
    filter.multipliers = std::vector<double>(parent.range.size(), 0.5);

    const auto child = texas::propagate_range_to_action(parent, filter);
    EXPECT_NEAR(child.range.sum(), 1.0, 1e-12);
}

TEST_CASE(ranges_action_propagation_rejects_twenty_zero_mass_posteriors) {
    const auto parent = make_two_combo_parent();
    for (std::size_t index = 0; index < 20; ++index) {
        texas::ActionRangeFilter filter;
        filter.action = texas::ACTION_CALL;
        filter.multipliers.assign(parent.range.size(), 0.0);
        EXPECT_THROW(
            texas::propagate_range_to_action(parent, filter),
            std::invalid_argument);
    }
}

TEST_CASE(ranges_action_propagation_rejects_twenty_invalid_multipliers) {
    const auto parent = make_two_combo_parent();
    for (std::size_t index = 0; index < 20; ++index) {
        texas::ActionRangeFilter filter;
        filter.action = texas::ACTION_CALL;
        filter.multipliers.assign(parent.range.size(), 1.0);
        filter.multipliers[index % filter.multipliers.size()] =
            index % 3U == 0U
                ? -0.1
                : (index % 3U == 1U
                    ? std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::quiet_NaN());
        EXPECT_THROW(
            texas::propagate_range_to_action(parent, filter),
            std::invalid_argument);
    }
}

TEST_CASE(ranges_fold_branch_removes_dead_combos) {
    const auto combos = texas::enumerate_combos({
        texas::card_to_int(14, 0), texas::card_to_int(13, 1), texas::card_to_int(12, 2)});
    const auto parent = make_two_combo_parent();

    texas::RangeMask surviving_only;
    surviving_only.kind = texas::RangeVector::Kind::Combo;
    surviving_only.enabled.assign(combos.size(), 0U);
    surviving_only.enabled[1] = 1U;

    const auto folded = texas::propagate_range_to_action(
        parent,
        texas::ActionRangeFilter{texas::ACTION_FOLD, surviving_only, {}});

    EXPECT_NEAR(folded.range.weights[0], 0.0, 1e-12);
    EXPECT_NEAR(folded.range.weights[1], 1.0, 1e-12);
}

TEST_CASE(ranges_chance_nodes_update_legal_combos_correctly) {
    const std::vector<std::uint8_t> board = {
        texas::card_to_int(14, 0), texas::card_to_int(13, 1), texas::card_to_int(12, 2)};
    const auto combos = texas::enumerate_combos(board);
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto parent = texas::make_canonical_range_from_values(texas::RangeSourceKind::UniformPrior, range, mask);

    const auto dealt = texas::card_to_int(2, 0);
    const auto child = texas::propagate_range_to_chance_card(parent, combos, dealt, 1.0);

    for (std::size_t i = 0; i < combos.hands.size(); ++i) {
        if (combos.hands[i][0] == dealt || combos.hands[i][1] == dealt) {
            EXPECT_NEAR(child.range.range.weights[i], 0.0, 1e-12);
        }
    }
    EXPECT_NEAR(child.range.range.sum(), 1.0, 1e-12);
}

TEST_CASE(ranges_chance_propagation_rejects_twenty_invalid_probabilities) {
    const std::vector<std::uint8_t> board = {
        texas::card_to_int(14, 0),
        texas::card_to_int(13, 1),
        texas::card_to_int(12, 2),
    };
    const auto combos = texas::enumerate_combos(board);
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto parent = texas::make_canonical_range_from_values(
        texas::RangeSourceKind::UniformPrior, range, mask);

    for (std::size_t index = 0; index < 20; ++index) {
        const auto probability = index % 4U == 0U
            ? -0.1
            : (index % 4U == 1U
                ? 1.1
                : (index % 4U == 2U
                    ? std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::quiet_NaN()));
        EXPECT_THROW(
            texas::propagate_range_to_chance_card(
                parent, combos, texas::card_to_int(2, 0), probability),
            std::invalid_argument);
    }
}

}  // namespace
