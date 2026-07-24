#include "preflop/preflop_rvr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double TOL = 1e-12;
constexpr std::size_t PAIR_COUNT =
    core::PREFLOP_NUM_CLASSES * core::PREFLOP_NUM_CLASSES;

std::vector<double> valid_reach() {
    return std::vector<double>(core::PREFLOP_NUM_CLASSES, 1.0);
}

core::PreflopBettingTree valid_one_decision_tree() {
    core::PreflopBettingTree tree;
    tree.nodes.resize(3U);
    tree.nodes[0].kind =
        core::PreflopBettingTree::NodeKind::Decision;
    tree.nodes[0].player = 0U;
    tree.nodes[0].actions = {"f", "c"};
    tree.nodes[0].children = {1U, 2U};
    tree.nodes[0].key_suffix = "||p|";

    tree.nodes[1].kind = core::PreflopBettingTree::NodeKind::Fold;
    tree.nodes[1].folded_player = 0U;
    tree.nodes[2].kind = core::PreflopBettingTree::NodeKind::Fold;
    tree.nodes[2].folded_player = 1U;
    return tree;
}

core::Class169TerminalCache valid_fold_cache(std::size_t node_count = 3U) {
    core::Class169TerminalCache cache;
    cache.leaves.resize(node_count);
    cache.leaves[0].kind =
        core::Class169LeafEntry::Kind::NonTerminal;
    for (std::size_t node = 1; node < node_count; ++node) {
        cache.leaves[node].kind = core::Class169LeafEntry::Kind::Fold;
        cache.leaves[node].payoff = {-1.0, 1.0};
    }
    cache.shared_blocker_mass = {
        std::vector<double>(PAIR_COUNT, 1.0),
        std::vector<double>(PAIR_COUNT, 1.0),
    };
    return cache;
}

core::PreflopBettingTree valid_two_decision_tree() {
    core::PreflopBettingTree tree;
    tree.nodes.resize(4U);
    tree.nodes[0].kind =
        core::PreflopBettingTree::NodeKind::Decision;
    tree.nodes[0].player = 0U;
    tree.nodes[0].actions = {"c"};
    tree.nodes[0].children = {1U};
    tree.nodes[0].key_suffix = "||p|";

    tree.nodes[1].kind =
        core::PreflopBettingTree::NodeKind::Decision;
    tree.nodes[1].player = 1U;
    tree.nodes[1].actions = {"x", "A"};
    tree.nodes[1].children = {2U, 3U};
    tree.nodes[1].key_suffix = "||p|c";

    tree.nodes[2].kind = core::PreflopBettingTree::NodeKind::Fold;
    tree.nodes[2].folded_player = 0U;
    tree.nodes[3].kind = core::PreflopBettingTree::NodeKind::Fold;
    tree.nodes[3].folded_player = 1U;
    return tree;
}

core::Class169TerminalCache valid_two_decision_cache() {
    auto cache = valid_fold_cache(4U);
    cache.leaves[1].kind =
        core::Class169LeafEntry::Kind::NonTerminal;
    return cache;
}

void expect_invalid_solve(
    const core::PreflopBettingTree& tree,
    const core::Class169TerminalCache& cache,
    const std::vector<double>& reach0,
    const std::vector<double>& reach1) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    EXPECT_THROW(
        solver.solve(tree, cache, 1U, reach0, reach1),
        std::invalid_argument);
    EXPECT_EQ(solver.iteration(), 0U);
    EXPECT_TRUE(solver.average_strategy().empty());
}

}  // namespace

TEST_CASE(class169_constructor_rejects_zero_hands) {
    EXPECT_THROW(
        core::Class169VectorDCFR(0U, 1.5, 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(class169_constructor_rejects_one_hand) {
    EXPECT_THROW(
        core::Class169VectorDCFR(1U, 1.5, 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(class169_constructor_rejects_168_hands) {
    EXPECT_THROW(
        core::Class169VectorDCFR(168U, 1.5, 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(class169_constructor_rejects_170_hands) {
    EXPECT_THROW(
        core::Class169VectorDCFR(170U, 1.5, 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(class169_constructor_accepts_exact_dimension) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    EXPECT_EQ(solver.iteration(), 0U);
}

TEST_CASE(class169_rejects_empty_player0_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        {},
        valid_reach());
}

TEST_CASE(class169_rejects_short_player0_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        std::vector<double>(168U, 1.0),
        valid_reach());
}

TEST_CASE(class169_rejects_long_player0_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        std::vector<double>(170U, 1.0),
        valid_reach());
}

TEST_CASE(class169_rejects_empty_player1_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        valid_reach(),
        {});
}

TEST_CASE(class169_rejects_short_player1_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        valid_reach(),
        std::vector<double>(168U, 1.0));
}

TEST_CASE(class169_rejects_long_player1_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        valid_reach(),
        std::vector<double>(170U, 1.0));
}

TEST_CASE(class169_rejects_negative_reach) {
    auto reach = valid_reach();
    reach[10] = -1.0;
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        reach,
        valid_reach());
}

TEST_CASE(class169_rejects_nan_reach) {
    auto reach = valid_reach();
    reach[10] = std::numeric_limits<double>::quiet_NaN();
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        valid_reach(),
        reach);
}

TEST_CASE(class169_rejects_infinite_reach) {
    auto reach = valid_reach();
    reach[10] = std::numeric_limits<double>::infinity();
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        reach,
        valid_reach());
}

TEST_CASE(class169_rejects_zero_mass_reach) {
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        std::vector<double>(core::PREFLOP_NUM_CLASSES, 0.0),
        valid_reach());
}

TEST_CASE(class169_rejects_overflowing_reach_total) {
    auto reach = valid_reach();
    reach[0] = std::numeric_limits<double>::max();
    reach[1] = std::numeric_limits<double>::max();
    expect_invalid_solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        reach,
        valid_reach());
}

