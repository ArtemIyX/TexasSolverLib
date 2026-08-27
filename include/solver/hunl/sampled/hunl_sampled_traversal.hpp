#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/hunl/sampled/hunl_sampled_builder.hpp"
#include "solver/hunl/sampled/hunl_sampled_storage.hpp"
#include "solver/hunl/sampled/hunl_sampled_terminal.hpp"
#include "solver/hunl/sampled/hunl_sampled_trajectory.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace texas::solver::hunl {

struct HUNLSampledTraversalRequest {
    PlayerId traversing_player = 0;
    std::uint64_t seed = 1;
    std::uint64_t trajectory_id = 0;
    std::uint32_t iteration = 0;
    std::uint32_t root_node_id = 0;
    std::uint32_t bucket = 0;
    std::uint32_t bucket_count = 1;
    std::size_t delta_capacity_hint = 4096;
    std::optional<std::array<std::array<std::uint8_t, 2>, 2>> private_hole = std::nullopt;
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
        HUNLSampledWorkerScratch& scratch) const;

private:
    HUNLSampledBuilder& builder_;
    HUNLSampledStorage& storage_;
    const HUNLSampledTerminalEvaluator& terminal_evaluator_;
};

class HUNLSampledTraversalPreparationRequired final : public std::runtime_error {
public:
    explicit HUNLSampledTraversalPreparationRequired(std::uint32_t node_id)
        : std::runtime_error("sampled traversal requires coordinator preparation"), node_id_(node_id) {}
    [[nodiscard]] std::uint32_t node_id() const noexcept { return node_id_; }
private:
    std::uint32_t node_id_ = 0;
};

void prepare_hunl_sampled_trajectory(
    HUNLSampledBuilder& builder,
    HUNLSampledStorage& storage,
    const HUNLSampledTerminalEvaluator& terminal_evaluator,
    const HUNLSampledTraversalRequest& request);

}  // namespace texas::solver::hunl
