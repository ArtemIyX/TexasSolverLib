#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_public_builder.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

texas::MultiwayBettingSnapshot root_snapshot() {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = texas::Street::Flop;
    return texas::MultiwayState::initial(config).snapshot();
}

}  // namespace

TEST_CASE(multiway_public_builder_creates_replayable_action_child) {
    const auto snapshot = root_snapshot();
    const auto root_actions = texas::MultiwayPublicBuilder::make_legal_actions(
        snapshot, {0, 250, 0});
    const auto root = texas::MultiwayPublicBuilder::make_root(
        snapshot, {texas::card_to_int(2U, 0U), texas::card_to_int(7U, 1U), texas::card_to_int(9U, 2U)}, root_actions);

    const auto child_snapshot = texas::MultiwayState::from_snapshot(root.betting)
        .apply(root_actions[1].action, root_actions[1].target_street_contribution)
        .snapshot();
    const auto child_actions = texas::MultiwayPublicBuilder::make_legal_actions(
        child_snapshot, {0, 0, 500, 0});
    const auto child = texas::MultiwayPublicBuilder::make_action_child(root, 1, child_actions);
    const auto fixed_child = texas::MultiwayPublicBuilder::make_action_child(
        root, 1, child_snapshot, child_actions);

    EXPECT_EQ(child.parent_id, root.id);
    EXPECT_EQ(child.history.size(), 1U);
    EXPECT_EQ(child.history[0].actor, 0);
    EXPECT_EQ(child.betting.current_bet, 250);
    EXPECT_TRUE(child.id != root.id);
    EXPECT_TRUE(child.canonical_history_id != root.canonical_history_id);
    EXPECT_EQ(fixed_child.id, child.id);
    EXPECT_EQ(fixed_child.betting.current_bet, child.betting.current_bet);
}

TEST_CASE(multiway_public_builder_schema_v2_canonicalizes_menu_and_root_identity) {
    const auto snapshot = root_snapshot();
    const std::vector<texas::MultiwayActionDescriptor> first_input = {
        {texas::MultiwayAction::AllIn, 13U, 1'000, 91U},
        {texas::MultiwayAction::Bet, 11U, 250, 92U},
        {texas::MultiwayAction::Check, 12U, 0, 93U},
        {texas::MultiwayAction::Bet, 14U, 250, 94U},
    };
    const std::vector<texas::MultiwayActionDescriptor> second_input = {
        {texas::MultiwayAction::Check, 0U, 0, 0U},
        {texas::MultiwayAction::Bet, 1U, 250, 0U},
        {texas::MultiwayAction::AllIn, 2U, 1'000, 0U},
    };

    const auto first_menu = texas::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, first_input);
    const auto second_menu = texas::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, second_input);
    EXPECT_EQ(first_menu, second_menu);
    EXPECT_TRUE(first_menu.front().action_menu_id != 0U);
    for (std::size_t index = 0U; index < first_menu.size(); ++index) {
        EXPECT_EQ(first_menu[index].action_index, static_cast<std::uint32_t>(index));
        EXPECT_EQ(first_menu[index].action_menu_id, first_menu.front().action_menu_id);
    }

    auto changed_input = second_input;
    changed_input[1].target_street_contribution = 300;
    const auto changed_menu = texas::MultiwayPublicBuilder::canonicalize_action_menu(snapshot, changed_input);
    EXPECT_TRUE(changed_menu.front().action_menu_id != first_menu.front().action_menu_id);

    const std::vector<std::uint8_t> first_board = {
        texas::card_to_int(2U, 0U), texas::card_to_int(7U, 1U), texas::card_to_int(9U, 2U)};
    const std::vector<std::uint8_t> permuted_board = {
        texas::card_to_int(9U, 2U), texas::card_to_int(2U, 0U), texas::card_to_int(7U, 1U)};
    const auto first_root = texas::MultiwayPublicBuilder::make_root(snapshot, first_board, first_menu);
    const auto second_root = texas::MultiwayPublicBuilder::make_root(snapshot, permuted_board, second_menu);
    EXPECT_EQ(first_root.board, second_root.board);
    EXPECT_EQ(first_root.canonical_history_id, second_root.canonical_history_id);
    EXPECT_EQ(first_root.id, second_root.id);
}

TEST_CASE(multiway_public_builder_schema_v2_changes_menu_id_for_off_tree_action) {
    const auto snapshot = root_snapshot();
    const texas::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(snapshot);
    const auto expanded = texas::MultiwayActionAbstraction::insert_exact_observed_action(
        snapshot, menu, texas::MultiwayAction::Bet, 501);

    EXPECT_TRUE(menu.front().action_menu_id != expanded.front().action_menu_id);
    EXPECT_TRUE(std::any_of(expanded.begin(), expanded.end(), [](const auto& action) {
        return action.action == texas::MultiwayAction::Bet && action.target_street_contribution == 501;
    }));
}

TEST_CASE(multiway_public_builder_lossless_current_round_key_tracks_exact_targets) {
    const auto snapshot = root_snapshot();
    const texas::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(snapshot);
    const auto root = texas::MultiwayPublicBuilder::make_root(snapshot, {8U, 13U, 18U}, menu);
    const auto expanded = texas::MultiwayActionAbstraction::insert_exact_observed_action(
        snapshot, menu, texas::MultiwayAction::Bet, 501);
    const auto expanded_root = texas::MultiwayPublicBuilder::make_root(snapshot, {8U, 13U, 18U}, expanded);

    EXPECT_TRUE(root.id != expanded_root.id);
    EXPECT_EQ(
        expanded_root.id.value,
        texas::MultiwayPublicBuilder::stable_lossless_current_round_key(
            expanded_root.betting, expanded_root.board, expanded_root.history, expanded_root.legal_actions));
}

TEST_CASE(multiway_public_builder_rejects_mismatched_action_targets) {
    const auto snapshot = root_snapshot();
    EXPECT_THROW(
        texas::MultiwayPublicBuilder::make_legal_actions(snapshot, {0, 0}),
        std::invalid_argument);
}
