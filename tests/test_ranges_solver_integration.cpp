#include "games/hunl_solver.hpp"
#include "ranges/cache.hpp"
#include "ranges/source.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

TEST_CASE(ranges_flat_solver_accepts_explicit_initial_range) {
    auto config = core::benchmark_turn_subgame();
    core::HUNLRangeInput range;
    range.hand_weights.push_back({{core::card_to_int(2, 0), core::card_to_int(3, 1)}, 1.0});
    config.initial_ranges[0] = range;
    config.range_policy = core::HUNLRangePolicy::UseInitialRanges;

    const auto output = core::solve_hunl_postflop(
        config,
        1,
        1.5,
        0.0,
        2.0,
        2,
        8,
        true);

    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
}

TEST_CASE(ranges_recursive_solver_works_when_ranges_are_absent) {
    const auto config = core::default_tiny_subgame();
    const auto output = core::solve_hunl_postflop(
        config,
        1,
        1.5,
        0.0,
        2.0,
        1,
        8,
        false);

    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(!output.average_strategy.empty());
}

TEST_CASE(ranges_average_strategy_export_remains_stable_with_range_support) {
    auto config = core::default_tiny_subgame();
    core::HUNLRangeInput range;
    range.hand_weights.push_back({{core::card_to_int(2, 0), core::card_to_int(3, 1)}, 1.0});
    config.player_ranges[0] = range;

    const auto first = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 1, 8, false);
    const auto second = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 1, 8, false);

    EXPECT_EQ(first.average_strategy.size(), second.average_strategy.size());
    for (const auto& [key, probs] : first.average_strategy) {
        const auto it = second.average_strategy.find(key);
        EXPECT_TRUE(it != second.average_strategy.end());
        EXPECT_EQ(probs.size(), it->second.size());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_NEAR(probs[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(ranges_exploitability_computation_remains_finite_and_non_negative) {
    const auto config = core::default_tiny_subgame();
    const auto output = core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0, 1, 8, false);

    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(output.exploitability >= 0.0);
}

}  // namespace
