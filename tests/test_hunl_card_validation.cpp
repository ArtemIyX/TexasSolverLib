#include "games/hunl.hpp"
#include "ranges/propagation.hpp"
#include "test_harness.hpp"

#include <limits>
#include <memory>
#include <stdexcept>

namespace {

core::HUNLConfig valid_config() {
    return core::default_tiny_subgame();
}

core::HUNLRangeInput single_hand_range(std::uint8_t first, std::uint8_t second, double weight) {
    core::HUNLRangeInput range;
    range.hand_weights.push_back({{first, second}, weight});
    return range;
}

core::HUNLConfig valid_range_config() {
    auto config = valid_config();
    config.initial_hole_cards = std::nullopt;
    config.range_policy = core::HUNLRangePolicy::RequireExplicit;
    config.initial_ranges[0] = single_hand_range(core::card_to_int(14, 1), core::card_to_int(13, 3), 1.0);
    config.initial_ranges[1] = single_hand_range(core::card_to_int(12, 1), core::card_to_int(11, 3), 1.0);
    return config;
}

}  // namespace

TEST_CASE(hunl_card_validation_recognizes_exact_encoding_bounds) {
    EXPECT_TRUE(!core::is_valid_card(0));
    EXPECT_TRUE(!core::is_valid_card(core::card_to_int(1, 3)));
    EXPECT_TRUE(core::is_valid_card(core::card_to_int(2, 0)));
    EXPECT_TRUE(core::is_valid_card(core::card_to_int(14, 3)));
    EXPECT_TRUE(!core::is_valid_card(core::card_to_int(15, 0)));
    EXPECT_TRUE(!core::is_valid_card(std::numeric_limits<std::uint8_t>::max()));
}

TEST_CASE(hunl_config_rejects_invalid_and_duplicate_board_cards) {
    auto low_card = valid_config();
    low_card.initial_board[0] = 0;
    EXPECT_THROW(low_card.validate(), std::invalid_argument);

    auto high_card = valid_config();
    high_card.initial_board[0] = std::numeric_limits<std::uint8_t>::max();
    EXPECT_THROW(high_card.validate(), std::invalid_argument);

    auto duplicate = valid_config();
    duplicate.initial_board[1] = duplicate.initial_board[0];
    EXPECT_THROW(duplicate.validate(), std::invalid_argument);
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
        core::card_to_int(2, 0), core::card_to_int(3, 1)};

    auto invalid_card = valid_range_config();
    invalid_card.initial_ranges[0] = single_hand_range(0, valid_hole[1], 1.0);
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
    invalid_bucket_weight.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    core::HUNLRangeInput bucket_range;
    bucket_range.bucket_weights.push_back({core::Street::River, 0U, std::numeric_limits<double>::infinity()});
    invalid_bucket_weight.initial_ranges[0] = bucket_range;
    EXPECT_THROW(invalid_bucket_weight.validate(), std::invalid_argument);

    auto zero_bucket_mass = valid_range_config();
    zero_bucket_mass.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    core::HUNLRangeInput zero_bucket_range;
    zero_bucket_range.bucket_weights.push_back({core::Street::River, 0U, 0.0});
    zero_bucket_mass.initial_ranges[0] = zero_bucket_range;
    EXPECT_THROW(zero_bucket_mass.validate(), std::invalid_argument);
}

TEST_CASE(hunl_card_validation_protects_combo_masks_and_mutable_state_chance_paths) {
    EXPECT_THROW(core::enumerate_combos({0}), std::invalid_argument);
    EXPECT_THROW(core::enumerate_combos({core::card_to_int(2, 0), core::card_to_int(2, 0)}),
                 std::invalid_argument);

    const auto combos = core::enumerate_combos({core::card_to_int(2, 0)});
    EXPECT_THROW(core::dead_card_mask(combos, {std::numeric_limits<std::uint8_t>::max()}),
                 std::invalid_argument);
    EXPECT_THROW(core::dead_card_mask(combos, {core::card_to_int(3, 0), core::card_to_int(3, 0)}),
                 std::invalid_argument);

    auto state = core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(valid_config()));
    auto invalid_holes = *state.hole_cards;
    invalid_holes[1][1] = invalid_holes[0][0];
    EXPECT_THROW(state.clone_with_hole_cards(invalid_holes), std::invalid_argument);

    state.cur_player = -1;
    state.board[0] = std::numeric_limits<std::uint8_t>::max();
    EXPECT_THROW(state.chance_outcomes(), std::invalid_argument);
}
