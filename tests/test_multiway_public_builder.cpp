#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_public_builder.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

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
    const auto root = core::MultiwayPublicBuilder::make_root(
        snapshot, {core::card_to_int(2U, 0U), core::card_to_int(7U, 1U), core::card_to_int(9U, 2U)}, root_actions);

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

TEST_CASE(multiway_public_builder_schema_v2_canonicalizes_menu_and_root_identity) {
    const auto snapshot = root_snapshot();
    const std::vector<core::MultiwayActionDescriptor> first_input = {
        {core::MultiwayAction::AllIn, 13U, 1'000, 91U},
        {core::MultiwayAction::Bet, 11U, 250, 92U},
        {core::MultiwayAction::Check, 12U, 0, 93U},
        {core::MultiwayAction::Bet, 14U, 250, 94U},
    };
    const std::vector<core::MultiwayActionDescriptor> second_input = {
        {core::MultiwayAction::Check, 0U, 0, 0U},
        {core::MultiwayAction::Bet, 1U, 250, 0U},
        {core::MultiwayAction::AllIn, 2U, 1'000, 0U},
    };

    const auto first_menu = core::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, first_input);
    const auto second_menu = core::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, second_input);
    EXPECT_EQ(first_menu, second_menu);
    EXPECT_TRUE(first_menu.front().action_menu_id != 0U);
    for (std::size_t index = 0U; index < first_menu.size(); ++index) {
        EXPECT_EQ(first_menu[index].action_index, static_cast<std::uint32_t>(index));
        EXPECT_EQ(first_menu[index].action_menu_id, first_menu.front().action_menu_id);
    }

    auto changed_input = second_input;
    changed_input[1].target_street_contribution = 300;
    const auto changed_menu = core::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, changed_input);
    EXPECT_TRUE(changed_menu.front().action_menu_id != first_menu.front().action_menu_id);

    const std::vector<std::uint8_t> first_board = {
        core::card_to_int(2U, 0U), core::card_to_int(7U, 1U), core::card_to_int(9U, 2U)};
    const std::vector<std::uint8_t> permuted_board = {
        core::card_to_int(9U, 2U), core::card_to_int(2U, 0U), core::card_to_int(7U, 1U)};
    const auto first_root = core::MultiwayPublicBuilder::make_root(snapshot, first_board, first_menu);
    const auto second_root = core::MultiwayPublicBuilder::make_root(snapshot, permuted_board, second_menu);
    EXPECT_EQ(first_root.board, second_root.board);
    EXPECT_EQ(first_root.canonical_history_id, second_root.canonical_history_id);
    EXPECT_EQ(first_root.id, second_root.id);
}

TEST_CASE(multiway_public_builder_schema_v2_changes_menu_id_for_off_tree_action) {
    const auto snapshot = root_snapshot();
    const core::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(snapshot, 17U);
    const auto expanded = core::MultiwayActionAbstraction::insert_exact_observed_action(
        snapshot, menu, core::MultiwayAction::Bet, 501, 0U);

    EXPECT_TRUE(menu.front().action_menu_id != expanded.front().action_menu_id);
    EXPECT_TRUE(std::any_of(expanded.begin(), expanded.end(), [](const auto& action) {
        return action.action == core::MultiwayAction::Bet && action.target_street_contribution == 501;
    }));
}

TEST_CASE(multiway_public_builder_rejects_mismatched_action_targets) {
    const auto snapshot = root_snapshot();
    EXPECT_THROW(
        core::MultiwayPublicBuilder::make_legal_actions(snapshot, 17, {0, 0}),
        std::invalid_argument);
}
