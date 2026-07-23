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
    const auto after_bet = river_state().apply(core::ACTION_ALL_IN);
    const auto actions = after_bet.legal_actions();
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_FOLD) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CALL) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_CHECK) == actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_ALL_IN) == actions.end());
}

TEST_CASE(hunl_fold_and_showdown_utilities_match_reference) {
    const auto folded =
        river_state().apply(core::ACTION_ALL_IN).apply(core::ACTION_FOLD);
    EXPECT_TRUE(folded.is_terminal());
    EXPECT_NEAR(folded.utility()[0], 0.0, 1e-9);
    EXPECT_NEAR(folded.utility()[1], 10.0, 1e-9);

    const auto showdown = river_state().apply(core::ACTION_CHECK).apply(core::ACTION_CHECK);
    EXPECT_TRUE(showdown.is_terminal());
    EXPECT_EQ(showdown.street, core::Street::Showdown);
    EXPECT_NEAR(showdown.utility()[0], 10.0, 1e-9);
    EXPECT_NEAR(showdown.utility()[1], 0.0, 1e-9);
}

TEST_CASE(hunl_apply_rejects_twenty_unknown_player_action_identifiers) {
    const auto state = river_state();
    for (int action = -10; action < 0; ++action) {
        EXPECT_THROW(state.apply(action), std::invalid_argument);
    }
    for (int action = 100; action < 110; ++action) {
        EXPECT_THROW(state.apply(action), std::invalid_argument);
    }
}

TEST_CASE(hunl_apply_rejects_semantically_illegal_known_player_actions) {
    const auto root = river_state();
    EXPECT_THROW(root.apply(core::ACTION_FOLD), std::invalid_argument);
    EXPECT_THROW(root.apply(core::ACTION_CALL), std::invalid_argument);
    EXPECT_THROW(root.apply(core::ACTION_RAISE_33), std::invalid_argument);

    const auto facing_bet = root.apply(core::ACTION_ALL_IN);
    EXPECT_THROW(facing_bet.apply(core::ACTION_CHECK), std::invalid_argument);
    EXPECT_THROW(facing_bet.apply(core::ACTION_BET_33), std::invalid_argument);

    const auto terminal = root.apply(core::ACTION_CHECK).apply(core::ACTION_CHECK);
    EXPECT_THROW(terminal.apply(core::ACTION_CHECK), std::invalid_argument);
}

TEST_CASE(hunl_apply_rejects_twenty_invalid_or_blocked_chance_actions) {
    const auto chance = flop_state(1000, 200)
                            .apply(core::ACTION_CHECK)
                            .apply(core::ACTION_CHECK);
    EXPECT_EQ(chance.cur_player, -1);
    const std::array<int, 20> invalid = {
        -1, 0, 1, 2, 3, 4, 5, 6, 7,
        core::card_to_int(14, 0),
        core::card_to_int(7, 3),
        core::card_to_int(2, 2),
        core::card_to_int(14, 1),
        core::card_to_int(13, 3),
        core::card_to_int(12, 2),
        core::card_to_int(12, 1),
        60, 61, 62, 63,
    };
    for (const auto action : invalid) {
        EXPECT_THROW(chance.apply(action), std::invalid_argument);
    }
}

TEST_CASE(hunl_infoset_key_preserves_cards_and_history) {
    const auto state = river_state();
    EXPECT_EQ(state.infoset_key(0), std::string("KcAh|2d5s7cKhAs|r|"));
    EXPECT_EQ(state.infoset_key(1), std::string("QhQd|2d5s7cKhAs|r|"));

    const auto after_bet =
        state.apply(core::ACTION_CHECK).apply(core::ACTION_BET_75);
    EXPECT_EQ(
        after_bet.infoset_key(1),
        std::string("QhQd|2d5s7cKhAs|r|xb750"));
}

