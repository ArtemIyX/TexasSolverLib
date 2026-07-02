#include "games/hunl_solver.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <memory>

namespace {

core::HUNLFlatSolveGraph make_shared_infoset_same_depth_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 2;
    graph.max_actions = 2;

    graph.children = {1, 2, 3, 4, 5, 6};
    graph.chance_outcomes = {
        core::HUNLFlatChanceOutcome{0, 0.5, 1},
        core::HUNLFlatChanceOutcome{1, 0.5, 2},
    };

    const auto shared_infoset = core::InfosetId{0};
    graph.infosets.push_back(core::HUNLFlatInfoset{
        shared_infoset,
        0,
        2,
        {},
        0,
        0,
        core::Street::Flop,
        2,
    });
    graph.infoset_debug_keys = {"shared-depth-infoset"};
    graph.infoset_nodes = {1, 2};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(7);

    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].chance_begin = 0;
    graph.node_meta[0].chance_count = 2;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Chance;
    graph.node_meta[0].street = core::Street::Flop;

    for (std::uint32_t node_idx : {1U, 2U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = node_idx == 1 ? 2 : 4;
        meta.child_count = 2;
        meta.infoset_id = shared_infoset;
        meta.player = 0;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 2;
        meta.has_infoset = true;
    }

    graph.node_meta[3] = make_terminal_meta(3.0);
    graph.node_meta[4] = make_terminal_meta(-1.0);
    graph.node_meta[5] = make_terminal_meta(2.0);
    graph.node_meta[6] = make_terminal_meta(-2.0);

    graph.depth_order = {0, 1, 2, 3, 4, 5, 6};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
        core::HUNLFlatSlice{3, 4},
    };
    graph.node_depths = {0, 1, 1, 2, 2, 2, 2};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {6, 5, 4, 3, 2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, static_cast<std::uint32_t>(graph.node_meta.size())};
    return graph;
}

TEST_CASE(ranges_single_thread_and_multi_thread_runs_match_within_tolerance) {
    const auto config = core::benchmark_turn_subgame();
    const auto single = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 1, 8, true);
    const auto multi = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 2, 8, true);

    EXPECT_NEAR(single.game_value, multi.game_value, 1e-12);
    EXPECT_NEAR(single.exploitability, multi.exploitability, 1e-12);
}

TEST_CASE(ranges_worker_scheduling_does_not_change_normalized_ranges) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::benchmark_turn_subgame());
    const auto graph_a = core::HUNLFlatSolveGraph::build(config);
    const auto graph_b = core::HUNLFlatSolveGraph::build(config);

    core::HUNLFlatDCFR two_workers(graph_a, {2, 2}, core::HUNLFlatValueLayout::InfosetHandAction, 2);
    core::HUNLFlatDCFR three_workers(graph_b, {2, 2}, core::HUNLFlatValueLayout::InfosetHandAction, 3);
    two_workers.run_iteration();
    three_workers.run_iteration();

    const auto a = two_workers.export_average_strategy();
    const auto b = three_workers.export_average_strategy();
    EXPECT_EQ(a.size(), b.size());
    for (const auto& [key, probs] : a) {
        const auto it = b.find(key);
        EXPECT_TRUE(it != b.end());
        EXPECT_EQ(probs.size(), it->second.size());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_NEAR(probs[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(ranges_multiworker_run_does_not_corrupt_range_tables) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::benchmark_turn_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(graph, {2, 2}, core::HUNLFlatValueLayout::InfosetHandAction, 4);
    solver.run_iterations(2);

    for (const auto value : solver.bucket_reach()) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_TRUE(value >= 0.0);
    }
    for (const auto value : solver.player0_reach()) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_TRUE(value >= 0.0);
    }
    for (const auto value : solver.player1_reach()) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_TRUE(value >= 0.0);
    }
}

TEST_CASE(ranges_repeated_runs_with_same_config_are_deterministic) {
    const auto config = core::benchmark_turn_subgame();
    const auto first = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 2, 8, true);
    const auto second = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 2, 8, true);

    EXPECT_NEAR(first.game_value, second.game_value, 1e-12);
    EXPECT_NEAR(first.exploitability, second.exploitability, 1e-12);
    EXPECT_EQ(first.average_strategy.size(), second.average_strategy.size());
}

