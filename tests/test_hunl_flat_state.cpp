#include "solver/hunl_flat_state.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>

namespace {

core::HUNLFlatSolveGraph make_cost_skew_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 1;
    graph.max_actions = 4;

    graph.node_meta.resize(4);
    graph.infosets = {
        core::HUNLFlatInfoset{core::InfosetId{0}, 0, 1, {}, 0, 0, core::Street::Flop, 4},
        core::HUNLFlatInfoset{core::InfosetId{1}, 1, 1, {}, 1, 0, core::Street::Flop, 4},
        core::HUNLFlatInfoset{core::InfosetId{2}, 2, 1, {}, 2, 0, core::Street::Flop, 1},
        core::HUNLFlatInfoset{core::InfosetId{3}, 3, 1, {}, 3, 0, core::Street::Flop, 1},
    };
    graph.infoset_debug_keys = {"a", "b", "c", "d"};
    graph.infoset_nodes = {0, 1, 2, 3};
    graph.depth_order = {0, 1, 2, 3};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {3, 2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.depth_slices = {core::HUNLFlatSlice{0, 4}};
    graph.node_depths = {0, 0, 0, 0};
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] = core::HUNLFlatSlice{0, 4};

    auto set_decision = [&](std::size_t node_idx, core::InfosetId infoset_id, std::uint8_t action_count) {
        graph.node_meta[node_idx].infoset_id = infoset_id;
        graph.node_meta[node_idx].type = core::HUNLFlatNodeType::Decision;
        graph.node_meta[node_idx].player = 0;
        graph.node_meta[node_idx].action_count = action_count;
        graph.node_meta[node_idx].has_infoset = true;
        graph.node_meta[node_idx].street = core::Street::Flop;
    };

    set_decision(0, core::InfosetId{0}, 4);
    set_decision(1, core::InfosetId{1}, 4);
    set_decision(2, core::InfosetId{2}, 1);
    set_decision(3, core::InfosetId{3}, 1);
    return graph;
}

}  // namespace

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

TEST_CASE(hunl_flat_infoset_table_float32_precision_halves_dense_storage) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {16, 16};

    const auto table_f64 = core::HUNLFlatInfosetTable::build(
        graph,
        hand_count_per_player,
        core::HUNLFlatValueLayout::InfosetHandAction,
        core::HUNLFlatStoragePrecision::Float64);
    const auto table_f32 = core::HUNLFlatInfosetTable::build(
        graph,
        hand_count_per_player,
        core::HUNLFlatValueLayout::InfosetHandAction,
        core::HUNLFlatStoragePrecision::Float32);

    EXPECT_EQ(table_f64.precision(), core::HUNLFlatStoragePrecision::Float64);
    EXPECT_EQ(table_f32.precision(), core::HUNLFlatStoragePrecision::Float32);
    EXPECT_EQ(table_f32.regret_storage_bytes() * 2ULL, table_f64.regret_storage_bytes());
    EXPECT_EQ(table_f32.strategy_sum_storage_bytes() * 2ULL, table_f64.strategy_sum_storage_bytes());
    EXPECT_EQ(table_f32.current_strategy_storage_bytes() * 2ULL, table_f64.current_strategy_storage_bytes());
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
    EXPECT_EQ(node_cursor, static_cast<std::uint32_t>(graph.node_meta.size()));

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

TEST_CASE(hunl_flat_parallel_plan_weights_depth_ranges_by_estimated_backward_cost) {
    const auto graph = make_cost_skew_graph();
    const auto table = core::HUNLFlatInfosetTable::build(graph, {16, 16}, core::HUNLFlatValueLayout::InfosetActionHand);
    const auto plan = core::HUNLFlatParallelPlan::build(graph, table, 2);

    EXPECT_EQ(plan.workers.size(), 2U);
    EXPECT_EQ(plan.workers[0].depth_node_ranges.size(), 1U);
    EXPECT_EQ(plan.workers[1].depth_node_ranges.size(), 1U);

    const auto first = plan.workers[0].depth_node_ranges[0];
    const auto second = plan.workers[1].depth_node_ranges[0];
    EXPECT_EQ(first.begin, 0U);
    EXPECT_EQ(second.end, 4U);
    EXPECT_EQ(first.end, second.begin);

    const auto first_cost = plan.workers[0].depth_backward_costs[0];
    const auto second_cost = plan.workers[1].depth_backward_costs[0];
    EXPECT_TRUE(first.end - first.begin < 3U);
    EXPECT_TRUE(second.end - second.begin >= 2U);
    EXPECT_TRUE(first_cost > 0U);
    EXPECT_TRUE(second_cost > 0U);
    EXPECT_TRUE(first_cost <= second_cost * 2U);
    EXPECT_TRUE(second_cost <= first_cost * 2U);
}

