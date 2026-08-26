#include "core/lib.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {

void expect_strategy_close(
    const std::vector<std::pair<std::string, std::vector<double>>>& lhs,
    const std::vector<std::pair<std::string, std::vector<double>>>& rhs,
    double epsilon) {
    EXPECT_EQ(static_cast<std::size_t>(lhs.size()), static_cast<std::size_t>(rhs.size()));
    for (const auto& [key, left_vec] : lhs) {
        const auto it = std::find_if(rhs.begin(), rhs.end(), [&](const auto& item) {
            return item.first == key;
        });
        EXPECT_TRUE(it != rhs.end());
        EXPECT_EQ(static_cast<std::size_t>(left_vec.size()), static_cast<std::size_t>(it->second.size()));
        for (std::size_t i = 0; i < left_vec.size(); ++i) {
            EXPECT_NEAR(left_vec[i], it->second[i], epsilon);
        }
    }
}

texas::HUNLConfig tiny_postflop_config() {
    texas::HUNLConfig cfg;
    cfg.starting_stack = 1000;
    cfg.starting_street = texas::Street::River;
    cfg.initial_board = {
        texas::card_to_int(14, 0),
        texas::card_to_int(7, 3),
        texas::card_to_int(2, 2),
        texas::card_to_int(13, 1),
        texas::card_to_int(5, 0)};
    cfg.initial_pot = 1000;
    cfg.initial_contributions = {500, 500};
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {texas::card_to_int(14, 1), texas::card_to_int(13, 3)},
        {texas::card_to_int(12, 2), texas::card_to_int(12, 1)},
    }};
    return cfg;
}

}  // namespace

TEST_CASE(parallel_dcfr_matches_sequential_kuhn_output) {
    const auto sequential = texas::lib::solve_kuhn(20, 1.5, 0.0, 2.0, 1);
    const auto parallel = texas::lib::solve_kuhn(20, 1.5, 0.0, 2.0, 2);

    EXPECT_EQ(sequential.iterations, parallel.iterations);
    EXPECT_NEAR(sequential.game_value, parallel.game_value, 1e-12);
    EXPECT_NEAR(sequential.exploitability, parallel.exploitability, 1e-12);
    expect_strategy_close(sequential.average_strategy, parallel.average_strategy, 1e-12);
}

TEST_CASE(parallel_dcfr_matches_sequential_leduc_output) {
    const auto sequential = texas::lib::solve_leduc(10, 1.5, 0.0, 2.0, 1);
    const auto parallel = texas::lib::solve_leduc(10, 1.5, 0.0, 2.0, 2);

    EXPECT_EQ(sequential.iterations, parallel.iterations);
    EXPECT_NEAR(sequential.game_value, parallel.game_value, 1e-12);
    EXPECT_NEAR(sequential.exploitability, parallel.exploitability, 1e-12);
    expect_strategy_close(sequential.average_strategy, parallel.average_strategy, 1e-12);
}

TEST_CASE(parallel_dcfr_matches_sequential_hunl_output) {
    const auto cfg = tiny_postflop_config();
    const auto sequential = texas::lib::solve_hunl_postflop(cfg, 5, 1.5, 0.0, 2.0, 1);
    const auto parallel = texas::lib::solve_hunl_postflop(cfg, 5, 1.5, 0.0, 2.0, 2);

    EXPECT_EQ(sequential.iterations, parallel.iterations);
    EXPECT_EQ(static_cast<std::size_t>(sequential.infoset_count), static_cast<std::size_t>(parallel.infoset_count));
    EXPECT_NEAR(sequential.game_value, parallel.game_value, 1e-9);
    EXPECT_NEAR(sequential.exploitability, parallel.exploitability, 1e-9);
    EXPECT_EQ(static_cast<std::size_t>(sequential.average_strategy.size()), static_cast<std::size_t>(parallel.average_strategy.size()));
    for (const auto& [key, left_vec] : sequential.average_strategy) {
        const auto it = parallel.average_strategy.find(key);
        EXPECT_TRUE(it != parallel.average_strategy.end());
        EXPECT_EQ(static_cast<std::size_t>(left_vec.size()), static_cast<std::size_t>(it->second.size()));
        for (std::size_t j = 0; j < left_vec.size(); ++j) {
            EXPECT_TRUE(std::fabs(left_vec[j] - it->second[j]) <= 1e-9);
        }
    }
}
