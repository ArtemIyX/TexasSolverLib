#include "preflop/preflop.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

texas::HUNLConfig base_config() {
    texas::HUNLConfig config;
    config.starting_stack = 1'000;
    config.small_blind = 50;
    config.big_blind = 100;
    config.ante = 0;
    config.starting_street = texas::Street::Preflop;
    config.initial_board.clear();
    config.initial_pot = 0;
    config.initial_contributions = {0, 0};
    config.initial_hole_cards = std::nullopt;
    config.preflop_raise_cap = 2;
    config.bet_size_fractions = {0.5};
    config.raise_size_xs = {2.0};
    config.include_all_in = false;
    config.auto_all_in_spr_threshold = std::nullopt;
    config.force_allin_threshold = 0;
    return config;
}

const texas::PreflopBettingTree::Node& root_of(
    const texas::PreflopBettingTree& tree) {
    return tree.nodes.at(0);
}

bool has_action(
    const texas::PreflopBettingTree::Node& node,
    const std::string& action) {
    return std::find(node.actions.begin(), node.actions.end(), action) !=
        node.actions.end();
}

std::size_t child_for(
    const texas::PreflopBettingTree& tree,
    std::size_t node_index,
    const std::string& action) {
    const auto& node = tree.nodes.at(node_index);
    const auto it = std::find(node.actions.begin(), node.actions.end(), action);
    if (it == node.actions.end()) {
        throw test::Failure("requested preflop action is absent");
    }
    return node.children.at(
        static_cast<std::size_t>(std::distance(node.actions.begin(), it)));
}

std::size_t count_action_prefix(
    const texas::PreflopBettingTree::Node& node,
    char prefix) {
    return static_cast<std::size_t>(std::count_if(
        node.actions.begin(),
        node.actions.end(),
        [prefix](const std::string& action) {
            return !action.empty() && action.front() == prefix;
        }));
}

}  // namespace

TEST_CASE(preflop_tree_uses_single_configured_raise_size) {
    auto config = base_config();
    config.raise_size_xs = {2.5};
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(has_action(root_of(tree), "r250"));
    EXPECT_TRUE(!has_action(root_of(tree), "r300"));
}

TEST_CASE(preflop_tree_preserves_configured_raise_menu_order) {
    auto config = base_config();
    config.raise_size_xs = {3.0, 2.0};
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto& root = root_of(tree);
    EXPECT_EQ(root.actions.at(2), std::string("r300"));
    EXPECT_EQ(root.actions.at(3), std::string("r200"));
}

TEST_CASE(preflop_tree_deduplicates_clamped_minimum_raises) {
    auto config = base_config();
    config.raise_size_xs = {0.0, 1.0, 2.0};
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto& root = root_of(tree);
    EXPECT_EQ(count_action_prefix(root, 'r'), 1U);
    EXPECT_TRUE(has_action(root, "r200"));
}

TEST_CASE(preflop_tree_skips_menu_raise_equal_to_all_in) {
    auto config = base_config();
    config.raise_size_xs = {10.0};
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_EQ(count_action_prefix(root_of(tree), 'r'), 0U);
}

TEST_CASE(preflop_tree_empty_raise_menu_has_only_passive_root_actions) {
    auto config = base_config();
    config.raise_size_xs.clear();
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_EQ(root_of(tree).actions.size(), 2U);
    EXPECT_TRUE(has_action(root_of(tree), "f"));
    EXPECT_TRUE(has_action(root_of(tree), "c"));
}

TEST_CASE(preflop_tree_raise_cap_zero_suppresses_aggression) {
    auto config = base_config();
    config.preflop_raise_cap = 0;
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_EQ(root_of(tree).actions.size(), 2U);
}

TEST_CASE(preflop_tree_raise_cap_one_suppresses_aggression) {
    auto config = base_config();
    config.preflop_raise_cap = 1;
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_EQ(root_of(tree).actions.size(), 2U);
}

TEST_CASE(preflop_tree_raise_cap_two_allows_one_raise_level) {
    auto config = base_config();
    config.preflop_raise_cap = 2;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto raise_child = child_for(tree, 0U, "r200");
    EXPECT_EQ(count_action_prefix(tree.nodes.at(raise_child), 'r'), 0U);
}

