#include "games/hunl_eval.hpp"
#include "test_harness.hpp"

#include <array>
#include <vector>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

}  // namespace

TEST_CASE(eval_five_card_ordering_and_bridge) {
    const std::array<std::uint8_t, 5> high_card = {c(14, 0), c(11, 1), c(8, 2), c(5, 3), c(2, 0)};
    const std::array<std::uint8_t, 5> pair = {c(8, 0), c(8, 1), c(14, 0), c(11, 1), c(2, 0)};
    const std::array<std::uint8_t, 5> straight_flush = {c(9, 0), c(8, 0), c(7, 0), c(6, 0), c(5, 0)};

    const auto s_high = core::evaluate_n(std::vector<std::uint8_t>(high_card.begin(), high_card.end()));
    const auto s_pair = core::evaluate_n(std::vector<std::uint8_t>(pair.begin(), pair.end()));
    const auto s_sf = core::evaluate_n(std::vector<std::uint8_t>(straight_flush.begin(), straight_flush.end()));

    EXPECT_TRUE(s_high < s_pair);
    EXPECT_TRUE(s_pair < s_sf);
    EXPECT_EQ(s_high, core::Strength::evaluate_5(high_card));
    EXPECT_EQ(s_pair, core::Strength::evaluate_5(pair));
    EXPECT_EQ(s_sf, core::Strength::evaluate_5(straight_flush));
}

TEST_CASE(eval_six_card_uses_fast_path) {
    const std::array<std::uint8_t, 6> strong = {c(14, 0), c(14, 1), c(13, 0), c(12, 1), c(11, 2), c(2, 3)};
    const std::array<std::uint8_t, 6> weak = {c(14, 0), c(13, 1), c(12, 0), c(9, 1), c(8, 2), c(2, 3)};

    const auto s_strong = core::evaluate_n(std::vector<std::uint8_t>(strong.begin(), strong.end()));
    const auto s_weak = core::evaluate_n(std::vector<std::uint8_t>(weak.begin(), weak.end()));

    EXPECT_TRUE(s_weak < s_strong);
}

TEST_CASE(eval_seven_card_uses_fast_path) {
    const std::array<std::uint8_t, 7> seven = {c(14, 0), c(11, 0), c(8, 0), c(5, 0), c(2, 0), c(7, 1), c(6, 2)};
    const auto s_7 = core::Strength::evaluate_7(seven);
    const auto s_n = core::evaluate_n(std::vector<std::uint8_t>(seven.begin(), seven.end()));

    EXPECT_EQ(s_n, s_7);
}

TEST_CASE(eval_eight_card_falls_back) {
    const std::vector<std::uint8_t> eight = {c(14, 0), c(14, 1), c(13, 0), c(12, 1), c(11, 2), c(10, 3), c(9, 0), c(2, 1)};
    const auto s8 = core::evaluate_n(eight);
    const auto s7 = core::Strength::evaluate_7({c(14, 0), c(14, 1), c(13, 0), c(12, 1), c(11, 2), c(10, 3), c(9, 0)});

    EXPECT_TRUE(s8 >= s7);
}
