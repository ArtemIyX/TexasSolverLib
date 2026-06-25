#include "core/preflop.hpp"
#include "core/preflop_equity.hpp"
#include "core/preflop_rvr.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

}  // namespace

TEST_CASE(preflop_class_index_roundtrip_basic) {
    EXPECT_EQ(core::class_index(14, 14, false), 0U);
    EXPECT_EQ(core::class_index(2, 2, false), 12U);
    EXPECT_EQ(core::class_index(14, 13, true), 13U);
    EXPECT_EQ(core::class_index(14, 13, false), 91U);

    const auto [hi, lo, suited] = core::class_decode(13);
    EXPECT_EQ(hi, 14U);
    EXPECT_EQ(lo, 13U);
    EXPECT_TRUE(suited);
}

TEST_CASE(preflop_hole_to_class_canonicalizes_suits) {
    EXPECT_EQ(core::hole_to_class({c(14, 0), c(14, 1)}), 0U);
    EXPECT_EQ(core::hole_to_class({c(14, 0), c(13, 0)}), 13U);
    EXPECT_EQ(core::hole_to_class({c(7, 1), c(2, 2)}), core::class_index(7, 2, false));
}

TEST_CASE(preflop_equity_table_builds_with_expected_shape) {
    const auto table = core::PreflopEquityTable::build();
    EXPECT_EQ(table.data().size(), core::PREFLOP_NUM_CLASSES * core::PREFLOP_NUM_CLASSES * core::PREFLOP_NUM_VARIANTS);
    EXPECT_TRUE(!table.empty());
    EXPECT_EQ(table.at(0, 0, 0), 0.5);
}

TEST_CASE(preflop_solver_entrypoints_smoke) {
    core::HUNLConfig cfg;
    cfg.starting_street = core::Street::Preflop;
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {c(14, 0), c(14, 1)},
        {c(13, 2), c(13, 3)},
    }};

    const auto preflop = core::solve_hunl_preflop(cfg, 3, 1.5, 0.0, 2.0);
    EXPECT_EQ(preflop.base.iterations, 3U);
    EXPECT_TRUE(preflop.infoset_count > 0);

    const auto table = core::PreflopEquityTable::build();
    const auto rvr = core::solve_hunl_preflop_rvr(cfg, table, 2, 1.5, 0.0, 2.0);
    EXPECT_TRUE(rvr.decision_node_count > 0);
}

TEST_CASE(preflop_class169_combo_builder_covers_full_deck) {
    const auto combos = core::Class169Combos::build();
    std::size_t total = 0;
    for (const auto& bucket : combos.combos) {
        total += bucket.size();
    }
    EXPECT_EQ(total, 1326U);
    EXPECT_EQ(combos.combos[0].size(), 6U);
    EXPECT_EQ(combos.combos[13].size(), 4U);
    EXPECT_EQ(combos.combos[13 + 78].size(), 12U);
}

TEST_CASE(preflop_class169_leaf_payoff_smoke) {
    const auto combos = core::Class169Combos::build();
    core::PreflopEquityTable table;
    table.at(0, 1, 0) = 0.813;
    const std::array<int, 2> contrib = {1000, 1000};
    const auto payoff = core::build_class169_leaf_payoff(contrib, 100, 0, {0, 0}, table, combos);
    EXPECT_EQ(payoff[0].size(), core::PREFLOP_NUM_CLASSES * core::PREFLOP_NUM_CLASSES);
    EXPECT_EQ(payoff[1].size(), core::PREFLOP_NUM_CLASSES * core::PREFLOP_NUM_CLASSES);
}