TEST_CASE(preflop_tree_raise_cap_three_allows_reraise) {
    auto config = base_config();
    config.preflop_raise_cap = 3;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto raise_child = child_for(tree, 0U, "r200");
    EXPECT_TRUE(has_action(tree.nodes.at(raise_child), "r400"));
}

TEST_CASE(preflop_tree_root_raise_uses_raise_token) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    EXPECT_TRUE(has_action(root_of(tree), "r200"));
    EXPECT_EQ(count_action_prefix(root_of(tree), 'b'), 0U);
}

TEST_CASE(preflop_tree_limp_gives_big_blind_check_option) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    const auto limp_child = child_for(tree, 0U, "c");
    EXPECT_TRUE(has_action(tree.nodes.at(limp_child), "x"));
}

TEST_CASE(preflop_tree_limp_uses_configured_bet_menu) {
    auto config = base_config();
    config.bet_size_fractions = {0.5};
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto limp_child = child_for(tree, 0U, "c");
    EXPECT_TRUE(has_action(tree.nodes.at(limp_child), "b100"));
}

TEST_CASE(preflop_tree_big_blind_check_ends_preflop) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    const auto limp_child = child_for(tree, 0U, "c");
    const auto check_child = child_for(tree, limp_child, "x");
    EXPECT_EQ(
        tree.nodes.at(check_child).kind,
        texas::PreflopBettingTree::NodeKind::EquityLeaf);
}

TEST_CASE(preflop_tree_raise_call_ends_preflop) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    const auto raise_child = child_for(tree, 0U, "r200");
    const auto call_child = child_for(tree, raise_child, "c");
    EXPECT_EQ(
        tree.nodes.at(call_child).kind,
        texas::PreflopBettingTree::NodeKind::EquityLeaf);
}

TEST_CASE(preflop_tree_raise_fold_is_fold_leaf) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    const auto raise_child = child_for(tree, 0U, "r200");
    const auto fold_child = child_for(tree, raise_child, "f");
    EXPECT_EQ(
        tree.nodes.at(fold_child).kind,
        texas::PreflopBettingTree::NodeKind::Fold);
}

TEST_CASE(preflop_tree_include_all_in_adds_root_action) {
    auto config = base_config();
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(has_action(root_of(tree), "A"));
}

TEST_CASE(preflop_tree_exclude_all_in_removes_root_action) {
    auto config = base_config();
    config.include_all_in = false;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(!has_action(root_of(tree), "A"));
}

TEST_CASE(preflop_tree_auto_all_in_threshold_can_add_action) {
    auto config = base_config();
    config.include_all_in = false;
    config.auto_all_in_spr_threshold = 10.0;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(has_action(root_of(tree), "A"));
}

TEST_CASE(preflop_tree_low_auto_all_in_threshold_does_not_add_action) {
    auto config = base_config();
    config.include_all_in = false;
    config.auto_all_in_spr_threshold = 1.0;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(!has_action(root_of(tree), "A"));
}

TEST_CASE(preflop_tree_all_in_opponent_can_only_fold_or_call) {
    auto config = base_config();
    config.include_all_in = true;
    config.preflop_raise_cap = 4;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto all_in_child = child_for(tree, 0U, "A");
    const auto& response = tree.nodes.at(all_in_child);
    EXPECT_EQ(response.actions.size(), 2U);
    EXPECT_TRUE(has_action(response, "f"));
    EXPECT_TRUE(has_action(response, "c"));
}

