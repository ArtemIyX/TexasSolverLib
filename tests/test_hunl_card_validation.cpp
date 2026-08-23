#include "games/hunl.hpp"
#include "ranges/propagation.hpp"
#include "test_harness.hpp"

#include <limits>
#include <memory>
#include <stdexcept>

namespace {

texas::HUNLConfig valid_config() {
    return texas::default_tiny_subgame();
}

texas::HUNLRangeInput single_hand_range(std::uint8_t first, std::uint8_t second, double weight) {
    texas::HUNLRangeInput range;
    range.hand_weights.push_back({{first, second}, weight});
    return range;
}

texas::HUNLConfig valid_range_config() {
    auto config = valid_config();
    config.initial_hole_cards = std::nullopt;
    config.range_policy = texas::HUNLRangePolicy::RequireExplicit;
    config.initial_ranges[0] = single_hand_range(texas::card_to_int(14, 1), texas::card_to_int(13, 3), 1.0);
    config.initial_ranges[1] = single_hand_range(texas::card_to_int(12, 1), texas::card_to_int(11, 3), 1.0);
    return config;
}

}  // namespace

TEST_CASE(hunl_card_validation_recognizes_compact_encoding_bounds) {
    EXPECT_EQ(texas::DECK_CARD_COUNT, 52U);
    EXPECT_TRUE(texas::is_valid_card(0));
    EXPECT_TRUE(!texas::is_valid_card(texas::card_to_int(1, 3)));
    EXPECT_TRUE(texas::is_valid_card(texas::card_to_int(2, 0)));
    EXPECT_TRUE(texas::is_valid_card(texas::card_to_int(14, 3)));
    EXPECT_TRUE(!texas::is_valid_card(texas::card_to_int(15, 0)));
    EXPECT_TRUE(!texas::is_valid_card(std::numeric_limits<std::uint8_t>::max()));
}

TEST_CASE(hunl_card_validation_uses_explicit_deck_count_boundary) {
    EXPECT_TRUE(texas::is_valid_card(static_cast<std::uint8_t>(texas::DECK_CARD_COUNT - 1U)));
    EXPECT_TRUE(!texas::is_valid_card(static_cast<std::uint8_t>(texas::DECK_CARD_COUNT)));
}

TEST_CASE(hunl_config_rejects_invalid_and_duplicate_board_cards) {
    auto out_of_range_card = valid_config();
    out_of_range_card.initial_board[0] = 52U;
    EXPECT_THROW(out_of_range_card.validate(), std::invalid_argument);

    auto high_card = valid_config();
    high_card.initial_board[0] = std::numeric_limits<std::uint8_t>::max();
    EXPECT_THROW(high_card.validate(), std::invalid_argument);

    auto duplicate = valid_config();
    duplicate.initial_board[1] = duplicate.initial_board[0];
    EXPECT_THROW(duplicate.validate(), std::invalid_argument);
}

