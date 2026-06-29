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

TEST_CASE(hunl_flat_dcfr_keeps_configured_worker_pool_across_iterations) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetActionHand,
        3);

    EXPECT_EQ(solver.worker_count(), 3U);
    solver.run_iteration();
    solver.run_iteration();

    EXPECT_EQ(solver.worker_count(), 3U);
    EXPECT_EQ(solver.iterations(), 2U);
}

TEST_CASE(hunl_flat_dcfr_accepts_single_worker_pool_configuration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand,
        1);

    EXPECT_EQ(solver.worker_count(), 1U);
    solver.run_iteration();
    EXPECT_EQ(solver.iterations(), 1U);
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

TEST_CASE(hunl_flat_dcfr_terminal_stage_uses_precomputed_leaf_utilities) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool saw_terminal = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::TerminalFold &&
            meta.type != core::HUNLFlatNodeType::TerminalShowdown) {
            continue;
        }

        saw_terminal = true;
        if (meta.type == core::HUNLFlatNodeType::TerminalFold) {
            EXPECT_TRUE(meta.terminal_kind.tag == core::TerminalKindTag::Fold);
        } else {
            EXPECT_TRUE(meta.terminal_kind.tag == core::TerminalKindTag::Showdown);
        }
        EXPECT_NEAR(meta.terminal_utility[0], solver.terminal_values()[node_idx], 1e-12);
    }

    EXPECT_TRUE(saw_terminal);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_copies_terminal_values_into_node_values) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool saw_terminal = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::TerminalFold &&
            meta.type != core::HUNLFlatNodeType::TerminalShowdown) {
            continue;
        }
        saw_terminal = true;
        EXPECT_NEAR(solver.node_values()[node_idx], solver.terminal_values()[node_idx], 1e-12);
    }

    EXPECT_TRUE(saw_terminal);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_writes_action_values_from_children) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool checked_parent = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.child_count == 0) {
            continue;
        }

        for (std::size_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph.children[meta.child_begin + i];
            EXPECT_NEAR(
                solver.action_values()[meta.child_begin + i],
                solver.node_values()[child],
                1e-12);
        }
        checked_parent = true;
        break;
    }

    EXPECT_TRUE(checked_parent);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_computes_root_value_from_children) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    const auto& root_meta = graph.node_meta[graph.root];
    const auto infoset_id = root_meta.infoset_id;
    auto* regret = table.regret_mut(infoset_id);
    for (std::size_t i = 0; i < table.row_value_count(infoset_id); ++i) {
        regret[i] = 0.0;
    }
    if (root_meta.child_count >= 2) {
        const auto hand_count = table.meta()[infoset_id.value].hand_count;
        regret[0] = 4.0;
        regret[hand_count] = 1.0;
    }

    solver.run_iteration();

    if (root_meta.type == core::HUNLFlatNodeType::Decision && root_meta.child_count >= 2) {
        const auto a0 = solver.action_values()[root_meta.child_begin];
        const auto a1 = solver.action_values()[root_meta.child_begin + 1];
        EXPECT_TRUE(solver.node_values()[graph.root] <= std::max(a0, a1) + 1e-12);
        EXPECT_TRUE(solver.node_values()[graph.root] >= std::min(a0, a1) - 1e-12);
    } else {
        EXPECT_TRUE(root_meta.child_count > 0);
    }
}

TEST_CASE(hunl_flat_dcfr_regret_update_uses_action_minus_node_value) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    const auto root_infoset = graph.node_meta[graph.root].infoset_id;
    auto* regret_before = table.regret_mut(root_infoset);
    const auto before0 = regret_before[0];
    const auto before1 = regret_before[table.meta()[root_infoset.value].hand_count];

    solver.run_iteration();

    const auto* regret_after = table.regret(root_infoset);
    EXPECT_TRUE(regret_after[0] != before0 || regret_after[table.meta()[root_infoset.value].hand_count] != before1);
}

TEST_CASE(hunl_flat_dcfr_average_strategy_update_is_reach_weighted) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    const auto root_infoset = graph.node_meta[graph.root].infoset_id;
    const auto* strategy = solver.infoset_table().current_strategy(root_infoset);
    const auto* strategy_sum = solver.infoset_table().strategy_sum(root_infoset);
    const auto hand_count = solver.infoset_table().meta()[root_infoset.value].hand_count;
    const auto own_reach = graph.node_meta[graph.root].player == 0
        ? solver.player0_reach()[graph.root] * solver.chance_reach()[graph.root]
        : solver.player1_reach()[graph.root] * solver.chance_reach()[graph.root];

    EXPECT_NEAR(strategy_sum[0], own_reach * strategy[0], 1e-12);
    EXPECT_NEAR(strategy_sum[hand_count], own_reach * strategy[hand_count], 1e-12);
}

TEST_CASE(hunl_flat_dcfr_discount_stage_updates_infoset_discount_iteration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iterations(2);

    for (const auto& meta : solver.infoset_table().meta()) {
        EXPECT_EQ(meta.last_discount_iter, 2U);
    }
}
