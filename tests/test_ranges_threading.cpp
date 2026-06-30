#include "games/hunl_solver.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <memory>

namespace {

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

}  // namespace
