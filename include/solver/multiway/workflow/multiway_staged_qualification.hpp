#pragma once

/** @file
 * Incremental, memory-gated training qualification stage runner.
 */

#include "solver/multiway/engine/multiway_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	/** Identifier for one target size in a staged qualification plan. */
	enum class MultiwayQualificationStageId : std::uint8_t
	{
		FiftyThousand = 0U,
		FiveHundredThousand = 1U,
		FiveMillion = 2U,
		FiftyMillion = 3U,
	};

	/** Target trajectories and authorization requirement for one stage. */
	struct MultiwayQualificationStageSpec
	{
		MultiwayQualificationStageId id = MultiwayQualificationStageId::FiftyThousand;
		std::uint64_t target_trajectories = 0U;
		bool requires_explicit_authorization = false;
	};

	[[nodiscard]] constexpr std::array<MultiwayQualificationStageSpec, 4U>
	/** Returns the four repository-defined qualification targets. */
	multiway_default_qualification_stages() noexcept
	{
		return { {
			{ MultiwayQualificationStageId::FiftyThousand, 50'000U, false },
			{ MultiwayQualificationStageId::FiveHundredThousand, 500'000U, false },
			{ MultiwayQualificationStageId::FiveMillion, 5'000'000U, false },
			{ MultiwayQualificationStageId::FiftyMillion, 50'000'000U, true },
		} };
	}

	/** Ordered stage list, authorization, and working-set gate settings. */
	struct MultiwayStagedQualificationPlan
	{
		std::array<MultiwayQualificationStageSpec, 4U> stages = multiway_default_qualification_stages();
		std::size_t stage_count = 4U;
		bool authorize_50m_stage = false;
		MultiwayWorkingSetGateConfig working_set_gate{};

		/** Checks stage ordering, target counts, and authorization settings. */
		void validate() const;
	};

	/** Training counters sampled before or after a qualification stage. */
	struct MultiwayQualificationProgress
	{
		std::uint64_t trajectories = 0U;
		std::uint64_t batches = 0U;
		std::uint64_t batch_trajectories = 0U;
		std::uint64_t deterministic_fingerprint = 0U;
		std::uint64_t discarded_trajectories = 0U;
	};

	/** Callback that reads current qualification progress. */
	using MultiwayQualificationProgressObserver =
		MultiwayQualificationProgress (*)(const void*) noexcept;
	/** Callback that attempts one training batch and reports success. */
	using MultiwayQualificationBatchRunner = bool (*)(void*) noexcept;

	/** Host-owned callbacks for progress, batch execution, and memory sampling. */
	struct MultiwayStagedQualificationCallbacks
	{
		void* context = nullptr;
		MultiwayQualificationProgressObserver progress = nullptr;
		MultiwayQualificationBatchRunner run_one_batch = nullptr;
		MultiwayProcessMemoryObserver observe_working_set = nullptr;
		const void* observe_working_set_context = nullptr;

		/** Checks that the required callbacks are installed. */
		void validate() const;
	};

	/** Outcome assigned to one attempted qualification stage. */
	enum class MultiwayQualificationStageStatus : std::uint8_t
	{
		NotRun = 0U,
		Completed = 1U,
		PreflightRejected = 2U,
		RuntimeRejected = 3U,
		NotAuthorized = 4U,
		Failed = 5U,
	};

	/** Stage status, progress delta, and working-set gate result. */
	struct MultiwayQualificationStageResult
	{
		MultiwayQualificationStageSpec stage{};
		MultiwayQualificationStageStatus status = MultiwayQualificationStageStatus::NotRun;
		MultiwayQualificationProgress before{};
		MultiwayQualificationProgress after{};
		MultiwayWorkingSetGateResult gate{};
	};

	/** Ordered stage results and final stop/completion state. */
	struct MultiwayStagedQualificationResult
	{
		std::array<MultiwayQualificationStageResult, 4U> stages{};
		std::size_t stage_count = 0U;
		MultiwayWorkingSetGateResult preflight_gate{};
		bool stopped = false;

		/** Returns true when all configured stages completed. */
		[[nodiscard]] bool completed() const noexcept;
	};

	/** Runs configured training stages after preflight and runtime gate checks. */
	class MultiwayStagedQualificationRunner
	{
	public:
		/** Binds plan, preflight estimate, and host callbacks. */
		MultiwayStagedQualificationRunner(
			MultiwayStagedQualificationPlan plan,
			MultiwayMemoryPreflight preflight,
			MultiwayStagedQualificationCallbacks callbacks)
			: plan_(plan), preflight_(preflight), callbacks_(callbacks) {}

		/** Executes stages until completion or a gate, callback, or auth stop. */
		[[nodiscard]] MultiwayStagedQualificationResult run();

	private:
		MultiwayStagedQualificationPlan plan_{};
		MultiwayMemoryPreflight preflight_{};
		MultiwayStagedQualificationCallbacks callbacks_{};
	};

} // namespace texas::solver::multiway
