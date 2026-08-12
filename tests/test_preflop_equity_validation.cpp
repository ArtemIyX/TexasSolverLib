#include "preflop/preflop_equity.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::array<std::uint8_t, 2> HERO = {
    texas::card_to_int(14, 0),
    texas::card_to_int(13, 1),
};
constexpr std::array<std::uint8_t, 2> VILLAIN = {
    texas::card_to_int(12, 2),
    texas::card_to_int(11, 3),
};

}  // namespace

TEST_CASE(preflop_class_index_rejects_rank_zero) {
    EXPECT_THROW(texas::class_index(0, 2, false), std::invalid_argument);
}

TEST_CASE(preflop_class_index_rejects_rank_one) {
    EXPECT_THROW(texas::class_index(14, 1, false), std::invalid_argument);
}

TEST_CASE(preflop_class_index_rejects_rank_fifteen) {
    EXPECT_THROW(texas::class_index(15, 14, false), std::invalid_argument);
}

TEST_CASE(preflop_class_index_rejects_max_byte_rank) {
    EXPECT_THROW(texas::class_index(14, 255, true), std::invalid_argument);
}

TEST_CASE(preflop_class_decode_rejects_first_invalid_class) {
    EXPECT_THROW(
        texas::class_decode(static_cast<std::uint16_t>(texas::PREFLOP_NUM_CLASSES)),
        std::out_of_range);
}

TEST_CASE(preflop_class_decode_rejects_next_invalid_class) {
    EXPECT_THROW(texas::class_decode(170U), std::out_of_range);
}

TEST_CASE(preflop_class_decode_rejects_max_class_id) {
    EXPECT_THROW(
        texas::class_decode(std::numeric_limits<std::uint16_t>::max()),
        std::out_of_range);
}

TEST_CASE(preflop_hole_to_class_rejects_low_invalid_card) {
    EXPECT_THROW(texas::hole_to_class({52U, HERO[1]}), std::invalid_argument);
}

TEST_CASE(preflop_hole_to_class_rejects_high_invalid_card) {
    EXPECT_THROW(texas::hole_to_class({HERO[0], 60U}), std::invalid_argument);
}

TEST_CASE(preflop_hole_to_class_rejects_max_byte_card) {
    EXPECT_THROW(texas::hole_to_class({HERO[0], 255U}), std::invalid_argument);
}

TEST_CASE(preflop_hole_to_class_rejects_duplicate_cards) {
    EXPECT_THROW(texas::hole_to_class({HERO[0], HERO[0]}), std::invalid_argument);
}

TEST_CASE(preflop_build_hole_rep_rejects_invalid_hero_class) {
    EXPECT_THROW(texas::build_hole_rep(169U, 0U, 0U), std::out_of_range);
}

TEST_CASE(preflop_build_hole_rep_rejects_invalid_villain_class) {
    EXPECT_THROW(texas::build_hole_rep(0U, 169U, 0U), std::out_of_range);
}

TEST_CASE(preflop_build_hole_rep_rejects_first_invalid_variant) {
    EXPECT_THROW(texas::build_hole_rep(0U, 1U, 3U), std::out_of_range);
}

TEST_CASE(preflop_build_hole_rep_rejects_max_variant) {
    EXPECT_THROW(texas::build_hole_rep(0U, 1U, 255U), std::out_of_range);
}

TEST_CASE(preflop_exact_equity_rejects_invalid_hero_first_card) {
    EXPECT_THROW(
        texas::enumerate_pair_equity({255U, HERO[1]}, VILLAIN),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_invalid_hero_second_card) {
    EXPECT_THROW(
        texas::enumerate_pair_equity({HERO[0], 52U}, VILLAIN),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_invalid_villain_first_card) {
    EXPECT_THROW(
        texas::enumerate_pair_equity(HERO, {60U, VILLAIN[1]}),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_invalid_villain_second_card) {
    EXPECT_THROW(
        texas::enumerate_pair_equity(HERO, {VILLAIN[0], 255U}),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_duplicate_hero_cards) {
    EXPECT_THROW(
        texas::enumerate_pair_equity({HERO[0], HERO[0]}, VILLAIN),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_duplicate_villain_cards) {
    EXPECT_THROW(
        texas::enumerate_pair_equity(HERO, {VILLAIN[0], VILLAIN[0]}),
        std::invalid_argument);
}

TEST_CASE(preflop_exact_equity_rejects_cross_player_overlap) {
    EXPECT_THROW(
        texas::enumerate_pair_equity(HERO, {HERO[1], VILLAIN[1]}),
        std::invalid_argument);
}

TEST_CASE(preflop_monte_carlo_rejects_invalid_private_card) {
    EXPECT_THROW(
        texas::monte_carlo_pair_equity({255U, HERO[1]}, VILLAIN, 1U, 7U),
        std::invalid_argument);
}

TEST_CASE(preflop_monte_carlo_rejects_duplicate_private_card) {
    EXPECT_THROW(
        texas::monte_carlo_pair_equity(HERO, {HERO[0], VILLAIN[1]}, 1U, 7U),
        std::invalid_argument);
}

TEST_CASE(preflop_monte_carlo_rejects_zero_samples) {
    EXPECT_THROW(
        texas::monte_carlo_pair_equity(HERO, VILLAIN, 0U, 7U),
        std::invalid_argument);
}

TEST_CASE(preflop_equity_table_rejects_invalid_hero_coordinate) {
    texas::PreflopEquityTable table;
    EXPECT_THROW(table.at(texas::PREFLOP_NUM_CLASSES, 0U, 0U), std::out_of_range);
}

TEST_CASE(preflop_equity_table_rejects_invalid_villain_coordinate) {
    texas::PreflopEquityTable table;
    EXPECT_THROW(table.at(0U, texas::PREFLOP_NUM_CLASSES, 0U), std::out_of_range);
}

TEST_CASE(preflop_equity_table_rejects_invalid_variant_coordinate) {
    texas::PreflopEquityTable table;
    EXPECT_THROW(table.at(0U, 0U, texas::PREFLOP_NUM_VARIANTS), std::out_of_range);
}

TEST_CASE(preflop_equity_table_const_access_rejects_invalid_coordinate) {
    const texas::PreflopEquityTable table;
    EXPECT_THROW(table.at(std::numeric_limits<std::size_t>::max(), 0U, 0U), std::out_of_range);
}

TEST_CASE(preflop_equity_table_rejects_cleared_backing_storage) {
    texas::PreflopEquityTable table;
    table.data().clear();
    EXPECT_THROW(table.at(0U, 0U, 0U), std::logic_error);
}

TEST_CASE(preflop_equity_table_rejects_shortened_backing_storage) {
    texas::PreflopEquityTable table;
    table.data().resize(1U);
    EXPECT_THROW(table.at(0U, 0U, 0U), std::logic_error);
}

TEST_CASE(preflop_equity_table_rejects_extended_backing_storage) {
    texas::PreflopEquityTable table;
    table.data().push_back(0.5);
    EXPECT_THROW(table.at(0U, 0U, 0U), std::logic_error);
}
