#pragma once

#include "solver/hunl_flat_state.hpp"

#include <cstdint>
#include <vector>

namespace core {

class HUNLFlatDCFR;

enum class HUNLFlatPipelineStageId : std::uint8_t {
    ForwardProfile = 0,
    AggregateReach = 1,
    OpponentReach = 2,
    ShowdownEquity = 3,
    DepthLimitedEval = 4,
    BackwardCFV = 5,
    RegretUpdate = 6,
};

enum class HUNLFlatPipelineDomain : std::uint8_t {
    Infoset = 0,
    Node = 1,
    DepthNode = 2,
};

struct HUNLFlatPipelineBuffers {
    std::uint32_t infoset_count = 0;
    std::uint32_t node_count = 0;
    std::uint32_t bucket_count = 0;
    std::uint32_t value_count = 0;
    std::uint32_t terminal_node_count = 0;
    std::uint32_t showdown_node_count = 0;
    std::uint32_t depth_limited_node_count = 0;
    bool has_depth_limited_nodes = false;
};

struct HUNLFlatPipelineStagePlan {
    HUNLFlatPipelineStageId id = HUNLFlatPipelineStageId::ForwardProfile;
    HUNLFlatPipelineDomain domain = HUNLFlatPipelineDomain::Infoset;
    bool optional = false;
    std::vector<HUNLFlatRange> worker_ranges;
    std::vector<std::vector<HUNLFlatRange>> worker_depth_ranges;
    std::vector<std::uint64_t> worker_costs;
};

class HUNLFlatPipelinePlan {
public:
    static HUNLFlatPipelinePlan build(
        const HUNLFlatSolveGraph& graph,
        const HUNLFlatInfosetTable& infoset_table,
        const HUNLFlatParallelPlan& parallel_plan);

    [[nodiscard]] const HUNLFlatPipelineBuffers& buffers() const noexcept;
    [[nodiscard]] const std::vector<HUNLFlatPipelineStagePlan>& stages() const noexcept;
    [[nodiscard]] const HUNLFlatPipelineStagePlan& stage(HUNLFlatPipelineStageId id) const;

private:
    HUNLFlatPipelineBuffers buffers_;
    std::vector<HUNLFlatPipelineStagePlan> stages_;
};

class HUNLFlatPipeline {
public:
    explicit HUNLFlatPipeline(HUNLFlatPipelinePlan plan);

    [[nodiscard]] const HUNLFlatPipelinePlan& plan() const noexcept;
    void run_iteration(HUNLFlatDCFR& solver) const;

private:
    HUNLFlatPipelinePlan plan_;
};

}  // namespace core
