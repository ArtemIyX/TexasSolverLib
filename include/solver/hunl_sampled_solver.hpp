#pragma once

#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_terminal.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

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

class HUNLSampledSolverNotReady final : public std::logic_error {
public:
    HUNLSampledSolverNotReady();
};

struct HUNLSampledMemoryEstimate {
    std::uint64_t public_state_cache_bytes = 0;
    std::uint64_t public_states_cached = 0;
    std::uint64_t public_state_edges = 0;
    std::uint64_t infoset_row_bytes = 0;
    std::uint64_t infoset_rows_allocated = 0;
    std::uint64_t sparse_values_allocated = 0;
    std::uint64_t terminal_cache_bytes = 0;
    std::uint64_t worker_delta_bytes = 0;
    std::uint64_t export_bytes = 0;
    std::uint64_t total_bytes_live = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return total_bytes_live;
    }
};

enum class HUNLSampledMemoryStatus : std::uint8_t {
    Ok = 0,
    Warning = 1,
    Rejected = 2,
};

struct HUNLSampledAdaptiveAdjustments {
    static constexpr std::size_t kMaxRecordedSteps = 128;

    bool reduced_minibatch = false;
    bool reduced_traversals = false;
    bool disabled_average_strategy_sampling = false;
    bool reduced_bucket_hint = false;
    bool reduced_depth_limit_hint = false;
    std::array<std::uint64_t, kMaxRecordedSteps> estimate_before{};
    std::array<std::uint64_t, kMaxRecordedSteps> estimate_after{};
    std::size_t recorded_step_count = 0;
};

struct HUNLSampledMemoryPreflight {
    HUNLSampledMemoryStatus status = HUNLSampledMemoryStatus::Ok;
    HUNLSampledMemoryEstimate estimate{};
    HUNLSampledSolverConfig effective_config{};
    HUNLSampledAdaptiveAdjustments adjustments{};
    const char* message = "";
};

class HUNLSampledSolver {
public:
    explicit HUNLSampledSolver(HUNLSampledSolverConfig config = {});

    // Non-positive budgets initialize/export the unsolved uniform root only.
    // Positive solve requests throw until sampled MCCFR updates are implemented.
    [[nodiscard]] HUNLSampledSolveResult solve_for(
        const HUNLSampledSolveRequest& request,
        std::chrono::milliseconds budget);
    // A zero batch count has the same initialization-only behavior as a zero budget.
    [[nodiscard]] HUNLSampledSolveResult run_batches(
        const HUNLSampledSolveRequest& request,
        std::uint32_t batches);
    [[nodiscard]] HUNLSampledRootStrategy export_root_strategy() const;
    [[nodiscard]] const HUNLSampledProfile& profile() const noexcept;
    [[nodiscard]] HUNLSampledMemoryEstimate memory_estimate() const noexcept;
    [[nodiscard]] HUNLSampledMemoryPreflight preflight(
        const HUNLSampledSolveRequest& request) const noexcept;
    [[nodiscard]] const HUNLSampledSolverConfig& config() const noexcept;
    [[nodiscard]] HUNLSampledBuilder& builder() noexcept;
    [[nodiscard]] const HUNLSampledBuilder& builder() const noexcept;
    [[nodiscard]] HUNLSampledStorage& storage() noexcept;
    [[nodiscard]] const HUNLSampledStorage& storage() const noexcept;

private:
    [[nodiscard]] HUNLSampledMemoryEstimate estimate_memory_for(
        const HUNLSampledSolveRequest& request,
        const HUNLSampledSolverConfig& config) const noexcept;
    [[nodiscard]] HUNLSampledMemoryPreflight build_preflight_result(
        const HUNLSampledSolveRequest& request,
        const HUNLSampledSolverConfig& config) const noexcept;
    static void apply_adaptive_fallback(
        HUNLSampledSolverConfig& config,
        HUNLSampledAdaptiveAdjustments& adjustments) noexcept;
    [[nodiscard]] static std::uint64_t estimate_worker_delta_bytes(
        const HUNLSampledSolveRequest& request,
        const HUNLSampledSolverConfig& config) noexcept;
    [[nodiscard]] static std::uint64_t estimate_export_bytes(
        const HUNLSampledSolveRequest& request) noexcept;
    [[nodiscard]] static std::uint64_t estimate_terminal_cache_bytes(
        const HUNLSampledSolveRequest& request,
        const HUNLSampledSolverConfig& config) noexcept;

    HUNLSampledSolverConfig config_;
    HUNLSampledBuilder builder_;
    HUNLSampledStorage storage_;
    HUNLSampledTerminalEvaluator terminal_evaluator_;
    HUNLSampledProfile profile_;
    HUNLSampledRootStrategy root_strategy_;
};

}  // namespace core
