#include "solver/hunl_flat_pipeline.hpp"

#include <stdexcept>

namespace core {

namespace {

std::uint32_t count_nodes_of_type(
    const HUNLFlatSolveGraph& graph,
    HUNLFlatNodeType type) {
    std::uint32_t count = 0;
    for (const auto& meta : graph.node_meta) {
        if (meta.type == type) {
            ++count;
        }
    }
    return count;
}

HUNLFlatPipelineStagePlan make_infoset_stage(
    HUNLFlatPipelineStageId id,
    const HUNLFlatParallelPlan& parallel_plan) {
    HUNLFlatPipelineStagePlan stage;
    stage.id = id;
    stage.domain = HUNLFlatPipelineDomain::Infoset;
    stage.worker_ranges.reserve(parallel_plan.workers.size());
    for (const auto& worker : parallel_plan.workers) {
        stage.worker_ranges.push_back(worker.infoset_range);
    }
    return stage;
}

HUNLFlatPipelineStagePlan make_node_stage(
    HUNLFlatPipelineStageId id,
    const HUNLFlatParallelPlan& parallel_plan,
    bool optional = false) {
    HUNLFlatPipelineStagePlan stage;
    stage.id = id;
    stage.domain = HUNLFlatPipelineDomain::Node;
    stage.optional = optional;
    stage.worker_ranges.reserve(parallel_plan.workers.size());
    for (const auto& worker : parallel_plan.workers) {
        stage.worker_ranges.push_back(worker.node_range);
    }
    return stage;
}

HUNLFlatPipelineStagePlan make_depth_node_stage(
    HUNLFlatPipelineStageId id,
    const HUNLFlatParallelPlan& parallel_plan) {
    HUNLFlatPipelineStagePlan stage;
    stage.id = id;
    stage.domain = HUNLFlatPipelineDomain::DepthNode;
    stage.worker_ranges.reserve(parallel_plan.workers.size());
    stage.worker_depth_ranges.reserve(parallel_plan.workers.size());
    stage.worker_costs.reserve(parallel_plan.workers.size());
    for (const auto& worker : parallel_plan.workers) {
        stage.worker_ranges.push_back(worker.node_range);
        stage.worker_depth_ranges.push_back(worker.depth_node_ranges);
        std::uint64_t total_cost = 0;
        for (const auto cost : worker.depth_backward_costs) {
            total_cost += cost;
        }
        stage.worker_costs.push_back(total_cost);
    }
    return stage;
}

}  // namespace

HUNLFlatPipelinePlan HUNLFlatPipelinePlan::build(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table,
    const HUNLFlatParallelPlan& parallel_plan) {
    HUNLFlatPipelinePlan plan;
    plan.buffers_.infoset_count = static_cast<std::uint32_t>(graph.infosets.size());
    plan.buffers_.node_count = static_cast<std::uint32_t>(graph.nodes.size());
    plan.buffers_.bucket_count = static_cast<std::uint32_t>(infoset_table.total_bucket_count());
    plan.buffers_.value_count = static_cast<std::uint32_t>(infoset_table.total_value_count());
    plan.buffers_.terminal_node_count =
        count_nodes_of_type(graph, HUNLFlatNodeType::TerminalFold) +
        count_nodes_of_type(graph, HUNLFlatNodeType::TerminalShowdown);
    plan.buffers_.showdown_node_count = count_nodes_of_type(graph, HUNLFlatNodeType::TerminalShowdown);
    plan.buffers_.depth_limited_node_count = count_nodes_of_type(graph, HUNLFlatNodeType::DepthLimited);
    plan.buffers_.has_depth_limited_nodes = plan.buffers_.depth_limited_node_count > 0;

    plan.stages_.reserve(7);
    plan.stages_.push_back(make_infoset_stage(HUNLFlatPipelineStageId::ForwardProfile, parallel_plan));
    plan.stages_.push_back(make_depth_node_stage(HUNLFlatPipelineStageId::AggregateReach, parallel_plan));
    plan.stages_.push_back(make_node_stage(HUNLFlatPipelineStageId::OpponentReach, parallel_plan));
    plan.stages_.push_back(make_node_stage(HUNLFlatPipelineStageId::ShowdownEquity, parallel_plan));
    plan.stages_.push_back(make_node_stage(
        HUNLFlatPipelineStageId::DepthLimitedEval,
        parallel_plan,
        !plan.buffers_.has_depth_limited_nodes));
    plan.stages_.push_back(make_depth_node_stage(HUNLFlatPipelineStageId::BackwardCFV, parallel_plan));
    plan.stages_.push_back(make_infoset_stage(HUNLFlatPipelineStageId::RegretUpdate, parallel_plan));
    return plan;
}

const HUNLFlatPipelineBuffers& HUNLFlatPipelinePlan::buffers() const noexcept {
    return buffers_;
}

const std::vector<HUNLFlatPipelineStagePlan>& HUNLFlatPipelinePlan::stages() const noexcept {
    return stages_;
}

const HUNLFlatPipelineStagePlan& HUNLFlatPipelinePlan::stage(HUNLFlatPipelineStageId id) const {
    for (const auto& stage : stages_) {
        if (stage.id == id) {
            return stage;
        }
    }
    throw std::out_of_range("HUNLFlatPipelinePlan stage id not found");
}

}  // namespace core