TEST_CASE(hunl_flat_parallel_plan_estimated_backward_cost_matches_node_types) {
    const auto graph = make_cost_skew_graph();
    const auto table = core::HUNLFlatInfosetTable::build(graph, {16, 16}, core::HUNLFlatValueLayout::InfosetActionHand);

    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[0], table), 64U);
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[1], table), 64U);
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[2], table), 16U);
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[3], table), 16U);

    core::HUNLFlatNodeMeta chance_meta;
    chance_meta.type = core::HUNLFlatNodeType::Chance;
    chance_meta.chance_count = 5;
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(chance_meta, table), 5U);

    core::HUNLFlatNodeMeta fold_meta;
    fold_meta.type = core::HUNLFlatNodeType::TerminalFold;
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(fold_meta, table), 1U);

    core::HUNLFlatNodeMeta showdown_meta;
    showdown_meta.type = core::HUNLFlatNodeType::TerminalShowdown;
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(showdown_meta, table), 1U);

    core::HUNLFlatNodeMeta depth_limited_meta;
    depth_limited_meta.type = core::HUNLFlatNodeType::DepthLimited;
    EXPECT_EQ(core::HUNLFlatParallelPlan::estimated_backward_cost(depth_limited_meta, table), 1U);
}

TEST_CASE(hunl_flat_parallel_plan_depth_backward_costs_cover_full_depth_cost) {
    const auto graph = make_cost_skew_graph();
    const auto table = core::HUNLFlatInfosetTable::build(graph, {16, 16}, core::HUNLFlatValueLayout::InfosetActionHand);
    const auto plan = core::HUNLFlatParallelPlan::build(graph, table, 3);

    EXPECT_EQ(plan.workers.size(), 3U);
    EXPECT_EQ(graph.depth_slices.size(), 1U);

    std::uint64_t total_expected_cost = 0;
    for (const auto node_idx : graph.depth_order) {
        total_expected_cost += core::HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[node_idx], table);
    }

    std::uint64_t total_planned_cost = 0;
    std::uint32_t cursor = graph.depth_slices[0].begin;
    for (const auto& worker : plan.workers) {
        const auto range = worker.depth_node_ranges[0];
        EXPECT_EQ(range.begin, cursor);
        EXPECT_TRUE(range.begin <= range.end);
        total_planned_cost += worker.depth_backward_costs[0];
        cursor = range.end;
    }

    EXPECT_EQ(cursor, graph.depth_slices[0].begin + graph.depth_slices[0].count);
    EXPECT_EQ(total_planned_cost, total_expected_cost);
}

TEST_CASE(hunl_flat_worker_scratch_reuses_and_zeros_temporary_buffers) {
    core::HUNLFlatWorkerScratch scratch;
    scratch.ensure_capacity(6, 5, 4, 3, 2);

    scratch.player0_reach[3] = 1.0;
    scratch.player1_reach[4] = 2.0;
    scratch.chance_reach[5] = 3.0;
    scratch.bucket_reach[1] = 4.0;
    scratch.dirty_nodes.push_back(3);
    scratch.dirty_buckets.push_back(1);

    scratch.reset_values();

    EXPECT_EQ(scratch.player0_reach.size(), 6U);
    EXPECT_EQ(scratch.player1_reach.size(), 6U);
    EXPECT_EQ(scratch.chance_reach.size(), 6U);
    EXPECT_EQ(scratch.bucket_reach.size(), 4U);
    EXPECT_EQ(scratch.row_values.size(), 3U);
    EXPECT_EQ(scratch.row_weights.size(), 3U);
    EXPECT_EQ(scratch.local_bucket_mass.size(), 2U);
    EXPECT_TRUE(scratch.dirty_nodes.empty());
    EXPECT_TRUE(scratch.dirty_buckets.empty());
    EXPECT_EQ(scratch.player0_reach[3], 0.0);
    EXPECT_EQ(scratch.player1_reach[4], 0.0);
    EXPECT_EQ(scratch.chance_reach[5], 0.0);
    EXPECT_EQ(scratch.bucket_reach[1], 0.0);
}

TEST_CASE(hunl_flat_worker_scratch_ensure_capacity_sizes_row_buffers_and_resets_dirty_tracking) {
    core::HUNLFlatWorkerScratch scratch;
    scratch.ensure_capacity(2, 1, 3, 2, 2);
    scratch.dirty_nodes.push_back(1);
    scratch.dirty_buckets.push_back(2);
    scratch.row_values[0] = 7.0;
    scratch.row_weights[1] = 8.0;
    scratch.local_bucket_mass[1] = 9.0;

    scratch.ensure_capacity(5, 4, 6, 7, 8);

    EXPECT_EQ(scratch.player0_reach.size(), 5U);
    EXPECT_EQ(scratch.player1_reach.size(), 5U);
    EXPECT_EQ(scratch.chance_reach.size(), 5U);
    EXPECT_EQ(scratch.bucket_reach.size(), 6U);
    EXPECT_EQ(scratch.row_values.size(), 7U);
    EXPECT_EQ(scratch.row_weights.size(), 7U);
    EXPECT_EQ(scratch.local_bucket_mass.size(), 8U);
    EXPECT_TRUE(scratch.dirty_nodes.empty());
    EXPECT_TRUE(scratch.dirty_buckets.empty());
    EXPECT_EQ(scratch.row_values[0], 0.0);
    EXPECT_EQ(scratch.row_weights[1], 0.0);
    EXPECT_EQ(scratch.local_bucket_mass[1], 0.0);
}
