#include "core/poker.hpp"
#include "test_harness.hpp"

#include <string>

TEST_CASE(core_poker_domain_owns_street_and_card_operations) {
    const auto ace_spade = texas::core::card_to_int(14U, 0U);
    EXPECT_EQ(texas::core::rank_of(ace_spade), 14U);
    EXPECT_EQ(texas::core::suit_of(ace_spade), 0U);
    EXPECT_TRUE(texas::core::is_valid_card(ace_spade));
    EXPECT_TRUE(texas::core::are_valid_and_distinct_cards(&ace_spade, 1U));
    EXPECT_EQ(texas::core::cards_to_deal(texas::core::Street::Flop), 3U);
    EXPECT_EQ(texas::core::street_token(texas::core::Street::River), std::string{"r"});
}
