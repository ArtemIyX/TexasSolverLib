#pragma once

/** @file
 * @brief Sampled HUNL solve coordinator, memory estimates and resumable sessions.
 */

#include "games/hunl_solver.hpp"
#include "solver/hunl/sampled/hunl_sampled_config.hpp"
#include "solver/hunl/sampled/hunl_sampled_export.hpp"
#include "solver/hunl/sampled/hunl_sampled_profile.hpp"
#include "solver/hunl/sampled/hunl_sampled_range.hpp"
#include "solver/hunl/sampled/hunl_sampled_storage.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace texas::solver::hunl
{

	/** @brief Structured public root and optional depth-limited evaluator for a sampled solve. */
	struct HUNLSampledSolveRequest
	{
		// Production sampled solving requires one explicit structured public
		// root. Fixed-private reference roots use a separate adapter.
		HUNLStructuredRootRequest structured_root; ///< Validated public root and private-range contract.
		// Non-owning. Required when the structured root has a non-zero depth limit.
		const HUNLLeafEvaluator* leaf_evaluator = nullptr; ///< Non-owning callback required for depth-limited roots.
	};

	/** @brief Root strategy, range-wide diagnostics and progress from a sampled solve. */
	struct HUNLSampledSolveResult
	{
		HUNLSampledRootStrategy root_strategy;											///< Acting-root strategy for the selected hero hand.
		std::optional<HUNLSampledRootStrategy> range_wide_root_strategy = std::nullopt; ///< Optional range-wide diagnostic strategy.
		HUNLSampledProfileSnapshot profile;												///< Profile captured when the result was produced.
		std::uint64_t batches_completed = 0;											///< Whole trajectory batches completed.
		bool timed_out = false;															///< Whether a supplied budget or deadline expired.
	};

	/** @brief Memory estimates for sampled graph, rows, workers and exports. */
	struct HUNLSampledMemoryEstimate
	{
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

		/** @brief Returns estimated live bytes. */
		[[nodiscard]] std::uint64_t total_bytes() const noexcept
		{
			return total_bytes_live;
		}
	};

	/** @brief Result category of sampled memory preflight. */
	enum class HUNLSampledMemoryStatus : std::uint8_t
	{
		Ok = 0,		  ///< Estimate is within warning limits.
		Warning = 1,  ///< Estimate crosses warning threshold.
		Rejected = 2, ///< Estimate crosses a hard limit or request is unsupported.
	};

	/** @brief Recorded changes made by adaptive memory fallback. */
	struct HUNLSampledAdaptiveAdjustments
	{
		/** @brief Maximum number of before/after estimates retained. */
		static constexpr std::size_t kMaxRecordedSteps = 128;

		bool reduced_minibatch = false;									///< Whether fallback lowered batch size.
		std::array<std::uint64_t, kMaxRecordedSteps> estimate_before{}; ///< Estimates before each adjustment.
		std::array<std::uint64_t, kMaxRecordedSteps> estimate_after{};	///< Estimates after each adjustment.
		std::size_t recorded_step_count = 0;							///< Number of recorded adjustments.
	};

	/** @brief Memory preflight status, estimate and effective sampled settings. */
	struct HUNLSampledMemoryPreflight
	{
		HUNLSampledMemoryStatus status = HUNLSampledMemoryStatus::Ok; ///< Admission category.
		HUNLSampledMemoryEstimate estimate{};						  ///< Estimated memory for the request.
		HUNLSampledSolverConfig effective_config{};					  ///< Configuration after permitted fallback.
		HUNLSampledAdaptiveAdjustments adjustments{};				  ///< Fallback steps taken during preflight.
		const char* message = "";									  ///< Static detail for warning or rejection.
	};

	/** @brief Coordinates validation, memory preflight and sampled traversal sessions. */
	class HUNLSampledSolver
	{
	public:
		/** @brief Creates the coordinator with validated default solver settings. */
		explicit HUNLSampledSolver(HUNLSampledSolverConfig config = {});
		HUNLSampledSolver(const HUNLSampledSolver&) = delete;
		HUNLSampledSolver& operator=(const HUNLSampledSolver&) = delete;
		HUNLSampledSolver(HUNLSampledSolver&&) = delete;
		HUNLSampledSolver& operator=(HUNLSampledSolver&&) = delete;

		// Runs deterministic whole batches until the deadline and returns the
		// latest clean root export. Non-positive budgets initialize an unsolved
		// uniform root without traversal work.
		/** @brief Runs whole batches until the relative time budget expires. */
		[[nodiscard]] HUNLSampledSolveResult solve_for(
			const HUNLSampledSolveRequest& request,
			std::chrono::milliseconds budget);
		// Starts a fresh deterministic structured-range solve.
		/** @brief Starts a fresh solve and runs the requested number of whole batches. */
		[[nodiscard]] HUNLSampledSolveResult run_batches(
			const HUNLSampledSolveRequest& request,
			std::uint32_t batches);
		// Begins one structured private-range solve retained by this coordinator.
		// Call resume_structured_batches to continue it without replaying any
		// trajectory ids. Starting another solve replaces this retained session.
		/** @brief Begins and retains a structured private-range solve session. */
		void begin_structured_session(
			HUNLStructuredRootRequest root,
			const HUNLLeafEvaluator* leaf_evaluator = nullptr);
		/** @brief Resumes the retained structured session by up to batches. */
		[[nodiscard]] HUNLSampledSolveResult resume_structured_batches(
			std::uint32_t batches,
			std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
		/** @brief Reports whether a structured session is ready to resume. */
		[[nodiscard]] bool has_structured_session() const noexcept;
		/** @brief Exports the latest acting-root strategy. */
		[[nodiscard]] HUNLSampledRootStrategy export_root_strategy() const;
		/** @brief Returns the accumulated sampled profile. */
		[[nodiscard]] const HUNLSampledProfile& profile() const noexcept;
		/** @brief Returns the current memory estimate. */
		[[nodiscard]] HUNLSampledMemoryEstimate memory_estimate() const noexcept;
		/** @brief Validates request admission and estimates memory without throwing. */
		[[nodiscard]] HUNLSampledMemoryPreflight preflight(
			const HUNLSampledSolveRequest& request) const noexcept;
		/** @brief Returns the effective sampled solver configuration. */
		[[nodiscard]] const HUNLSampledSolverConfig& config() const noexcept;
		/** @brief Returns mutable sampled row storage. */
		[[nodiscard]] HUNLSampledStorage& storage() noexcept;
		/** @brief Returns read-only sampled row storage. */
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
		HUNLSampledStorage storage_;
		HUNLSampledProfile profile_;
		HUNLSampledRootStrategy root_strategy_;
		std::unique_ptr<HUNLSampledRangeSession> structured_session_;
	};

} // namespace texas::solver::hunl
