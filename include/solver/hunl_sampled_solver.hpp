#pragma once

#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_terminal.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace core {

struct HUNLSampledSolveRequest {
    std::uint8_t root_action_count = 0;
    std::optional<HUNLState> root_state = std::nullopt;
};

struct HUNLSampledSolveResult {
    HUNLSampledRootStrategy root_strategy;
    HUNLSampledProfileSnapshot profile;
    std::uint32_t batches_completed = 0;
    bool timed_out = false;
};

struct HUNLSampledMemoryEstimate {
    std::uint64_t graph_cache_bytes = 0;
    std::uint64_t graph_nodes = 0;
    std::uint64_t graph_edges = 0;
    std::uint64_t sparse_row_bytes = 0;
    std::uint64_t sparse_rows = 0;
    std::uint64_t sparse_values = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return graph_cache_bytes + sparse_row_bytes;
    }
};

class HUNLSampledSolver {
public:
    explicit HUNLSampledSolver(HUNLSampledSolverConfig config = {});

    [[nodiscard]] HUNLSampledSolveResult solve_for(
        const HUNLSampledSolveRequest& request,
        std::chrono::milliseconds budget);
    [[nodiscard]] HUNLSampledSolveResult run_batches(
        const HUNLSampledSolveRequest& request,
        std::uint32_t batches);
    [[nodiscard]] HUNLSampledRootStrategy export_root_strategy() const;
    [[nodiscard]] const HUNLSampledProfile& profile() const noexcept;
    [[nodiscard]] HUNLSampledMemoryEstimate memory_estimate() const noexcept;
    [[nodiscard]] const HUNLSampledSolverConfig& config() const noexcept;
    [[nodiscard]] HUNLSampledBuilder& builder() noexcept;
    [[nodiscard]] const HUNLSampledBuilder& builder() const noexcept;
    [[nodiscard]] HUNLSampledStorage& storage() noexcept;
    [[nodiscard]] const HUNLSampledStorage& storage() const noexcept;

private:
    HUNLSampledSolverConfig config_;
    HUNLSampledBuilder builder_;
    HUNLSampledStorage storage_;
    HUNLSampledTerminalEvaluator terminal_evaluator_;
    HUNLSampledProfile profile_;
    HUNLSampledRootStrategy root_strategy_;
};

}  // namespace core
