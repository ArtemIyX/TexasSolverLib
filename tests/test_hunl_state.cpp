#include "core/lib.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>

namespace {

core::HUNLState river_state() {
    return core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame()));
}

core::HUNLState flop_state(int starting_stack, int pot) {
    core::HUNLConfig cfg;
    cfg.starting_stack = starting_stack;
    cfg.starting_street = core::Street::Flop;
    cfg.initial_board = {
        core::card_to_int(14, 0),
        core::card_to_int(7, 3),
        core::card_to_int(2, 2),
    };
    cfg.initial_pot = pot;
    cfg.initial_contributions = {pot / 2, pot / 2};
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(14, 1), core::card_to_int(13, 3)},
        {core::card_to_int(12, 2), core::card_to_int(12, 1)},
    }};
    return core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(cfg));
}

struct EnvGuard {
    std::string name;
    std::optional<std::string> previous;

    EnvGuard(std::string env_name, std::optional<std::string> prev)
        : name(std::move(env_name)), previous(std::move(prev)) {}

    ~EnvGuard() {
#if defined(_MSC_VER)
        if (previous.has_value()) {
            _putenv_s(name.c_str(), previous->c_str());
        } else {
            _putenv_s(name.c_str(), "");
        }
#else
        if (previous.has_value()) {
            setenv(name.c_str(), previous->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
#endif
    }
};

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

}

TEST_CASE(hunl_initial_postflop_invariants) {
    const auto state = river_state();
    EXPECT_EQ(state.street, core::Street::River);
    EXPECT_EQ(state.contributions[0], 500);
    EXPECT_EQ(state.contributions[1], 500);
    EXPECT_EQ(state.stacks[0], 1000);
    EXPECT_EQ(state.stacks[1], 1000);
    EXPECT_EQ(state.to_call, 0);
    EXPECT_EQ(state.cur_player, 1);
    EXPECT_EQ(state.street_aggressor, -1);
    EXPECT_EQ(state.street_num_raises, 0);
}

TEST_CASE(hunl_river_root_actions_match_rust_test) {
    const auto actions = river_state().legal_actions();
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CHECK) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_ALL_IN) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_FOLD) == actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CALL) == actions.end());
}

TEST_CASE(hunl_facing_bet_has_fold_and_call_only_here) {
    const auto after_bet = river_state().apply(core::ACTION_BET_100);
    const auto actions = after_bet.legal_actions();
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_FOLD) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CALL) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CHECK) == actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_ALL_IN) == actions.end());
}

TEST_CASE(hunl_fold_and_showdown_utilities_match_reference) {
    const auto folded = river_state().apply(core::ACTION_CHECK).apply(core::ACTION_FOLD);
    EXPECT_TRUE(folded.is_terminal());
    EXPECT_NEAR(folded.utility()[0], 0.0, 1e-9);
    EXPECT_NEAR(folded.utility()[1], 10.0, 1e-9);

    const auto showdown = river_state().apply(core::ACTION_CHECK).apply(core::ACTION_CHECK);
    EXPECT_TRUE(showdown.is_terminal());
    EXPECT_EQ(showdown.street, core::Street::Showdown);
    EXPECT_NEAR(showdown.utility()[0], 10.0, 1e-9);
    EXPECT_NEAR(showdown.utility()[1], 0.0, 1e-9);
}

TEST_CASE(hunl_infoset_key_preserves_cards_and_history) {
    const auto state = river_state();
    EXPECT_EQ(state.infoset_key(0), std::string("KcAh|2d5s7cKhAs|r|"));
    EXPECT_EQ(state.infoset_key(1), std::string("QhQd|2d5s7cKhAs|r|"));

    const auto after_bet = state.apply(core::ACTION_CHECK).apply(core::ACTION_BET_100);
    EXPECT_EQ(after_bet.infoset_key(1), std::string("QhQd|2d5s7cKhAs|r|xb1000"));
}

TEST_CASE(hunl_flat_backend_populates_value_and_exploitability) {
    auto config = core::default_tiny_subgame();
    const auto prev = get_env("TEXASSOLVER_HUNL_FLAT_BACKEND");
    EnvGuard guard("TEXASSOLVER_HUNL_FLAT_BACKEND", prev);
#if defined(_MSC_VER)
    _putenv_s("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat");
#else
    setenv("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat", 1);
#endif

    const auto output = core::lib::solve_hunl_postflop(config, 10, 1.5, 0.0, 2.0, 4, 8, true);

    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(output.game_value != 0.0);
    EXPECT_TRUE(output.exploitability != 0.0);
    EXPECT_TRUE(!output.average_strategy.empty());
}

TEST_CASE(hunl_postflop_raise_cap_blocks_further_raises) {
    const auto state = flop_state(100000, 200);
    const auto capped = state.apply(core::ACTION_CHECK)
                            .apply(core::ACTION_BET_100)
                            .apply(core::ACTION_RAISE_33)
                            .apply(core::ACTION_RAISE_33);
    const auto actions = capped.legal_actions();
    const auto any_raise = std::any_of(actions.begin(), actions.end(), [](int action) { return action >= 8 && action <= 12; });
    const auto any_bet = std::any_of(actions.begin(), actions.end(), [](int action) { return action >= 3 && action <= 7; });
    EXPECT_TRUE(!any_raise);
    EXPECT_TRUE(!any_bet);
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_FOLD) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CALL) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_ALL_IN) == actions.end());
}


