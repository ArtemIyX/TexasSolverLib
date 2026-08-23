#pragma once

#include "core/namespaces.hpp"

#include "games/hunl_solver.hpp"
#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_range.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_terminal.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace texas::solver::hunl {

struct HUNLSampledSolveRequest {
    std::uint8_t root_action_count = 0;
    std::optional<HUNLState> root_state = std::nullopt;
    std::optional<HUNLStructuredRootRequest> structured_root = std::nullopt;
    // Non-owning. Required when the selected root has a non-zero depth limit.
    const HUNLLeafEvaluator* leaf_evaluator = nullptr;
};

struct HUNLSampledSolveResult {
    HUNLSampledRootStrategy root_strategy;
    std::optional<HUNLSampledRootStrategy> range_wide_root_strategy = std::nullopt;
    HUNLSampledProfileSnapshot profile;
    std::uint64_t batches_completed = 0;
    bool timed_out = false;
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
    std::uint64_t structured_joint_deal_bytes = 0;
    std::uint64_t structured_infoset_lookup_bytes = 0;
    std::uint64_t structured_session_bytes = 0;
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
    HUNLSampledSolver(const HUNLSampledSolver&) = delete;
    HUNLSampledSolver& operator=(const HUNLSampledSolver&) = delete;
    HUNLSampledSolver(HUNLSampledSolver&&) = delete;
    HUNLSampledSolver& operator=(HUNLSampledSolver&&) = delete;

    // Runs deterministic whole batches until the deadline and returns the
    // latest clean root export. Non-positive budgets initialize an unsolved
    // uniform root without traversal work.
    [[nodiscard]] HUNLSampledSolveResult solve_for(
        const HUNLSampledSolveRequest& request,
        std::chrono::milliseconds budget);
    // Starts a fresh deterministic solve. Structured roots use the private
    // range traversal; explicit roots remain the fixed-hand oracle path.
    [[nodiscard]] HUNLSampledSolveResult run_batches(
        const HUNLSampledSolveRequest& request,
        std::uint32_t batches);
    // Begins one structured private-range solve retained by this coordinator.
    // Call resume_structured_batches to continue it without replaying any
    // trajectory ids. Starting another solve replaces this retained session.
    void begin_structured_session(
        HUNLStructuredRootRequest root,
        const HUNLLeafEvaluator* leaf_evaluator = nullptr);
    [[nodiscard]] HUNLSampledSolveResult resume_structured_batches(
        std::uint32_t batches,
        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
    [[nodiscard]] bool has_structured_session() const noexcept;
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
    [[nodiscard]] HUNLSampledSolveResult run_batches_impl(
        const HUNLSampledSolveRequest& request,
        std::uint32_t batches,
        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
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
    std::unique_ptr<HUNLSampledRangeSession> structured_session_;
};

}  // namespace texas::solver::hunl
