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
        EXPECT_EQ(worker.depth_reduce_ranges.size(), graph.depth_slices.size());
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

    for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
        if (depth + 1 >= graph.depth_slices.size()) {
            for (const auto& worker : plan.workers) {
                EXPECT_EQ(worker.depth_reduce_ranges[depth].begin, 0U);
                EXPECT_EQ(worker.depth_reduce_ranges[depth].end, 0U);
            }
            continue;
        }

        const auto slice = graph.depth_slices[depth + 1];
        std::uint32_t cursor = slice.begin;
        for (const auto& worker : plan.workers) {
            const auto range = worker.depth_reduce_ranges[depth];
            EXPECT_EQ(range.begin, cursor);
            EXPECT_TRUE(range.begin <= range.end);
            EXPECT_TRUE(range.end <= slice.begin + slice.count);
            cursor = range.end;
        }
        EXPECT_EQ(cursor, slice.begin + slice.count);
    }
}

TEST_CASE(hunl_flat_parallel_plan_derives_contiguous_bucket_and_value_ranges_from_infosets) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {2, 3};
    const auto table = core::HUNLFlatInfosetTable::build(graph, bucket_count_per_player);
    const auto plan = core::HUNLFlatParallelPlan::build(graph, table, 4);

    EXPECT_EQ(plan.workers.size(), 4U);

    std::uint32_t bucket_cursor = 0;
    std::uint32_t value_cursor = 0;
    for (const auto& worker : plan.workers) {
        EXPECT_TRUE(worker.bucket_range.begin <= worker.bucket_range.end);
        EXPECT_TRUE(worker.value_range.begin <= worker.value_range.end);

        if (worker.infoset_range.begin == worker.infoset_range.end) {
            EXPECT_EQ(worker.bucket_range.begin, worker.bucket_range.end);
            EXPECT_EQ(worker.value_range.begin, worker.value_range.end);
            continue;
        }

        EXPECT_EQ(worker.bucket_range.begin, bucket_cursor);
        EXPECT_EQ(worker.value_range.begin, value_cursor);
        bucket_cursor = worker.bucket_range.end;
        value_cursor = worker.value_range.end;
    }

    EXPECT_EQ(bucket_cursor, static_cast<std::uint32_t>(table.total_bucket_count()));
    EXPECT_EQ(value_cursor, static_cast<std::uint32_t>(table.total_value_count()));
}

TEST_CASE(hunl_flat_worker_scratch_reuses_and_zeros_temporary_buffers) {
    core::HUNLFlatWorkerScratch scratch;
    scratch.ensure_capacity(6, 5, 4, 3, 2);

    scratch.terminal_values[0] = 1.0;
    scratch.node_values[1] = 2.0;
    scratch.action_values[2] = 3.0;
    scratch.player0_reach[3] = 4.0;
    scratch.player1_reach[4] = 5.0;
    scratch.chance_reach[5] = 6.0;

    scratch.reset_values();

    EXPECT_EQ(scratch.terminal_values.size(), 6U);
    EXPECT_EQ(scratch.node_values.size(), 6U);
    EXPECT_EQ(scratch.action_values.size(), 5U);
    EXPECT_EQ(scratch.player0_reach.size(), 6U);
    EXPECT_EQ(scratch.player1_reach.size(), 6U);
    EXPECT_EQ(scratch.chance_reach.size(), 6U);
    EXPECT_EQ(scratch.bucket_reach.size(), 4U);
    EXPECT_EQ(scratch.row_values.size(), 3U);
    EXPECT_EQ(scratch.row_weights.size(), 3U);
    EXPECT_EQ(scratch.local_bucket_mass.size(), 2U);
    EXPECT_EQ(scratch.terminal_values[0], 0.0);
    EXPECT_EQ(scratch.node_values[1], 0.0);
    EXPECT_EQ(scratch.action_values[2], 0.0);
    EXPECT_EQ(scratch.player0_reach[3], 0.0);
    EXPECT_EQ(scratch.player1_reach[4], 0.0);
    EXPECT_EQ(scratch.chance_reach[5], 0.0);
}
