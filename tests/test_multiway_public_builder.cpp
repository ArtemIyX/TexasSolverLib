#include "solver/multiway_public_builder.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

core::MultiwayBettingSnapshot root_snapshot() {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = core::Street::Flop;
    return core::MultiwayState::initial(config).snapshot();
}

}  // namespace

TEST_CASE(multiway_public_builder_creates_replayable_action_child) {
    const auto snapshot = root_snapshot();
    const auto root_actions = core::MultiwayPublicBuilder::make_legal_actions(
        snapshot, 17, {0, 250, 0});
    const auto root = core::MultiwayPublicBuilder::make_root(snapshot, {0, 1, 2}, root_actions);

    const auto child_snapshot = core::MultiwayState::from_snapshot(root.betting)
        .apply(root_actions[1].action, root_actions[1].target_street_contribution)
        .snapshot();
    const auto child_actions = core::MultiwayPublicBuilder::make_legal_actions(
        child_snapshot, 18, {0, 0, 500, 0});
    const auto child = core::MultiwayPublicBuilder::make_action_child(root, 1, child_actions);

    EXPECT_EQ(child.parent_id, root.id);
    EXPECT_EQ(child.history.size(), 1U);
    EXPECT_EQ(child.history[0].actor, 0);
    EXPECT_EQ(child.betting.current_bet, 250);
    EXPECT_TRUE(child.id != root.id);
    EXPECT_TRUE(child.canonical_history_id != root.canonical_history_id);
}

TEST_CASE(multiway_public_builder_rejects_mismatched_action_targets) {
    const auto snapshot = root_snapshot();
    EXPECT_THROW(
        core::MultiwayPublicBuilder::make_legal_actions(snapshot, 17, {0, 0}),
        std::invalid_argument);
}
