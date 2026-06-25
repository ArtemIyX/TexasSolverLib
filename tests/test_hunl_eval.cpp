#include "core/hunl.hpp"
#include "core/hunl_eval.hpp"
#include "test_harness.hpp"

#include <array>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

}

TEST_CASE(hunl_eval_category_ordering) {
    const auto high_card = core::Strength::evaluate_5({c(14, 0), c(11, 1), c(8, 2), c(5, 3), c(2, 0)});
    const auto pair = core::Strength::evaluate_5({c(8, 0), c(8, 1), c(14, 0), c(11, 1), c(2, 0)});
    const auto two_pair = core::Strength::evaluate_5({c(8, 0), c(8, 1), c(5, 0), c(5, 1), c(14, 0)});
    const auto trips = core::Strength::evaluate_5({c(8, 0), c(8, 1), c(8, 2), c(14, 0), c(2, 1)});
    const auto straight = core::Strength::evaluate_5({c(9, 0), c(8, 1), c(7, 2), c(6, 3), c(5, 0)});
    const auto flush = core::Strength::evaluate_5({c(14, 0), c(11, 0), c(8, 0), c(5, 0), c(2, 0)});
    const auto full_house = core::Strength::evaluate_5({c(14, 0), c(14, 1), c(14, 2), c(13, 0), c(13, 1)});
    const auto quads = core::Strength::evaluate_5({c(14, 0), c(14, 1), c(14, 2), c(14, 3), c(13, 0)});
    const auto straight_flush = core::Strength::evaluate_5({c(9, 0), c(8, 0), c(7, 0), c(6, 0), c(5, 0)});

    EXPECT_TRUE(high_card < pair);
    EXPECT_TRUE(pair < two_pair);
    EXPECT_TRUE(two_pair < trips);
    EXPECT_TRUE(trips < straight);
    EXPECT_TRUE(straight < flush);
    EXPECT_TRUE(flush < full_house);
    EXPECT_TRUE(full_house < quads);
    EXPECT_TRUE(quads < straight_flush);
}

TEST_CASE(hunl_eval_wheel_is_lowest_straight) {
    const auto wheel = core::Strength::evaluate_5({c(14, 0), c(2, 1), c(3, 2), c(4, 3), c(5, 0)});
    const auto six_high = core::Strength::evaluate_5({c(6, 1), c(2, 2), c(3, 3), c(4, 0), c(5, 1)});
    EXPECT_TRUE(wheel < six_high);
}

TEST_CASE(hunl_eval_seven_card_uses_best_five) {
    const std::array<std::uint8_t, 7> seven = {c(14, 0), c(11, 0), c(8, 0), c(5, 0), c(2, 0), c(7, 1), c(6, 2)};
    const std::array<std::uint8_t, 5> five = {c(14, 0), c(11, 0), c(8, 0), c(5, 0), c(2, 0)};
    EXPECT_EQ(core::Strength::evaluate_7(seven), core::Strength::evaluate_5(five));
}
