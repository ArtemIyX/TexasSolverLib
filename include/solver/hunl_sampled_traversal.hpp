#pragma once

#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_terminal.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace core {

struct HUNLSampledTraversalRequest {
    PlayerId traversing_player = 0;
    std::uint64_t seed = 1;
    std::uint64_t trajectory_id = 0;
    std::uint32_t iteration = 0;
    std::uint32_t root_node_id = 0;
    std::uint32_t bucket = 0;
    std::uint32_t bucket_count = 1;
    std::size_t delta_capacity_hint = 4096;
};

struct HUNLSampledTraversalResult {
    static constexpr std::uint16_t kNoSampledEdge = std::numeric_limits<std::uint16_t>::max();

    double value = 0.0;
    std::uint64_t nodes_visited = 0;
    std::uint64_t infosets_updated = 0;
    std::uint64_t chance_nodes_sampled = 0;
    std::uint64_t opponent_nodes_sampled = 0;
    std::array<std::uint16_t, 4> sampled_edge_slots = {
        kNoSampledEdge,
        kNoSampledEdge,
        kNoSampledEdge,
        kNoSampledEdge,
    };
    std::uint8_t sampled_edge_slot_count = 0;
};

struct HUNLSampledValueDelta {
    InfosetId infoset_id{};
    std::uint32_t bucket = 0;
    std::uint8_t action = 0;
    double regret = 0.0;
    double strategy_sum = 0.0;
};

struct HUNLSampledWorkerScratch {
    std::vector<double> action_values;
    std::vector<double> strategy;
    std::vector<HUNLSampledValueDelta> deltas;

    void clear_keep_capacity() noexcept;
    void reserve_deltas(std::size_t count);
};

class HUNLSampledTraversal {
public:
    HUNLSampledTraversal(
        HUNLSampledBuilder& builder,
        HUNLSampledStorage& storage,
        const HUNLSampledTerminalEvaluator& terminal_evaluator);

    [[nodiscard]] HUNLSampledTraversalResult run(
        const HUNLSampledTraversalRequest& request,
        HUNLSampledWorkerScratch& scratch);
    // Worker-safe delta phase. The caller must prepare any mutable graph/row
    // state before dispatch and merge through the coordinator after workers
    // have completed in deterministic order.
    [[nodiscard]] HUNLSampledTraversalResult run_unmerged(
        const HUNLSampledTraversalRequest& request,
        HUNLSampledWorkerScratch& scratch);

private:
    HUNLSampledBuilder& builder_;
    HUNLSampledStorage& storage_;
    const HUNLSampledTerminalEvaluator& terminal_evaluator_;
};

void merge_hunl_sampled_worker_deltas(
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch);

}  // namespace core