TEST_CASE(hunl_config_rejects_more_than_twenty_unsafe_numeric_and_menu_contracts) {
    std::vector<texas::HUNLConfig> invalid;
    const auto add = [&invalid](auto mutate) {
        auto config = valid_config();
        mutate(config);
        invalid.push_back(std::move(config));
    };

    add([](auto& c) { c.starting_street = static_cast<texas::Street>(99); });
    add([](auto& c) { c.flat_solve_mode = static_cast<texas::HUNLFlatSolveMode>(99); });
    add([](auto& c) { c.range_policy = static_cast<texas::HUNLRangePolicy>(99); });
    add([](auto& c) { c.small_blind = c.big_blind + 1; });
    add([](auto& c) { c.big_blind = std::numeric_limits<int>::max(); c.ante = 1; });
    add([](auto& c) { c.big_blind = c.starting_stack + 1; });
    add([](auto& c) { c.starting_stack = std::numeric_limits<int>::max(); });
    add([](auto& c) { c.initial_contributions = {std::numeric_limits<int>::max(), 1}; });
    add([](auto& c) { c.min_bet_bb = 0; });
    add([](auto& c) { c.min_bet_bb = -1; });
    add([](auto& c) { c.force_allin_threshold = -1; });
    add([](auto& c) { c.min_bet_bb = std::numeric_limits<int>::max(); });
    add([](auto& c) { c.force_allin_threshold = std::numeric_limits<int>::max(); });
    add([](auto& c) { c.bet_size_fractions = {std::numeric_limits<double>::quiet_NaN()}; });
    add([](auto& c) { c.bet_size_fractions = {std::numeric_limits<double>::infinity()}; });
    add([](auto& c) { c.bet_size_fractions = {-0.01}; });
    add([](auto& c) { c.bet_size_fractions = {1e300}; });
    add([](auto& c) { c.flop_bet_fractions = std::vector<double>(6, 0.5); });
    add([](auto& c) { c.turn_bet_fractions = std::vector<double>{
        std::numeric_limits<double>::quiet_NaN()}; });
    add([](auto& c) { c.river_bet_fractions = std::vector<double>{-1.0}; });
    add([](auto& c) { c.raise_size_xs = {std::numeric_limits<double>::quiet_NaN()}; });
    add([](auto& c) { c.raise_size_xs = {-1.0}; });
    add([](auto& c) { c.raise_size_xs = {1e300}; });
    add([](auto& c) { c.raise_size_xs = std::vector<double>(6, 3.0); });
    add([](auto& c) { c.auto_all_in_spr_threshold =
        std::numeric_limits<double>::quiet_NaN(); });
    add([](auto& c) { c.auto_all_in_spr_threshold =
        std::numeric_limits<double>::infinity(); });
    add([](auto& c) { c.auto_all_in_spr_threshold = -1.0; });
    add([](auto& c) { c.preflop_raise_cap = 11; });
    add([](auto& c) { c.postflop_raise_cap = 11; });

    EXPECT_TRUE(invalid.size() > 20U);
    for (const auto& config : invalid) {
        EXPECT_THROW(config.validate(), std::invalid_argument);
    }
}

TEST_CASE(hunl_config_accepts_twenty_representable_nonnegative_bet_fractions) {
    for (int step = 0; step < 20; ++step) {
        auto config = valid_config();
        config.bet_size_fractions = {static_cast<double>(step) / 10.0};
        config.validate();
    }
}

TEST_CASE(hunl_public_action_math_rejects_nonfinite_and_overflowing_inputs) {
    for (const double value : {
             -1.0,
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             static_cast<double>(std::numeric_limits<int>::max()) + 1.0}) {
        EXPECT_THROW(texas::python_round_positive(value), std::invalid_argument);
    }

    texas::ActionContext context;
    context.pot = std::numeric_limits<int>::max();
    context.big_blind = 100;
    context.min_bet_bb = 1;
    for (int step = 1; step <= 20; ++step) {
        EXPECT_THROW(
            texas::bet_amount_for_fraction(context, static_cast<double>(step) + 1.0),
            std::invalid_argument);
    }
}

TEST_CASE(hunl_config_rejects_every_fixed_hole_card_collision) {
    auto duplicate_within_player = valid_config();
    (*duplicate_within_player.initial_hole_cards)[0][1] =
        (*duplicate_within_player.initial_hole_cards)[0][0];
    EXPECT_THROW(duplicate_within_player.validate(), std::invalid_argument);

    auto duplicate_between_players = valid_config();
    (*duplicate_between_players.initial_hole_cards)[1][0] =
        (*duplicate_between_players.initial_hole_cards)[0][0];
    EXPECT_THROW(duplicate_between_players.validate(), std::invalid_argument);

    auto overlap_with_board = valid_config();
    (*overlap_with_board.initial_hole_cards)[1][1] = overlap_with_board.initial_board[3];
    EXPECT_THROW(overlap_with_board.validate(), std::invalid_argument);

    auto invalid_hole = valid_config();
    (*invalid_hole.initial_hole_cards)[0][0] = std::numeric_limits<std::uint8_t>::max();
    EXPECT_THROW(invalid_hole.validate(), std::invalid_argument);
}

