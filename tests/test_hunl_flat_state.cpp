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
    EXPECT_EQ(table.meta()[id.value].last_discount_iter, 0U);
}

TEST_CASE(hunl_flat_infoset_table_value_index_matches_selected_layout) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {2, 3};

    const auto hand_action = core::HUNLFlatInfosetTable::build(
        graph, hand_count_per_player, core::HUNLFlatValueLayout::InfosetHandAction);
    const auto action_hand = core::HUNLFlatInfosetTable::build(
        graph, hand_count_per_player, core::HUNLFlatValueLayout::InfosetActionHand);

    const auto id = graph.infosets.front().id;
    const auto& meta0 = hand_action.meta()[id.value];
    EXPECT_EQ(hand_action.layout(), core::HUNLFlatValueLayout::InfosetHandAction);
    EXPECT_EQ(action_hand.layout(), core::HUNLFlatValueLayout::InfosetActionHand);

    EXPECT_EQ(hand_action.value_index(id, 0, 0), meta0.offset);
    EXPECT_EQ(hand_action.value_index(id, 1, 0), meta0.offset + meta0.action_count);
    EXPECT_EQ(hand_action.value_index(id, 1, 1), meta0.offset + meta0.action_count + 1U);

    EXPECT_EQ(action_hand.value_index(id, 0, 0), meta0.offset);
    EXPECT_EQ(action_hand.value_index(id, 1, 0), meta0.offset + 1U);
    EXPECT_EQ(action_hand.value_index(id, 0, 1), meta0.offset + meta0.hand_count);
}

TEST_CASE(hunl_flat_infoset_table_maps_infoset_ranges_to_contiguous_value_ranges) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {2, 3};
    const auto table = core::HUNLFlatInfosetTable::build(graph, hand_count_per_player);

    const core::HUNLFlatRange infoset_range{0, static_cast<std::uint32_t>(std::min<std::size_t>(2, graph.infosets.size()))};
    const auto value_range = table.infoset_value_range(infoset_range);

    if (infoset_range.begin < infoset_range.end) {
        const auto& first = table.meta()[infoset_range.begin];
        const auto& last = table.meta()[infoset_range.end - 1];
        EXPECT_EQ(value_range.begin, first.offset);
        EXPECT_EQ(value_range.end, last.offset + last.value_count);
    } else {
        EXPECT_EQ(value_range.begin, 0U);
        EXPECT_EQ(value_range.end, 0U);
    }
}

TEST_CASE(hunl_flat_parallel_plan_assigns_disjoint_infoset_and_depth_ranges) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto plan = core::HUNLFlatParallelPlan::build(graph, 4);

    EXPECT_EQ(plan.workers.size(), 4U);

    std::uint32_t infoset_cursor = 0;
    std::uint32_t node_cursor = 0;
    for (const auto& worker : plan.workers) {
        EXPECT_EQ(worker.infoset_range.begin, infoset_cursor);
        EXPECT_TRUE(worker.infoset_range.begin <= worker.infoset_range.end);
        infoset_cursor = worker.infoset_range.end;

        EXPECT_EQ(worker.node_range.begin, node_cursor);
        EXPECT_TRUE(worker.node_range.begin <= worker.node_range.end);
        node_cursor = worker.node_range.end;
        EXPECT_EQ(worker.depth_node_ranges.size(), graph.depth_slices.size());
    }
    EXPECT_EQ(infoset_cursor, static_cast<std::uint32_t>(graph.infosets.size()));
    EXPECT_EQ(node_cursor, static_cast<std::uint32_t>(graph.nodes.size()));

    for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
        const auto slice = graph.depth_slices[depth];
        std::uint32_t cursor = slice.begin;
        for (const auto& worker : plan.workers) {
            const auto range = worker.depth_node_ranges[depth];
            EXPECT_EQ(range.begin, cursor);
            EXPECT_TRUE(range.begin <= range.end);
            EXPECT_TRUE(range.end <= slice.begin + slice.count);
            cursor = range.end;
        }
        EXPECT_EQ(cursor, slice.begin + slice.count);
    }
}

TEST_CASE(hunl_flat_worker_scratch_reset_clears_temporary_buffers) {
    core::HUNLFlatWorkerScratch scratch;
    scratch.terminal_values.assign(3, 1.0);
    scratch.node_values.assign(4, 2.0);
    scratch.action_values.assign(5, 3.0);
    scratch.player0_reach.assign(6, 4.0);
    scratch.player1_reach.assign(7, 5.0);
    scratch.chance_reach.assign(8, 6.0);

    scratch.reset();

    EXPECT_TRUE(scratch.terminal_values.empty());
    EXPECT_TRUE(scratch.node_values.empty());
    EXPECT_TRUE(scratch.action_values.empty());
    EXPECT_TRUE(scratch.player0_reach.empty());
    EXPECT_TRUE(scratch.player1_reach.empty());
    EXPECT_TRUE(scratch.chance_reach.empty());
}
