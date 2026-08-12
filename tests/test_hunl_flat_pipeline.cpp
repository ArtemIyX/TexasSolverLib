#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_flat_pipeline.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>

TEST_CASE(hunl_flat_pipeline_plan_exposes_paper_aligned_stage_sequence) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        std::array<std::size_t, 2>{2, 3},
        texas::HUNLFlatValueLayout::InfosetActionHand);
    const auto parallel_plan = texas::HUNLFlatParallelPlan::build(graph, table, 3);
    const auto pipeline = texas::HUNLFlatPipelinePlan::build(graph, table, parallel_plan);

    EXPECT_EQ(pipeline.stages().size(), 7U);
    EXPECT_EQ(pipeline.stages()[0].id, texas::HUNLFlatPipelineStageId::ForwardProfile);
    EXPECT_EQ(pipeline.stages()[1].id, texas::HUNLFlatPipelineStageId::AggregateReach);
    EXPECT_EQ(pipeline.stages()[2].id, texas::HUNLFlatPipelineStageId::OpponentReach);
    EXPECT_EQ(pipeline.stages()[3].id, texas::HUNLFlatPipelineStageId::ShowdownEquity);
    EXPECT_EQ(pipeline.stages()[4].id, texas::HUNLFlatPipelineStageId::DepthLimitedEval);
    EXPECT_EQ(pipeline.stages()[5].id, texas::HUNLFlatPipelineStageId::BackwardCFV);
    EXPECT_EQ(pipeline.stages()[6].id, texas::HUNLFlatPipelineStageId::RegretUpdate);

    EXPECT_EQ(pipeline.buffers().infoset_count, static_cast<std::uint32_t>(graph.infosets.size()));
    EXPECT_EQ(pipeline.buffers().node_count, static_cast<std::uint32_t>(graph.node_meta.size()));
    EXPECT_EQ(pipeline.buffers().bucket_count, static_cast<std::uint32_t>(table.total_bucket_count()));
    EXPECT_EQ(pipeline.buffers().value_count, static_cast<std::uint32_t>(table.total_value_count()));
}

TEST_CASE(hunl_flat_pipeline_plan_covers_worker_ranges_for_stage_domains) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        std::array<std::size_t, 2>{2, 2},
        texas::HUNLFlatValueLayout::InfosetHandAction);
    const auto parallel_plan = texas::HUNLFlatParallelPlan::build(graph, table, 4);
    const auto pipeline = texas::HUNLFlatPipelinePlan::build(graph, table, parallel_plan);

    for (const auto& stage : pipeline.stages()) {
        EXPECT_EQ(stage.worker_ranges.size(), parallel_plan.workers.size());
        if (stage.domain == texas::HUNLFlatPipelineDomain::Infoset) {
            std::uint32_t cursor = 0;
            for (const auto& range : stage.worker_ranges) {
                EXPECT_EQ(range.begin, cursor);
                EXPECT_TRUE(range.begin <= range.end);
                cursor = range.end;
            }
            EXPECT_EQ(cursor, static_cast<std::uint32_t>(graph.infosets.size()));
        } else {
            std::uint32_t cursor = 0;
            for (const auto& range : stage.worker_ranges) {
                EXPECT_EQ(range.begin, cursor);
                EXPECT_TRUE(range.begin <= range.end);
                cursor = range.end;
            }
            EXPECT_EQ(cursor, static_cast<std::uint32_t>(graph.node_meta.size()));
        }
    }
}

TEST_CASE(hunl_flat_pipeline_plan_marks_depth_limited_stage_optional_from_graph_content) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph,
        std::array<std::size_t, 2>{2, 2},
        texas::HUNLFlatValueLayout::InfosetActionHand);
    const auto parallel_plan = texas::HUNLFlatParallelPlan::build(graph, table, 2);
    const auto pipeline = texas::HUNLFlatPipelinePlan::build(graph, table, parallel_plan);

    std::uint32_t depth_limited_count = 0;
    for (const auto& meta : graph.node_meta) {
        if (meta.type == texas::HUNLFlatNodeType::DepthLimited) {
            ++depth_limited_count;
        }
    }

    const auto& stage = pipeline.stage(texas::HUNLFlatPipelineStageId::DepthLimitedEval);
    EXPECT_EQ(pipeline.buffers().depth_limited_node_count, depth_limited_count);
    EXPECT_EQ(pipeline.buffers().has_depth_limited_nodes, depth_limited_count > 0U);
    EXPECT_EQ(stage.optional, depth_limited_count == 0U);
}

TEST_CASE(hunl_flat_dcfr_exposes_built_pipeline_plan) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    texas::HUNLFlatDCFR solver(
        graph,
        std::array<std::size_t, 2>{2, 2},
        texas::HUNLFlatValueLayout::InfosetActionHand,
        3);

    const auto& pipeline = solver.pipeline_plan();
    EXPECT_EQ(pipeline.stages().size(), 7U);
    EXPECT_EQ(pipeline.buffers().infoset_count, static_cast<std::uint32_t>(solver.graph().infosets.size()));
    EXPECT_EQ(pipeline.buffers().node_count, static_cast<std::uint32_t>(solver.graph().node_meta.size()));
    EXPECT_EQ(
        pipeline.stage(texas::HUNLFlatPipelineStageId::BackwardCFV).worker_depth_ranges.size(),
        solver.worker_count());
}
