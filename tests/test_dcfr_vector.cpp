#include "core/dcfr_vector.hpp"
#include "test_harness.hpp"

#include <memory>
#include <stdexcept>

TEST_CASE(dcfr_vector_infoset_data_allocates_row_major_storage) {
    const core::VectorInfosetData info(3, 5);
    EXPECT_EQ(info.action_count, 3U);
    EXPECT_EQ(info.hand_count, 5U);
    EXPECT_EQ(info.regret.size(), 15U);
    EXPECT_EQ(info.strategy_sum.size(), 15U);
    EXPECT_EQ(info.last_discount_iter, 0U);
}

TEST_CASE(dcfr_vector_eval_context_from_hand_lists_preserves_order_and_strings) {
    const std::vector<std::array<std::uint8_t, 2>> p0 = {{
        {core::card_to_int(14, 1), core::card_to_int(13, 3)},
        {core::card_to_int(12, 2), core::card_to_int(12, 1)},
    }};
    const std::vector<std::array<std::uint8_t, 2>> p1 = {{
        {core::card_to_int(11, 0), core::card_to_int(10, 0)},
    }};

    const auto ctx = core::EvalContext::from_hand_lists(p0, p1, 100);
    EXPECT_EQ(ctx.hand_count[0], 2U);
    EXPECT_EQ(ctx.hand_count[1], 1U);
    EXPECT_EQ(ctx.big_blind, 100);
    EXPECT_EQ(ctx.hole[0][0][0], core::card_to_int(14, 1));
    EXPECT_EQ(ctx.hole_str[0][0], std::string("KcAh"));
    EXPECT_EQ(ctx.hole_str[0][1], std::string("QhQd"));
    EXPECT_EQ(ctx.hole_str[1][0], std::string("TsJs"));
}

TEST_CASE(dcfr_vector_eval_context_from_root_enumerates_board_disjoint_holes) {
    auto config = std::make_shared<core::HUNLConfig>(core::default_tiny_subgame());
    config->initial_hole_cards = std::nullopt;
    const auto initial = core::HUNLState::initial(config);

    const auto ctx = core::EvalContext::from_root(initial);
    EXPECT_EQ(ctx.big_blind, config->big_blind);
    EXPECT_EQ(ctx.hand_count[0], 1081U);
    EXPECT_EQ(ctx.hand_count[1], 1081U);
    EXPECT_EQ(ctx.hole[0].size(), ctx.hole_str[0].size());
    EXPECT_EQ(ctx.hole[1].size(), ctx.hole_str[1].size());
}

TEST_CASE(dcfr_vector_eval_context_from_suit_iso_is_not_implemented_yet) {
    const auto initial = core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame()));
    EXPECT_THROW(core::EvalContext::from_suit_iso(initial), std::logic_error);
}