TEST_CASE(preflop_tree_all_in_call_has_matched_contributions) {
    auto config = base_config();
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto all_in_child = child_for(tree, 0U, "A");
    const auto call_child = child_for(tree, all_in_child, "c");
    const auto& leaf = tree.nodes.at(call_child);
    EXPECT_EQ(leaf.contributions[0], 1'000);
    EXPECT_EQ(leaf.contributions[1], 1'000);
}

TEST_CASE(preflop_tree_forced_big_blind_all_in_has_no_aggression) {
    auto config = base_config();
    config.starting_stack = 100;
    config.include_all_in = true;
    config.preflop_raise_cap = 4;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto& root = root_of(tree);
    EXPECT_EQ(root.actions.size(), 2U);
    EXPECT_TRUE(has_action(root, "f"));
    EXPECT_TRUE(has_action(root, "c"));
}

TEST_CASE(preflop_tree_forced_big_blind_all_in_call_is_equity_leaf) {
    auto config = base_config();
    config.starting_stack = 100;
    const auto tree = texas::PreflopBettingTree::build(config);
    const auto call_child = child_for(tree, 0U, "c");
    EXPECT_EQ(
        tree.nodes.at(call_child).kind,
        texas::PreflopBettingTree::NodeKind::EquityLeaf);
}

TEST_CASE(preflop_tree_force_threshold_replaces_near_all_in_raise) {
    auto config = base_config();
    config.starting_stack = 500;
    config.raise_size_xs = {4.0};
    config.force_allin_threshold = 1;
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(!has_action(root_of(tree), "r400"));
    EXPECT_TRUE(has_action(root_of(tree), "A"));
}

TEST_CASE(preflop_tree_ante_changes_raise_target_consistently) {
    auto config = base_config();
    config.ante = 10;
    config.raise_size_xs = {2.0};
    const auto tree = texas::PreflopBettingTree::build(config);
    EXPECT_TRUE(has_action(root_of(tree), "r220"));
}

TEST_CASE(preflop_tree_decisions_never_have_empty_action_menus) {
    auto config = base_config();
    config.include_all_in = true;
    config.preflop_raise_cap = 4;
    const auto tree = texas::PreflopBettingTree::build(config);
    for (const auto& node : tree.nodes) {
        if (node.kind == texas::PreflopBettingTree::NodeKind::Decision) {
            EXPECT_TRUE(!node.actions.empty());
        }
    }
}

TEST_CASE(preflop_tree_actions_and_children_are_aligned) {
    auto config = base_config();
    config.raise_size_xs = {2.0, 3.0};
    config.include_all_in = true;
    const auto tree = texas::PreflopBettingTree::build(config);
    for (const auto& node : tree.nodes) {
        if (node.kind == texas::PreflopBettingTree::NodeKind::Decision) {
            EXPECT_EQ(node.actions.size(), node.children.size());
        }
    }
}

TEST_CASE(preflop_tree_root_key_uses_empty_history) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    EXPECT_EQ(root_of(tree).key_suffix, std::string("||p|"));
}

TEST_CASE(preflop_tree_child_key_uses_applied_raise_history) {
    const auto tree = texas::PreflopBettingTree::build(base_config());
    const auto raise_child = child_for(tree, 0U, "r200");
    EXPECT_EQ(
        tree.nodes.at(raise_child).key_suffix,
        std::string("||p|r200"));
}

TEST_CASE(preflop_tree_rejects_non_preflop_config) {
    auto config = base_config();
    config.starting_street = texas::Street::Flop;
    config.initial_board = {
        texas::card_to_int(14, 0),
        texas::card_to_int(13, 1),
        texas::card_to_int(12, 2),
    };
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}

TEST_CASE(preflop_tree_rejects_resolved_private_cards) {
    auto config = base_config();
    config.initial_hole_cards =
        std::array<std::array<std::uint8_t, 2>, 2>{{
            {texas::card_to_int(14, 0), texas::card_to_int(14, 1)},
            {texas::card_to_int(13, 2), texas::card_to_int(13, 3)},
        }};
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}

TEST_CASE(preflop_tree_rejects_invalid_stack) {
    auto config = base_config();
    config.starting_stack = 0;
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}

TEST_CASE(preflop_tree_rejects_nonfinite_raise_size) {
    auto config = base_config();
    config.raise_size_xs = {
        std::numeric_limits<double>::quiet_NaN()};
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}

TEST_CASE(preflop_tree_rejects_oversized_raise_menu) {
    auto config = base_config();
    config.raise_size_xs = std::vector<double>(6U, 2.0);
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}

TEST_CASE(preflop_tree_rejects_invalid_preflop_contributions) {
    auto config = base_config();
    config.initial_contributions = {75, 100};
    config.initial_pot = 175;
    EXPECT_THROW(
        texas::PreflopBettingTree::build(config),
        std::invalid_argument);
}