TEST_CASE(class169_invalid_resolve_preserves_previous_output) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    const auto tree = valid_one_decision_tree();
    const auto cache = valid_fold_cache();
    solver.solve(tree, cache, 1U, valid_reach(), valid_reach());
    const auto before = solver.average_strategy();
    EXPECT_THROW(
        solver.solve(
            tree,
            cache,
            1U,
            std::vector<double>(168U, 1.0),
            valid_reach()),
        std::invalid_argument);
    EXPECT_EQ(solver.iteration(), 1U);
    EXPECT_EQ(solver.average_strategy(), before);
}

TEST_CASE(class169_rejects_empty_tree) {
    expect_invalid_solve(
        {},
        {},
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_cache_size_mismatch) {
    auto cache = valid_fold_cache();
    cache.leaves.pop_back();
    expect_invalid_solve(
        valid_one_decision_tree(),
        cache,
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_empty_decision_actions) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].actions.clear();
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_action_child_mismatch) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].actions.pop_back();
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_invalid_decision_player) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].player = 2U;
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_empty_decision_key) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].key_suffix.clear();
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_out_of_bounds_child) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].children[1] = 99U;
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_cyclic_tree) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].children[1] = 0U;
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_shared_child_dag) {
    auto tree = valid_one_decision_tree();
    tree.nodes[0].children[1] = 1U;
    expect_invalid_solve(
        tree, valid_fold_cache(), valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_unreachable_node) {
    auto tree = valid_one_decision_tree();
    tree.nodes.push_back(tree.nodes[1]);
    auto cache = valid_fold_cache(4U);
    expect_invalid_solve(
        tree, cache, valid_reach(), valid_reach());
}

TEST_CASE(class169_rejects_duplicate_decision_key) {
    auto tree = valid_two_decision_tree();
    tree.nodes[1].key_suffix = tree.nodes[0].key_suffix;
    expect_invalid_solve(
        tree,
        valid_two_decision_cache(),
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_cache_kind_mismatch) {
    auto cache = valid_fold_cache();
    cache.leaves[1].kind =
        core::Class169LeafEntry::Kind::Equity;
    expect_invalid_solve(
        valid_one_decision_tree(),
        cache,
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_short_blocker_table) {
    auto cache = valid_fold_cache();
    cache.shared_blocker_mass[0].pop_back();
    expect_invalid_solve(
        valid_one_decision_tree(),
        cache,
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_nonfinite_blocker_table) {
    auto cache = valid_fold_cache();
    cache.shared_blocker_mass[1][10] =
        std::numeric_limits<double>::quiet_NaN();
    expect_invalid_solve(
        valid_one_decision_tree(),
        cache,
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_rejects_negative_blocker_mass) {
    auto cache = valid_fold_cache();
    cache.shared_blocker_mass[0][10] = -1.0;
    expect_invalid_solve(
        valid_one_decision_tree(),
        cache,
        valid_reach(),
        valid_reach());
}

TEST_CASE(class169_zero_iteration_export_contains_every_class) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        0U,
        valid_reach(),
        valid_reach());
    EXPECT_EQ(
        solver.average_strategy().size(),
        core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(class169_export_uses_canonical_pair_labels) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        0U,
        valid_reach(),
        valid_reach());
    const auto strategy = solver.average_strategy();
    EXPECT_TRUE(strategy.find("AA||p|") != strategy.end());
    EXPECT_TRUE(strategy.find("TT||p|") != strategy.end());
    EXPECT_TRUE(strategy.find("22||p|") != strategy.end());
    EXPECT_TRUE(strategy.find("AKs||p|") != strategy.end());
    EXPECT_TRUE(strategy.find("AKo||p|") != strategy.end());
}

TEST_CASE(class169_export_rows_match_action_count) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        0U,
        valid_reach(),
        valid_reach());
    for (const auto& [key, row] : solver.average_strategy()) {
        (void)key;
        EXPECT_EQ(row.size(), 2U);
        EXPECT_NEAR(row[0], 0.5, TOL);
        EXPECT_NEAR(row[1], 0.5, TOL);
    }
}

TEST_CASE(class169_export_contains_every_decision_and_class) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_two_decision_tree(),
        valid_two_decision_cache(),
        0U,
        valid_reach(),
        valid_reach());
    EXPECT_EQ(
        solver.average_strategy().size(),
        2U * core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(class169_export_preserves_each_public_history) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_two_decision_tree(),
        valid_two_decision_cache(),
        0U,
        valid_reach(),
        valid_reach());
    const auto strategy = solver.average_strategy();
    EXPECT_TRUE(strategy.find("AA||p|") != strategy.end());
    EXPECT_TRUE(strategy.find("AA||p|c") != strategy.end());
    EXPECT_EQ(strategy.at("AA||p|").size(), 1U);
    EXPECT_EQ(strategy.at("AA||p|c").size(), 2U);
}

TEST_CASE(class169_positive_iteration_export_is_normalized) {
    core::Class169VectorDCFR solver(
        core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    solver.solve(
        valid_one_decision_tree(),
        valid_fold_cache(),
        1U,
        valid_reach(),
        valid_reach());
    for (const auto& [key, row] : solver.average_strategy()) {
        (void)key;
        EXPECT_NEAR(row[0] + row[1], 1.0, TOL);
    }
}
