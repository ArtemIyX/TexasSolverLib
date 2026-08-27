#include "games/hunl.hpp"
#include "solver/generic/exploit.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>

TEST_CASE(br_walk_mode_contract_rejects_vector_and_unknown_modes_before_tree_construction) {
    const auto config = texas::default_tiny_subgame();
    const std::unordered_map<std::string, std::vector<double>> strategy;
    for (std::uint8_t raw_mode = 1; raw_mode <= 20; ++raw_mode) {
        EXPECT_THROW(
            texas::compute_exploitability_and_value_with_mode(
                config, strategy, static_cast<texas::BrWalkMode>(raw_mode)),
            std::invalid_argument);
    }
}

TEST_CASE(br_walk_mode_contract_keeps_per_combo_available) {
    const auto config = texas::default_tiny_subgame();
    const std::unordered_map<std::string, std::vector<double>> strategy;
    const auto output = texas::compute_exploitability_and_value_with_mode(
        config, strategy, texas::BrWalkMode::PerCombo);
    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(std::isfinite(output.game_value));
}
