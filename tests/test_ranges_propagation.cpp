#include "games/hunl.hpp"
#include "ranges/propagation.hpp"
#include "ranges/source.hpp"
#include "test_harness.hpp"

#include <vector>

namespace {

core::CanonicalRange make_two_combo_parent() {
    const auto combos = core::enumerate_combos({
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)});
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 0.0);
    range.weights[0] = 0.4;
    range.weights[1] = 0.6;
    range.normalize();
    core::RangeMask mask;
    mask.kind = core::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    return core::make_canonical_range_from_values(core::RangeSourceKind::UniformPrior, range, mask);
}

TEST_CASE(ranges_parent_splits_correctly_across_actions) {
    const auto combos = core::enumerate_combos({
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)});
    const auto parent = make_two_combo_parent();

    core::RangeMask first_only;
    first_only.kind = core::RangeVector::Kind::Combo;
    first_only.enabled.assign(combos.size(), 0U);
    first_only.enabled[0] = 1U;

    core::RangeMask second_only;
    second_only.kind = core::RangeVector::Kind::Combo;
    second_only.enabled.assign(combos.size(), 0U);
    second_only.enabled[1] = 1U;

    const auto children = core::propagate_range_to_actions(parent, {
        core::ActionRangeFilter{core::ACTION_CHECK, first_only, {}},
        core::ActionRangeFilter{core::ACTION_BET_75, second_only, {}},
    });

    EXPECT_EQ(children.size(), 2U);
    EXPECT_NEAR(children[0].range.range.weights[0], 1.0, 1e-12);
    EXPECT_NEAR(children[0].range.range.weights[1], 0.0, 1e-12);
    EXPECT_NEAR(children[1].range.range.weights[0], 0.0, 1e-12);
    EXPECT_NEAR(children[1].range.range.weights[1], 1.0, 1e-12);
}

TEST_CASE(ranges_child_ranges_preserve_total_mass_after_renormalization) {
    const auto parent = make_two_combo_parent();
    core::ActionRangeFilter filter;
    filter.action = core::ACTION_CALL;
    filter.multipliers = std::vector<double>(parent.range.size(), 0.5);

    const auto child = core::propagate_range_to_action(parent, filter);
    EXPECT_NEAR(child.range.sum(), 1.0, 1e-12);
}

TEST_CASE(ranges_fold_branch_removes_dead_combos) {
    const auto combos = core::enumerate_combos({
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)});
    const auto parent = make_two_combo_parent();

    core::RangeMask surviving_only;
    surviving_only.kind = core::RangeVector::Kind::Combo;
    surviving_only.enabled.assign(combos.size(), 0U);
    surviving_only.enabled[1] = 1U;

    const auto folded = core::propagate_range_to_action(
        parent,
        core::ActionRangeFilter{core::ACTION_FOLD, surviving_only, {}});

    EXPECT_NEAR(folded.range.weights[0], 0.0, 1e-12);
    EXPECT_NEAR(folded.range.weights[1], 1.0, 1e-12);
}

TEST_CASE(ranges_chance_nodes_update_legal_combos_correctly) {
    const std::vector<std::uint8_t> board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)};
    const auto combos = core::enumerate_combos(board);
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    core::RangeMask mask;
    mask.kind = core::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto parent = core::make_canonical_range_from_values(core::RangeSourceKind::UniformPrior, range, mask);

    const auto dealt = core::card_to_int(2, 0);
    const auto child = core::propagate_range_to_chance_card(parent, combos, dealt, 1.0);

    for (std::size_t i = 0; i < combos.hands.size(); ++i) {
        if (combos.hands[i][0] == dealt || combos.hands[i][1] == dealt) {
            EXPECT_NEAR(child.range.range.weights[i], 0.0, 1e-12);
        }
    }
    EXPECT_NEAR(child.range.range.sum(), 1.0, 1e-12);
}

}  // namespace