TEST_CASE(hunl_range_validation_rejects_invalid_cards_and_nonfinite_or_zero_mass_weights) {
    const auto valid_hole = std::array<std::uint8_t, 2>{
        texas::card_to_int(2, 0), texas::card_to_int(3, 1)};

    auto invalid_card = valid_range_config();
    invalid_card.initial_ranges[0] = single_hand_range(52U, valid_hole[1], 1.0);
    EXPECT_THROW(invalid_card.validate(), std::invalid_argument);

    auto board_blocked = valid_range_config();
    board_blocked.initial_ranges[1] = single_hand_range(board_blocked.initial_board[0], valid_hole[1], 1.0);
    EXPECT_THROW(board_blocked.validate(), std::invalid_argument);

    auto duplicate_hand = valid_range_config();
    duplicate_hand.initial_ranges[0] = single_hand_range(valid_hole[0], valid_hole[0], 1.0);
    EXPECT_THROW(duplicate_hand.validate(), std::invalid_argument);

    auto negative = valid_range_config();
    negative.initial_ranges[0] = single_hand_range(valid_hole[0], valid_hole[1], -1.0);
    EXPECT_THROW(negative.validate(), std::invalid_argument);

    auto nan_weight = valid_range_config();
    nan_weight.initial_ranges[0] = single_hand_range(
        valid_hole[0], valid_hole[1], std::numeric_limits<double>::quiet_NaN());
    EXPECT_THROW(nan_weight.validate(), std::invalid_argument);

    auto infinite_weight = valid_range_config();
    infinite_weight.initial_ranges[0] = single_hand_range(
        valid_hole[0], valid_hole[1], std::numeric_limits<double>::infinity());
    EXPECT_THROW(infinite_weight.validate(), std::invalid_argument);

    auto zero_mass = valid_range_config();
    zero_mass.initial_ranges[0] = single_hand_range(valid_hole[0], valid_hole[1], 0.0);
    EXPECT_THROW(zero_mass.validate(), std::invalid_argument);

    auto invalid_bucket_weight = valid_range_config();
    invalid_bucket_weight.flat_solve_mode = texas::HUNLFlatSolveMode::Bucketed;
    texas::HUNLRangeInput bucket_range;
    bucket_range.bucket_weights.push_back({texas::Street::River, 0U, std::numeric_limits<double>::infinity()});
    invalid_bucket_weight.initial_ranges[0] = bucket_range;
    EXPECT_THROW(invalid_bucket_weight.validate(), std::invalid_argument);

    auto zero_bucket_mass = valid_range_config();
    zero_bucket_mass.flat_solve_mode = texas::HUNLFlatSolveMode::Bucketed;
    texas::HUNLRangeInput zero_bucket_range;
    zero_bucket_range.bucket_weights.push_back({texas::Street::River, 0U, 0.0});
    zero_bucket_mass.initial_ranges[0] = zero_bucket_range;
    EXPECT_THROW(zero_bucket_mass.validate(), std::invalid_argument);
}

TEST_CASE(hunl_card_validation_protects_combo_masks_and_mutable_state_chance_paths) {
    EXPECT_THROW(texas::enumerate_combos({52U}), std::invalid_argument);
    EXPECT_THROW(texas::enumerate_combos({texas::card_to_int(2, 0), texas::card_to_int(2, 0)}),
                 std::invalid_argument);

    const auto combos = texas::enumerate_combos({texas::card_to_int(2, 0)});
    EXPECT_THROW(texas::dead_card_mask(combos, {std::numeric_limits<std::uint8_t>::max()}),
                 std::invalid_argument);
    EXPECT_THROW(texas::dead_card_mask(combos, {texas::card_to_int(3, 0), texas::card_to_int(3, 0)}),
                 std::invalid_argument);

    auto state = texas::HUNLState::initial(std::make_shared<const texas::HUNLConfig>(valid_config()));
    auto invalid_holes = *state.hole_cards;
    invalid_holes[1][1] = invalid_holes[0][0];
    EXPECT_THROW(state.clone_with_hole_cards(invalid_holes), std::invalid_argument);

    state.cur_player = -1;
    state.board[0] = std::numeric_limits<std::uint8_t>::max();
    EXPECT_THROW(state.chance_outcomes(), std::invalid_argument);
}
