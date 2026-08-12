#include "core/card.hpp"
#include "test_harness.hpp"

#include <stdexcept>

TEST_CASE(card_round_trips_all_compact_indices) {
    for (std::uint8_t index = 0U; index < texas::Card::COUNT; ++index) {
        const auto card = texas::Card::from_index(index);
        EXPECT_EQ(card.index(), index);
        EXPECT_EQ(texas::Card::from_rank_suit(card.rank(), card.suit()), card);
    }
}

TEST_CASE(card_rejects_invalid_components) {
    EXPECT_THROW(texas::Card::from_index(52U), std::invalid_argument);
    EXPECT_THROW(texas::Card::from_rank_suit(1U, 0U), std::invalid_argument);
    EXPECT_THROW(texas::Card::from_rank_suit(15U, 0U), std::invalid_argument);
    EXPECT_THROW(texas::Card::from_rank_suit(2U, 4U), std::invalid_argument);
}