TEST_CASE(hunl_infoset_encoding_rejects_twenty_invalid_player_ids) {
    const auto state = river_state();
    for (int player = -10; player < 0; ++player) {
        EXPECT_THROW(state.infoset_encoding(player), std::invalid_argument);
        EXPECT_THROW(state.infoset_key(player), std::invalid_argument);
    }
    for (int player = 2; player < 12; ++player) {
        EXPECT_THROW(state.infoset_encoding(player), std::invalid_argument);
        EXPECT_THROW(state.infoset_key(player), std::invalid_argument);
    }
}

TEST_CASE(hunl_infoset_validator_rejects_more_than_twenty_malformed_fixed_encodings) {
    const auto base = river_state().infoset_encoding(0);
    std::vector<core::HUNLInfosetEncoding> invalid;
    for (std::uint8_t count = 6; count <= 10; ++count) {
        auto encoding = base;
        encoding.board_count = count;
        invalid.push_back(encoding);
    }
    for (std::uint8_t count = 49; count <= 53; ++count) {
        auto encoding = base;
        encoding.history_count = count;
        invalid.push_back(encoding);
    }
    for (std::uint8_t length = 1; length <= 5; ++length) {
        auto encoding = base;
        encoding.street_lengths[0] = length;
        invalid.push_back(encoding);
    }
    for (int code = 1; code <= 5; ++code) {
        auto encoding = base;
        encoding.history_codes.back() = code;
        invalid.push_back(encoding);
    }
    for (std::uint8_t card = 0; card < 5; ++card) {
        auto encoding = base;
        encoding.hole[0] = card;
        invalid.push_back(encoding);
    }

    EXPECT_TRUE(invalid.size() > 20U);
    for (const auto& encoding : invalid) {
        EXPECT_THROW(
            core::validate_hunl_infoset_encoding(encoding),
            std::invalid_argument);
        EXPECT_THROW(core::hunl_infoset_key(encoding), std::invalid_argument);
    }
}

TEST_CASE(hunl_infoset_history_uses_exact_capacity_and_rejects_truncation) {
    auto exact = river_state();
    exact.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES, 2);
    const auto encoding = exact.infoset_encoding(0);
    EXPECT_EQ(encoding.history_count, core::HUNL_MAX_HISTORY_CODES);
    EXPECT_EQ(encoding.street_lengths[0], core::HUNL_MAX_HISTORY_CODES);

    exact.current_street_history_codes.push_back(2);
    EXPECT_THROW(exact.infoset_encoding(0), std::invalid_argument);
}

TEST_CASE(hunl_infoset_history_keeps_large_opening_bets_distinct_from_raises) {
    core::HUNLConfig config;
    config.starting_stack = 2'000'000;
    config.starting_street = core::Street::Flop;
    config.initial_board = {
        core::card_to_int(14, 0),
        core::card_to_int(7, 3),
        core::card_to_int(2, 2),
    };
    config.initial_pot = 1'200'000;
    config.initial_contributions = {600'000, 600'000};
    config.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(14, 1), core::card_to_int(13, 3)},
        {core::card_to_int(12, 2), core::card_to_int(12, 1)},
    }};
    const auto state = core::HUNLState::initial(
        std::make_shared<const core::HUNLConfig>(config));
    const auto facing_bet = state.apply(core::ACTION_CHECK)
                                .apply(core::ACTION_BET_100);
    EXPECT_EQ(
        facing_bet.infoset_key(1),
        std::string("QhQd|2d7cAs|f|xb1200000"));
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
    EXPECT_EQ(output.quality_metric, core::HUNLQualityMetric::PerPlayerExploitability);
    EXPECT_NEAR(output.total_nash_conv, output.exploitability * 2.0, 1e-12);
    EXPECT_TRUE(!output.average_strategy.empty());
}

TEST_CASE(hunl_public_postflop_solver_fails_closed_for_depth_limit_without_shared_leaf_evaluator) {
    auto config = core::default_tiny_subgame();
    config.depth_limit_plies = 1;
    EXPECT_THROW(core::solve_hunl_postflop(config, 1, 1.5, 0.0, 2.0), std::invalid_argument);
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


