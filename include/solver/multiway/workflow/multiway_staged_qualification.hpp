#pragma once

#include "solver/multiway/engine/multiway_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	enum class MultiwayQualificationStageId : std::uint8_t
	{
		FiftyThousand = 0U,
		FiveHundredThousand = 1U,
		FiveMillion = 2U,
		FiftyMillion = 3U,
	};

	struct MultiwayQualificationStageSpec
	{
		MultiwayQualificationStageId id = MultiwayQualificationStageId::FiftyThousand;
		std::uint64_t target_trajectories = 0U;
		bool requires_explicit_authorization = false;
	};

	[[nodiscard]] constexpr std::array<MultiwayQualificationStageSpec, 4U>
		multiway_default_qualification_stages() noexcept
	{
		return {{
			{ MultiwayQualificationStageId::FiftyThousand, 50'000U, false },
			{ MultiwayQualificationStageId::FiveHundredThousand, 500'000U, false },
			{ MultiwayQualificationStageId::FiveMillion, 5'000'000U, false },
			{ MultiwayQualificationStageId::FiftyMillion, 50'000'000U, true },
		}};
	}

	struct MultiwayStagedQualificationPlan
	{
		std::array<MultiwayQualificationStageSpec, 4U> stages = multiway_default_qualification_stages();
		std::size_t stage_count = 4U;
		bool authorize_50m_stage = false;
		MultiwayWorkingSetGateConfig working_set_gate{};

		void validate() const;
	};

	struct MultiwayQualificationProgress
	{
		std::uint64_t trajectories = 0U;
		std::uint64_t batches = 0U;
		std::uint64_t batch_trajectories = 0U;
		std::uint64_t deterministic_fingerprint = 0U;
		std::uint64_t discarded_trajectories = 0U;
	};

	using MultiwayQualificationProgressObserver =
		MultiwayQualificationProgress (*)(const void*) noexcept;
	using MultiwayQualificationBatchRunner = bool (*)(void*) noexcept;

	struct MultiwayStagedQualificationCallbacks
	{
		void* context = nullptr;
		MultiwayQualificationProgressObserver progress = nullptr;
		MultiwayQualificationBatchRunner run_one_batch = nullptr;
		MultiwayProcessMemoryObserver observe_working_set = nullptr;
		const void* observe_working_set_context = nullptr;

		void validate() const;
	};

	enum class MultiwayQualificationStageStatus : std::uint8_t
	{
		NotRun = 0U,
		Completed = 1U,
		PreflightRejected = 2U,
		RuntimeRejected = 3U,
		NotAuthorized = 4U,
		Failed = 5U,
	};

	struct MultiwayQualificationStageResult
	{
		MultiwayQualificationStageSpec stage{};
		MultiwayQualificationStageStatus status = MultiwayQualificationStageStatus::NotRun;
		MultiwayQualificationProgress before{};
		MultiwayQualificationProgress after{};
		MultiwayWorkingSetGateResult gate{};
	};

	struct MultiwayStagedQualificationResult
	{
		std::array<MultiwayQualificationStageResult, 4U> stages{};
		std::size_t stage_count = 0U;
		MultiwayWorkingSetGateResult preflight_gate{};
		bool stopped = false;

		[[nodiscard]] bool completed() const noexcept;
	};

	class MultiwayStagedQualificationRunner
	{
	public:
		MultiwayStagedQualificationRunner(
			MultiwayStagedQualificationPlan plan,
			MultiwayMemoryPreflight preflight,
			MultiwayStagedQualificationCallbacks callbacks)
			: plan_(plan), preflight_(preflight), callbacks_(callbacks) {}

		[[nodiscard]] MultiwayStagedQualificationResult run();

	private:
		MultiwayStagedQualificationPlan plan_{};
		MultiwayMemoryPreflight preflight_{};
		MultiwayStagedQualificationCallbacks callbacks_{};
	};

} // namespace texas::solver::multiway
