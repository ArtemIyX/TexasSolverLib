#include "core/lib.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

texas::HUNLState river_state() {
    return texas::HUNLState::initial(std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame()));
}

}  // namespace

TEST_CASE(hunl_regression_infoset_key_format_is_stable) {
    const auto state = river_state();
    EXPECT_EQ(state.infoset_key(0), std::string("KcAh|2d5s7cKhAs|r|"));
    EXPECT_EQ(state.infoset_key(1), std::string("QhQd|2d5s7cKhAs|r|"));
}

TEST_CASE(hunl_regression_flat_backend_populates_value_metrics) {
    auto config = texas::default_tiny_subgame();
    const auto output = texas::lib::solve_hunl_postflop(
        config, 10, 1.5, 0.0, 2.0, 4, 8, true, texas::HUNLBackendSelection::Flat);

    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(output.game_value != 0.0);
    EXPECT_TRUE(output.exploitability != 0.0);
    EXPECT_TRUE(!output.average_strategy.empty());
}

TEST_CASE(hunl_regression_explicit_hand_flat_mode_runs_without_abstraction) {
    auto config = texas::default_tiny_subgame();
    config.flat_solve_mode = texas::HUNLFlatSolveMode::ExplicitHand;

    const auto output = texas::lib::solve_hunl_postflop(
        config, 2, 1.5, 0.0, 2.0, 1, 8, true, texas::HUNLBackendSelection::Flat);
    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
}

TEST_CASE(hunl_regression_bucketed_flat_mode_requires_abstraction) {
    auto config = texas::default_tiny_subgame();
    config.flat_solve_mode = texas::HUNLFlatSolveMode::Bucketed;
    config.abstraction_path = std::nullopt;

    EXPECT_THROW(
        static_cast<void>(texas::lib::solve_hunl_postflop(
            config, 1, 1.5, 0.0, 2.0, 1, 8, true, texas::HUNLBackendSelection::Flat)),
        std::invalid_argument);
}

