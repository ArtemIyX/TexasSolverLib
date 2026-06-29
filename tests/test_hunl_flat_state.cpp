#include "solver/hunl_flat_state.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>

TEST_CASE(hunl_flat_infoset_table_builds_contiguous_arenas_from_graph) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {3, 5};

    const auto table = core::HUNLFlatInfosetTable::build(graph, hand_count_per_player);

    EXPECT_EQ(table.infoset_count(), graph.infosets.size());

    std::size_t expected_total = 0;
    for (std::size_t i = 0; i < graph.infosets.size(); ++i) {
        const auto& infoset = graph.infosets[i];
        const auto& meta = table.meta()[i];
        const auto expected_hand_count = hand_count_per_player[static_cast<std::size_t>(infoset.player)];
        const auto expected_values = expected_hand_count * static_cast<std::size_t>(infoset.action_count);

        EXPECT_EQ(meta.id.value, infoset.id.value);
        EXPECT_EQ(meta.offset, expected_total);
        EXPECT_EQ(meta.hand_count, expected_hand_count);
        EXPECT_EQ(meta.action_count, infoset.action_count);
        EXPECT_EQ(meta.player, infoset.player);
        EXPECT_EQ(meta.value_count, expected_values);

        expected_total += expected_values;
    }

    EXPECT_EQ(table.total_value_count(), expected_total);
}

TEST_CASE(hunl_flat_infoset_table_exposes_rw_rows_for_all_three_arenas) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {2, 4};

    auto table = core::HUNLFlatInfosetTable::build(graph, hand_count_per_player);
    const auto id = graph.infosets.front().id;
    const auto count = table.row_value_count(id);

    auto* regret = table.regret_mut(id);
    auto* strategy_sum = table.strategy_sum_mut(id);
    auto* current_strategy = table.current_strategy_mut(id);

    EXPECT_TRUE(count > 0);
    regret[0] = 1.25;
    strategy_sum[count - 1] = 2.5;
    current_strategy[0] = 0.75;

    EXPECT_EQ(table.regret(id)[0], 1.25);
    EXPECT_EQ(table.strategy_sum(id)[count - 1], 2.5);
    EXPECT_EQ(table.current_strategy(id)[0], 0.75);
}
