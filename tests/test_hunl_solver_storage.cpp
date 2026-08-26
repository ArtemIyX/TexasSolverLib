#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "solver/hunl_flat_state.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

std::shared_ptr<const texas::HUNLConfig> river_config() {
    return std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
}

TEST_CASE(hunl_solver_storage_infoset_table_allocates_expected_value_count) {
    const auto config = river_config();
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {2, 3};
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        texas::HUNLFlatValueLayout::InfosetHandAction);

    std::size_t expected_total = 0;
    for (std::size_t i = 0; i < graph.infosets.size(); ++i) {
        const auto& infoset = graph.infosets[i];
        const auto& meta = table.meta()[i];
        const auto expected_bucket_count = bucket_count_per_player[static_cast<std::size_t>(infoset.player)];
        const auto expected_value_count = expected_bucket_count * static_cast<std::size_t>(infoset.action_count);
        EXPECT_EQ(meta.bucket_count, expected_bucket_count);
        EXPECT_EQ(meta.value_count, expected_value_count);
        expected_total += expected_value_count;
    }

    EXPECT_EQ(table.total_value_count(), expected_total);
}

TEST_CASE(hunl_solver_storage_bucketed_row_indexing_is_correct_for_both_layouts) {
    const auto config = river_config();
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {2, 2};
    const auto hand_action = texas::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        texas::HUNLFlatValueLayout::InfosetHandAction);
    const auto action_hand = texas::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        texas::HUNLFlatValueLayout::InfosetActionHand);

    const auto id = graph.infosets.front().id;
    const auto& meta = hand_action.meta()[id.value];
    EXPECT_EQ(hand_action.value_index(id, 1, 0), meta.offset + meta.action_count);
    EXPECT_EQ(hand_action.value_index(id, 1, 1), meta.offset + meta.action_count + 1U);
    EXPECT_EQ(action_hand.value_index(id, 1, 0), meta.offset + 1U);
    EXPECT_EQ(action_hand.value_index(id, 0, 1), meta.offset + meta.bucket_count);
}

TEST_CASE(hunl_solver_storage_strategy_and_regret_buffers_scale_with_bucket_count_not_hand_count) {
    const auto config = river_config();
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {4, 5};
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        texas::HUNLFlatValueLayout::InfosetActionHand);

    for (const auto& meta : table.meta()) {
        EXPECT_EQ(meta.hand_count, meta.bucket_count);
        EXPECT_EQ(table.row_value_count(meta.id), meta.bucket_count * static_cast<std::size_t>(meta.action_count));
        EXPECT_TRUE(table.regret(meta.id) != nullptr);
        EXPECT_TRUE(table.strategy_sum(meta.id) != nullptr);
        EXPECT_TRUE(table.current_strategy(meta.id) != nullptr);
    }
}

TEST_CASE(hunl_solver_storage_worker_partitioning_covers_all_infosets_and_nodes) {
    const auto config = river_config();
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        {2, 3},
        texas::HUNLFlatValueLayout::InfosetHandAction);
    const auto plan = texas::HUNLFlatParallelPlan::build(graph, table, 4);

    std::uint32_t infoset_cursor = 0;
    std::uint32_t node_cursor = 0;
    for (const auto& worker : plan.workers) {
        EXPECT_EQ(worker.infoset_range.begin, infoset_cursor);
        EXPECT_EQ(worker.node_range.begin, node_cursor);
        EXPECT_TRUE(worker.infoset_range.begin <= worker.infoset_range.end);
        EXPECT_TRUE(worker.node_range.begin <= worker.node_range.end);
        infoset_cursor = worker.infoset_range.end;
        node_cursor = worker.node_range.end;
    }

    EXPECT_EQ(infoset_cursor, static_cast<std::uint32_t>(graph.infosets.size()));
    EXPECT_EQ(node_cursor, static_cast<std::uint32_t>(graph.node_meta.size()));
}

TEST_CASE(hunl_solver_storage_small_bucket_counts_keep_stage_buffers_in_bounds) {
    const auto config = river_config();
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        {1, 1},
        texas::HUNLFlatValueLayout::InfosetHandAction);
    texas::HUNLFlatWorkerScratch scratch;
    scratch.ensure_capacity(graph.node_meta.size(), graph.children.size(), table.total_bucket_count());

    EXPECT_EQ(scratch.bucket_reach.size(), table.total_bucket_count());
    EXPECT_EQ(scratch.player0_reach.size(), graph.node_meta.size());
    EXPECT_EQ(scratch.player1_reach.size(), graph.node_meta.size());
    EXPECT_EQ(scratch.chance_reach.size(), graph.node_meta.size());
    if (!table.meta().empty()) {
        const auto bucket_range = table.infoset_bucket_range(table.meta().front().id);
        EXPECT_TRUE(bucket_range.end <= scratch.bucket_reach.size());
    }
}

}  // namespace
