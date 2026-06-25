#include "core/dcfr_vector.hpp"
#include "core/exploit.hpp"
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

TEST_CASE(dcfr_vector_compute_strategy_matches_row_regret_matching) {
    core::VectorInfosetData info(3, 2);
    info.regret = {
        -1.0, 3.0, 1.0,
        0.0, 0.0, 0.0,
    };
    std::vector<double> out;
    core::VectorDCFR::compute_strategy(info, out);
    EXPECT_EQ(out.size(), 6U);
    EXPECT_NEAR(out[0], 0.0, 1e-12);
    EXPECT_NEAR(out[1], 0.75, 1e-12);
    EXPECT_NEAR(out[2], 0.25, 1e-12);
    EXPECT_NEAR(out[3], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(out[4], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(out[5], 1.0 / 3.0, 1e-12);
}

TEST_CASE(dcfr_vector_compute_avg_strategy_normalizes_per_hand) {
    core::VectorInfosetData info(2, 2);
    info.strategy_sum = {
        2.0, 6.0,
        0.0, 0.0,
    };
    std::vector<double> out;
    core::VectorDCFR::compute_avg_strategy(info, out);
    EXPECT_NEAR(out[0], 0.25, 1e-12);
    EXPECT_NEAR(out[1], 0.75, 1e-12);
    EXPECT_NEAR(out[2], 0.5, 1e-12);
    EXPECT_NEAR(out[3], 0.5, 1e-12);
}

TEST_CASE(dcfr_vector_discount_updates_regret_and_strategy_sum) {
    core::VectorInfosetData info(2, 1);
    info.regret = {4.0, -4.0};
    info.strategy_sum = {8.0, 2.0};
    core::VectorDCFR::discount(info, 1, 1.5, 0.0, 2.0);
    EXPECT_NEAR(info.regret[0], 2.0, 1e-12);
    EXPECT_NEAR(info.regret[1], -2.0, 1e-12);
    EXPECT_NEAR(info.strategy_sum[0], 2.0, 1e-12);
    EXPECT_NEAR(info.strategy_sum[1], 0.5, 1e-12);
    EXPECT_EQ(info.last_discount_iter, 1U);
}

TEST_CASE(dcfr_vector_with_init_noise_allocates_decision_infosets) {
    auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto state = core::HUNLState::initial(config);
    const auto tree = core::BettingTree::build_from(state);
    const auto solver = core::VectorDCFR::with_init_noise(tree, {2U, 3U}, 1.5, 0.0, 2.0, 0.1, 7);
    EXPECT_EQ(solver.infosets.size(), tree.nodes.size());
    bool saw_decision = false;
    bool saw_nonzero = false;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].tag == core::FlatNodeTag::Decision) {
            saw_decision = true;
            EXPECT_TRUE(solver.infosets[i].has_value());
            const auto& info = *solver.infosets[i];
            EXPECT_EQ(info.action_count, tree.nodes[i].actions.size());
            EXPECT_EQ(info.hand_count, tree.nodes[i].player == 0 ? 2U : 3U);
            for (double v : info.regret) {
                if (v != 0.0) {
                    saw_nonzero = true;
                    break;
                }
            }
        } else {
            EXPECT_TRUE(!solver.infosets[i].has_value());
        }
    }
    EXPECT_TRUE(saw_decision);
    EXPECT_TRUE(saw_nonzero);
}