TEST_CASE(ranges_shared_infoset_same_depth_graph_matches_across_worker_counts) {
    const auto graph_a = make_shared_infoset_same_depth_graph();
    const auto graph_b = make_shared_infoset_same_depth_graph();
    core::HUNLFlatDCFR single_worker(graph_a, {2, 2}, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    core::HUNLFlatDCFR two_workers(graph_b, {2, 2}, core::HUNLFlatValueLayout::InfosetActionHand, 2);

    for (auto* solver : {&single_worker, &two_workers}) {
        auto& table = solver->infoset_table_mut();
        const auto infoset_id = solver->graph().infosets.front().id;
        auto* regret = table.regret_mut(infoset_id);
        const auto bucket_count = table.meta()[infoset_id.value].bucket_count;
        for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[bucket] = 5.0;
            regret[bucket_count + bucket] = 0.0;
        }
    }

    single_worker.run_iteration();
    two_workers.run_iteration();

    for (std::size_t i = 0; i < single_worker.player0_reach().size(); ++i) {
        EXPECT_NEAR(single_worker.player0_reach()[i], two_workers.player0_reach()[i], 1e-12);
        EXPECT_NEAR(single_worker.player1_reach()[i], two_workers.player1_reach()[i], 1e-12);
        EXPECT_NEAR(single_worker.chance_reach()[i], two_workers.chance_reach()[i], 1e-12);
    }
    for (std::size_t i = 0; i < single_worker.bucket_reach().size(); ++i) {
        EXPECT_NEAR(single_worker.bucket_reach()[i], two_workers.bucket_reach()[i], 1e-12);
        EXPECT_NEAR(single_worker.normalized_bucket_reach()[i], two_workers.normalized_bucket_reach()[i], 1e-12);
    }

    const auto infoset_id = single_worker.graph().infosets.front().id;
    const auto* single_sum = single_worker.infoset_table().strategy_sum(infoset_id);
    const auto* two_sum = two_workers.infoset_table().strategy_sum(infoset_id);
    const auto row_count = single_worker.infoset_table().row_value_count(infoset_id);
    for (std::size_t i = 0; i < row_count; ++i) {
        EXPECT_NEAR(single_sum[i], two_sum[i], 1e-12);
    }
}

TEST_CASE(ranges_shared_infoset_same_depth_graph_repeated_runs_are_deterministic) {
    const auto graph_a = make_shared_infoset_same_depth_graph();
    const auto graph_b = make_shared_infoset_same_depth_graph();
    core::HUNLFlatDCFR first(graph_a, {2, 2}, core::HUNLFlatValueLayout::InfosetActionHand, 2);
    core::HUNLFlatDCFR second(graph_b, {2, 2}, core::HUNLFlatValueLayout::InfosetActionHand, 2);

    for (auto* solver : {&first, &second}) {
        auto& table = solver->infoset_table_mut();
        const auto infoset_id = solver->graph().infosets.front().id;
        auto* regret = table.regret_mut(infoset_id);
        const auto bucket_count = table.meta()[infoset_id.value].bucket_count;
        for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[bucket] = 3.0;
            regret[bucket_count + bucket] = 0.0;
        }
    }

    first.run_iterations(2);
    second.run_iterations(2);

    for (std::size_t i = 0; i < first.player0_reach().size(); ++i) {
        EXPECT_NEAR(first.player0_reach()[i], second.player0_reach()[i], 1e-12);
        EXPECT_NEAR(first.player1_reach()[i], second.player1_reach()[i], 1e-12);
        EXPECT_NEAR(first.chance_reach()[i], second.chance_reach()[i], 1e-12);
        EXPECT_NEAR(first.node_values()[i], second.node_values()[i], 1e-12);
    }
    for (std::size_t i = 0; i < first.bucket_reach().size(); ++i) {
        EXPECT_NEAR(first.bucket_reach()[i], second.bucket_reach()[i], 1e-12);
        EXPECT_NEAR(first.normalized_bucket_reach()[i], second.normalized_bucket_reach()[i], 1e-12);
    }
}

TEST_CASE(ranges_shared_infoset_same_depth_graph_matches_across_layouts_and_worker_counts) {
    const auto graph_a = make_shared_infoset_same_depth_graph();
    const auto graph_b = make_shared_infoset_same_depth_graph();
    core::HUNLFlatDCFR hand_action(graph_a, {2, 2}, core::HUNLFlatValueLayout::InfosetHandAction, 1);
    core::HUNLFlatDCFR action_hand(graph_b, {2, 2}, core::HUNLFlatValueLayout::InfosetActionHand, 3);

    for (auto* solver : {&hand_action, &action_hand}) {
        auto& table = solver->infoset_table_mut();
        const auto infoset_id = solver->graph().infosets.front().id;
        auto* regret = table.regret_mut(infoset_id);
        const auto bucket_count = table.meta()[infoset_id.value].bucket_count;
        for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[table.value_index(infoset_id, bucket, 0)] = 6.0;
            regret[table.value_index(infoset_id, bucket, 1)] = 0.0;
        }
    }

    hand_action.run_iterations(2);
    action_hand.run_iterations(2);

    for (std::size_t i = 0; i < hand_action.player0_reach().size(); ++i) {
        EXPECT_NEAR(hand_action.player0_reach()[i], action_hand.player0_reach()[i], 1e-12);
        EXPECT_NEAR(hand_action.player1_reach()[i], action_hand.player1_reach()[i], 1e-12);
        EXPECT_NEAR(hand_action.chance_reach()[i], action_hand.chance_reach()[i], 1e-12);
        EXPECT_NEAR(hand_action.node_values()[i], action_hand.node_values()[i], 1e-12);
    }
    for (std::size_t i = 0; i < hand_action.bucket_reach().size(); ++i) {
        EXPECT_NEAR(hand_action.bucket_reach()[i], action_hand.bucket_reach()[i], 1e-12);
        EXPECT_NEAR(hand_action.normalized_bucket_reach()[i], action_hand.normalized_bucket_reach()[i], 1e-12);
    }
}

}  // namespace
