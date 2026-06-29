#include "solver/hunl_flat_dcfr.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>

TEST_CASE(hunl_flat_dcfr_runs_explicit_stage_iteration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iterations(2);

    EXPECT_EQ(solver.iterations(), 2U);
    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().reach_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().terminal_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().backward_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().regret_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().average_strategy_seconds >= 0.0);
}

TEST_CASE(hunl_flat_dcfr_strategy_stage_writes_normalized_rows) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    for (const auto& meta : table.meta()) {
        auto* regret = table.regret_mut(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            regret[i] = 0.0;
        }
        if (meta.action_count >= 2 && meta.hand_count >= 1) {
            regret[0] = 2.0;
            regret[meta.hand_count] = 1.0;
        }
    }

    solver.run_iteration();

    for (const auto& meta : table.meta()) {
        const auto* strategy = table.current_strategy(meta.id);
        for (std::size_t h = 0; h < meta.hand_count; ++h) {
            double sum = 0.0;
            for (std::size_t a = 0; a < meta.action_count; ++a) {
                const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                EXPECT_TRUE(strategy[idx] >= 0.0);
                sum += strategy[idx];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_dcfr_exports_average_strategy_by_infoset_key) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();
    const auto exported = solver.export_average_strategy();

    EXPECT_EQ(exported.size(), graph.infosets.size());
    for (const auto& infoset : graph.infosets) {
        const auto it = exported.find(infoset.key);
        EXPECT_TRUE(it != exported.end());
        EXPECT_EQ(it->second.size(),
                  static_cast<std::size_t>(infoset.action_count) *
                      solver.infoset_table().meta()[infoset.id.value].hand_count);
    }
}

TEST_CASE(hunl_flat_dcfr_forward_reach_initializes_root_reaches) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    EXPECT_NEAR(solver.player0_reach()[graph.root], 1.0, 1e-12);
    EXPECT_NEAR(solver.player1_reach()[graph.root], 1.0, 1e-12);
    EXPECT_NEAR(solver.chance_reach()[graph.root], 1.0, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_forward_reach_propagates_strategy_on_decision_nodes) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    const auto root_idx = graph.root;
    const auto& root_meta = graph.node_meta[root_idx];
    EXPECT_EQ(root_meta.type, core::HUNLFlatNodeType::Decision);
    EXPECT_TRUE(root_meta.child_count >= 2);

    auto& table = solver.infoset_table_mut();
    const auto infoset_id = root_meta.infoset_id;
    auto* regret = table.regret_mut(infoset_id);
    for (std::size_t i = 0; i < table.row_value_count(infoset_id); ++i) {
        regret[i] = 0.0;
    }
    const auto hand_count = table.meta()[infoset_id.value].hand_count;
    regret[0] = 3.0;
    regret[hand_count] = 1.0;

    solver.run_iteration();

    const auto child0 = graph.children[root_meta.child_begin];
    const auto child1 = graph.children[root_meta.child_begin + 1];
    if (root_meta.player == 0) {
        EXPECT_TRUE(solver.player0_reach()[child0] > solver.player0_reach()[child1]);
        EXPECT_NEAR(solver.player1_reach()[child0], 1.0, 1e-12);
        EXPECT_NEAR(solver.player1_reach()[child1], 1.0, 1e-12);
    } else {
        EXPECT_TRUE(solver.player1_reach()[child0] > solver.player1_reach()[child1]);
        EXPECT_NEAR(solver.player0_reach()[child0], 1.0, 1e-12);
        EXPECT_NEAR(solver.player0_reach()[child1], 1.0, 1e-12);
    }
    EXPECT_NEAR(solver.chance_reach()[child0], 1.0, 1e-12);
    EXPECT_NEAR(solver.chance_reach()[child1], 1.0, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_forward_reach_weights_chance_nodes) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::benchmark_turn_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool checked_chance = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::Chance || meta.chance_count == 0) {
            continue;
        }

        const auto& outcome = graph.chance_outcomes[meta.chance_begin];
        const auto parent_chance = solver.chance_reach()[node_idx];
        const auto child_chance = solver.chance_reach()[outcome.child];
        if (parent_chance > 0.0) {
            EXPECT_TRUE(child_chance > 0.0);
            EXPECT_TRUE(child_chance <= parent_chance);
            checked_chance = true;
            break;
        }
    }

    EXPECT_TRUE(checked_chance);
}
