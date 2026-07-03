#pragma once

#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_terminal.hpp"

#include <cstdint>
#include <vector>

namespace core {

struct HUNLSampledTraversalRequest {
    PlayerId traversing_player = 0;
    std::uint64_t trajectory_id = 0;
    std::uint32_t iteration = 0;
    std::uint32_t root_node_id = 0;
};

struct HUNLSampledTraversalResult {
    double value = 0.0;
    std::uint64_t nodes_visited = 0;
    std::uint64_t infosets_updated = 0;
};

struct HUNLSampledWorkerScratch {
    std::vector<double> action_values;
    std::vector<double> strategy;

    void clear_keep_capacity() noexcept;
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

private:
    HUNLSampledBuilder& builder_;
    HUNLSampledStorage& storage_;
    const HUNLSampledTerminalEvaluator& terminal_evaluator_;
};

}  // namespace core
