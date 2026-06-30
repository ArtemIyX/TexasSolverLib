#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

std::shared_ptr<const core::HUNLConfig> one_bucket_river_config(std::filesystem::path* abstraction_path_out = nullptr) {
    auto config = core::default_tiny_subgame();
    const auto path = test_support::write_abstraction_fixture(
        "texas_solver_behavior_one_bucket.npz",
        std::nullopt,
        std::nullopt,
        config.initial_board,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        test_support::AbstractionFixtureOptions{{1, 1, 1}, core::ABSTRACTION_SCHEMA_VERSION, "one-bucket", std::nullopt});
    config.abstraction_path = path.string();
    config.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    if (abstraction_path_out != nullptr) {
        *abstraction_path_out = path;
    }
    return std::make_shared<const core::HUNLConfig>(std::move(config));
}

std::shared_ptr<const core::HUNLConfig> two_bucket_river_config(std::filesystem::path* abstraction_path_out = nullptr) {
    auto config = core::default_tiny_subgame();
    const auto path = test_support::write_abstraction_fixture(
        "texas_solver_behavior_two_bucket.npz",
        std::nullopt,
        std::nullopt,
        config.initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "two-bucket", std::nullopt});
    config.abstraction_path = path.string();
    config.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    if (abstraction_path_out != nullptr) {
        *abstraction_path_out = path;
    }
    return std::make_shared<const core::HUNLConfig>(std::move(config));
}

TEST_CASE(hunl_solver_behavior_explicit_and_one_bucket_outputs_match_on_tiny_game) {
    std::filesystem::path abstraction_path;
    const auto bucketed_config = one_bucket_river_config(&abstraction_path);
    const auto explicit_config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());

    core::HUNLFlatDCFR explicit_solver(
        core::HUNLFlatSolveGraph::build(explicit_config),
        {1, 1},
        core::HUNLFlatSolveMode::ExplicitHand,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR bucketed_solver(
        core::HUNLFlatSolveGraph::build(bucketed_config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);

    explicit_solver.run_iterations(2);
    bucketed_solver.run_iterations(2);

    const auto explicit_avg = explicit_solver.export_average_strategy();
    const auto bucketed_avg = bucketed_solver.export_average_strategy();
    EXPECT_EQ(explicit_avg.size(), bucketed_avg.size());
    for (const auto& [key, probs] : explicit_avg) {
        const auto it = bucketed_avg.find(key);
        EXPECT_TRUE(it != bucketed_avg.end());
        EXPECT_EQ(probs.size(), it->second.size());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_NEAR(probs[i], it->second[i], 1e-12);
        }
    }

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_behavior_fixed_abstraction_is_deterministic_across_runs) {
    std::filesystem::path abstraction_path;
    const auto config = two_bucket_river_config(&abstraction_path);

    core::HUNLFlatDCFR solver_a(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR solver_b(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);

    solver_a.run_iterations(2);
    solver_b.run_iterations(2);

    const auto avg_a = solver_a.export_average_strategy();
    const auto avg_b = solver_b.export_average_strategy();
    EXPECT_EQ(avg_a.size(), avg_b.size());
    for (const auto& [key, probs] : avg_a) {
        const auto it = avg_b.find(key);
        EXPECT_TRUE(it != avg_b.end());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_NEAR(probs[i], it->second[i], 1e-12);
        }
    }

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_behavior_one_iteration_updates_expected_bucket_rows) {
    std::filesystem::path abstraction_path;
    const auto config = two_bucket_river_config(&abstraction_path);
    core::HUNLFlatDCFR solver(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);

    solver.run_iteration();

    bool saw_non_zero_regret = false;
    bool saw_non_zero_strategy_sum = false;
    for (const auto& meta : solver.infoset_table().meta()) {
        const auto* regret = solver.infoset_table().regret(meta.id);
        const auto* strategy_sum = solver.infoset_table().strategy_sum(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            saw_non_zero_regret = saw_non_zero_regret || std::fabs(regret[i]) > 0.0;
            saw_non_zero_strategy_sum = saw_non_zero_strategy_sum || std::fabs(strategy_sum[i]) > 0.0;
        }
    }

    EXPECT_TRUE(saw_non_zero_regret);
    EXPECT_TRUE(saw_non_zero_strategy_sum);
    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_behavior_average_strategy_normalizes_bucket_rows) {
    std::filesystem::path abstraction_path;
    const auto config = two_bucket_river_config(&abstraction_path);
    core::HUNLFlatDCFR solver(
        core::HUNLFlatSolveGraph::build(config),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);

    solver.run_iterations(2);

    for (const auto& meta : solver.infoset_table().meta()) {
        const auto* strategy = solver.infoset_table().current_strategy(meta.id);
        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            double sum = 0.0;
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                const auto idx = bucket * static_cast<std::size_t>(meta.action_count) + action;
                sum += strategy[idx];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
        }
    }

    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_solver_behavior_range_weighted_inputs_change_bucketed_results_when_expected) {
    std::filesystem::path base_path;
    auto uniform_cfg = *two_bucket_river_config(&base_path);
    auto weighted_cfg = uniform_cfg;
    const auto player_hole = (*weighted_cfg.initial_hole_cards)[0];
    core::HUNLRangeInput range;
    range.hand_weights.push_back({player_hole, 4.0});
    range.bucket_weights.push_back({core::Street::River, 1U, 1.0});
    weighted_cfg.player_ranges[0] = range;

    core::HUNLFlatDCFR uniform_solver(
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(uniform_cfg)),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR weighted_solver(
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(weighted_cfg)),
        {1, 1},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);

    uniform_solver.run_iteration();
    weighted_solver.run_iteration();

    bool saw_difference = false;
    for (const auto& meta : uniform_solver.infoset_table().meta()) {
        const auto* uniform_sum = uniform_solver.infoset_table().strategy_sum(meta.id);
        const auto* weighted_sum = weighted_solver.infoset_table().strategy_sum(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            if (std::fabs(uniform_sum[i] - weighted_sum[i]) > 1e-12) {
                saw_difference = true;
                break;
            }
        }
        if (saw_difference) {
            break;
        }
    }

    EXPECT_TRUE(saw_difference);
    std::filesystem::remove(base_path);
}

}  // namespace
