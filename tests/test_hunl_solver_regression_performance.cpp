#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_flat_state.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

core::HUNLConfig flop_subgame_config() {
    core::HUNLConfig cfg;
    cfg.starting_stack = 1000;
    cfg.starting_street = core::Street::Flop;
    cfg.initial_board = {
        test_support::c(14, 0),
        test_support::c(7, 3),
        test_support::c(2, 2)};
    cfg.initial_pot = 1000;
    cfg.initial_contributions = {500, 500};
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {test_support::c(14, 1), test_support::c(13, 3)},
        {test_support::c(12, 2), test_support::c(12, 1)},
    }};
    return cfg;
}

std::shared_ptr<const core::HUNLConfig> bucketed_river_config(std::filesystem::path* abstraction_path_out = nullptr) {
    auto cfg = core::default_tiny_subgame();
    const auto path = test_support::write_abstraction_fixture(
        "texas_solver_regression_river_bucketed.npz",
        std::nullopt,
        std::nullopt,
        cfg.initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 3U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 3}, core::ABSTRACTION_SCHEMA_VERSION, "river-3", std::nullopt});
    cfg.abstraction_path = path.string();
    cfg.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    if (abstraction_path_out != nullptr) {
        *abstraction_path_out = path;
    }
    return std::make_shared<const core::HUNLConfig>(std::move(cfg));
}

TEST_CASE(hunl_solver_regression_fixed_hole_and_range_weighted_modes_remain_supported) {
    const auto fixed_graph = core::HUNLFlatSolveGraph::build(
        std::make_shared<const core::HUNLConfig>(flop_subgame_config()));
    core::HUNLFlatDCFR fixed_solver(
        fixed_graph,
        {1, 1},
        core::HUNLFlatSolveMode::ExplicitHand,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    fixed_solver.run_iteration();
    EXPECT_TRUE(fixed_solver.iterations() == 1U);
    EXPECT_TRUE(!fixed_solver.export_average_strategy().empty());

    std::filesystem::path abstraction_path;
    auto weighted_cfg = *bucketed_river_config(&abstraction_path);
    core::HUNLRangeInput range;
    range.hand_weights.push_back({(*weighted_cfg.initial_hole_cards)[0], 2.0});
    weighted_cfg.player_ranges[0] = range;
    core::HUNLFlatDCFR weighted_solver(
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(weighted_cfg)),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    weighted_solver.run_iteration();
    EXPECT_TRUE(weighted_solver.iterations() == 1U);
    EXPECT_TRUE(weighted_solver.bucket_map() != nullptr);

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_regression_flop_storage_shrinks_when_bucket_counts_shrink) {
    const auto explicit_cfg = std::make_shared<const core::HUNLConfig>(flop_subgame_config());
    const auto graph = core::HUNLFlatSolveGraph::build(explicit_cfg);
    const auto explicit_table = core::HUNLFlatInfosetTable::build(
        graph,
        {1326, 1326},
        core::HUNLFlatValueLayout::InfosetHandAction);
    const auto reduced_bucket_table = core::HUNLFlatInfosetTable::build(
        graph,
        {3, 3},
        core::HUNLFlatValueLayout::InfosetHandAction);

    EXPECT_TRUE(reduced_bucket_table.total_value_count() < explicit_table.total_value_count());
    EXPECT_TRUE(reduced_bucket_table.total_bucket_count() < explicit_table.total_bucket_count());
}

TEST_CASE(hunl_solver_regression_stage_timing_is_recorded_for_bucketed_mode) {
    std::filesystem::path abstraction_path;
    const auto config = bucketed_river_config(&abstraction_path);
    core::HUNLFlatDCFR solver(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    solver.run_iteration();

    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().reach_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().regret_seconds >= 0.0);

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_regression_multithreaded_bucketed_runs_match_single_thread_within_tolerance) {
    std::filesystem::path abstraction_path;
    const auto config = bucketed_river_config(&abstraction_path);
    core::HUNLFlatDCFR single(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR multi(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        2);
    single.run_iterations(2);
    multi.run_iterations(2);

    const auto avg_single = single.export_average_strategy();
    const auto avg_multi = multi.export_average_strategy();
    EXPECT_EQ(avg_single.size(), avg_multi.size());
    for (const auto& [key, probs] : avg_single) {
        const auto it = avg_multi.find(key);
        EXPECT_TRUE(it != avg_multi.end());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_NEAR(probs[i], it->second[i], 1e-9);
        }
    }

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_regression_benchmark_fixture_compares_explicit_and_bucketed_flop_runs) {
    std::filesystem::path abstraction_path;
    const auto bucketed_cfg = *bucketed_river_config(&abstraction_path);
    core::HUNLFlatDCFR explicit_solver(
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame())),
        {1, 1},
        core::HUNLFlatSolveMode::ExplicitHand,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR bucketed_solver(
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(bucketed_cfg)),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    explicit_solver.run_iteration();
    bucketed_solver.run_iteration();

    EXPECT_TRUE(explicit_solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(bucketed_solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(explicit_solver.infoset_table().infoset_count() > 0);
    EXPECT_TRUE(bucketed_solver.infoset_table().infoset_count() > 0);

    std::filesystem::remove(abstraction_path);
}

}  // namespace
