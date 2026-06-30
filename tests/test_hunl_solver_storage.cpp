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

std::shared_ptr<const core::HUNLConfig> river_config() {
    return std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
}

TEST_CASE(hunl_solver_storage_infoset_table_allocates_expected_value_count) {
    const auto config = river_config();
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {2, 3};
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        core::HUNLFlatValueLayout::InfosetHandAction);

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
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {2, 2};
    const auto hand_action = core::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        core::HUNLFlatValueLayout::InfosetHandAction);
    const auto action_hand = core::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        core::HUNLFlatValueLayout::InfosetActionHand);

    const auto id = graph.infosets.front().id;
    const auto& meta = hand_action.meta()[id.value];
    EXPECT_EQ(hand_action.value_index(id, 1, 0), meta.offset + meta.action_count);
    EXPECT_EQ(hand_action.value_index(id, 1, 1), meta.offset + meta.action_count + 1U);
    EXPECT_EQ(action_hand.value_index(id, 1, 0), meta.offset + 1U);
    EXPECT_EQ(action_hand.value_index(id, 0, 1), meta.offset + meta.bucket_count);
}

TEST_CASE(hunl_solver_storage_strategy_and_regret_buffers_scale_with_bucket_count_not_hand_count) {
    const auto config = river_config();
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> bucket_count_per_player = {4, 5};
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        bucket_count_per_player,
        core::HUNLFlatValueLayout::InfosetActionHand);

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
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetHandAction);
    const auto plan = core::HUNLFlatParallelPlan::build(graph, table, 4);

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
    EXPECT_EQ(node_cursor, static_cast<std::uint32_t>(graph.nodes.size()));
}

TEST_CASE(hunl_solver_storage_small_bucket_counts_keep_stage_buffers_in_bounds) {
    const auto config = river_config();
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        {1, 1},
        core::HUNLFlatValueLayout::InfosetHandAction);
    core::HUNLFlatWorkerScratch scratch;
    scratch.ensure_capacity(graph.nodes.size(), graph.children.size(), table.total_bucket_count());

    EXPECT_EQ(scratch.bucket_reach.size(), table.total_bucket_count());
    EXPECT_EQ(scratch.action_values.size(), graph.children.size());
    EXPECT_EQ(scratch.node_values.size(), graph.nodes.size());
    if (!table.meta().empty()) {
        const auto bucket_range = table.infoset_bucket_range(table.meta().front().id);
        EXPECT_TRUE(bucket_range.end <= scratch.bucket_reach.size());
    }
}

TEST_CASE(hunl_solver_storage_infoset_table_uses_bucket_map_bucket_counts) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_solver_storage_bucket_map_counts.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 3U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 3}, core::ABSTRACTION_SCHEMA_VERSION, "river-3", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        {99, 99},
        &map,
        core::HUNLFlatValueLayout::InfosetHandAction);

    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        const auto& meta = table.meta()[infoset.id.value];
        EXPECT_EQ(meta.bucket_count, 3U);
        EXPECT_EQ(meta.value_count, 3U * static_cast<std::uint32_t>(infoset.action_count));
    }

    std::filesystem::remove(path);
}

}  // namespace
