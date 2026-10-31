#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"

#include <chrono>
#include <cstdint>
#include <limits>

namespace texas::solver::multiway::trainer_detail
{
	class MultiwayDurationRecorder final
	{
	public:
		explicit MultiwayDurationRecorder(std::uint64_t& target) noexcept
			: target_(target), started_at_(std::chrono::steady_clock::now())
		{
		}

		~MultiwayDurationRecorder() noexcept
		{
			const auto elapsed = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started_at_)
					.count());
			if (elapsed > std::numeric_limits<std::uint64_t>::max() - target_)
				target_ = std::numeric_limits<std::uint64_t>::max();
			else
				target_ += elapsed;
		}

	private:
		std::uint64_t& target_;
		std::chrono::steady_clock::time_point started_at_;
	};

	inline MultiwayBlueprintTrainingMetadata make_metadata(
		const MultiwayBlueprintTrainingStatus& status,
		const MultiwayBlueprintIterationSchedule& schedule,
		std::uint64_t seed) noexcept
	{
		MultiwayBlueprintTrainingMetadata metadata;
		metadata.batches = status.batches;
		metadata.trajectories = status.trajectories;
		metadata.deterministic_seed = seed;
		metadata.late_window_start_batch = status.late_window_start_batch;
		metadata.schedule_hash = schedule.identity();
		metadata.pruned_negative_regrets = status.pruned_negative_regrets;
		metadata.terminal_visits = status.terminal_visits;
		metadata.leaf_visits = status.leaf_visits;
		metadata.missing_lookup_requests = status.missing_lookup_requests;
		metadata.linear_iteration_weighting = schedule.linear_iteration_weighting ? 1U : 0U;
		metadata.discounting_enabled = schedule.discount_regrets ? 1U : 0U;
		metadata.negative_regret_pruning_enabled = schedule.prune_negative_regrets ? 1U : 0U;
		return metadata;
	}
} // namespace texas::solver::multiway::trainer_detail
