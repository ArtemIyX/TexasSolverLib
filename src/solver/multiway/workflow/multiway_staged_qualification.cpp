#include "solver/multiway/workflow/multiway_staged_qualification.hpp"

#include <stdexcept>

namespace texas::solver::multiway
{

	void MultiwayStagedQualificationPlan::validate() const
	{
		working_set_gate.validate();
		if (stage_count == 0U || stage_count > stages.size())
			throw std::invalid_argument("staged qualification requires at least one stage");
		for (std::size_t index = 0U; index < stage_count; ++index)
		{
			const auto& stage = stages[index];
			if (stage.target_trajectories == 0U || (index != 0U && stage.target_trajectories <= stages[index - 1U].target_trajectories))
				throw std::invalid_argument("staged qualification targets must increase");
			if (stage.id == MultiwayQualificationStageId::FiftyMillion && !stage.requires_explicit_authorization)
				throw std::invalid_argument("the 50M qualification stage requires explicit authorization");
		}
	}

	void MultiwayStagedQualificationCallbacks::validate() const
	{
		if (progress == nullptr || run_one_batch == nullptr)
			throw std::invalid_argument("staged qualification callbacks are incomplete");
		if (observe_working_set == nullptr && observe_working_set_context != nullptr)
			throw std::invalid_argument("working-set observer context requires an observer");
	}

	bool MultiwayStagedQualificationResult::completed() const noexcept
	{
		if (stopped || stage_count == 0U)
			return false;
		for (std::size_t index = 0U; index < stage_count; ++index)
		{
			if (stages[index].status != MultiwayQualificationStageStatus::Completed)
				return false;
		}
		return true;
	}

	MultiwayStagedQualificationResult MultiwayStagedQualificationRunner::run()
	{
		plan_.validate();
		callbacks_.validate();
		MultiwayStagedQualificationResult result;
		result.preflight_gate = evaluate_multiway_working_set_preflight(
			preflight_, plan_.working_set_gate);
		if (result.preflight_gate.rejected())
		{
			result.stopped = true;
			return result;
		}

		result.stage_count = plan_.stage_count;
		for (std::size_t index = 0U; index < plan_.stage_count; ++index)
		{
			auto& stage_result = result.stages[index];
			stage_result.stage = plan_.stages[index];
			stage_result.before = callbacks_.progress(callbacks_.context);
			stage_result.gate = result.preflight_gate;
			if (stage_result.stage.requires_explicit_authorization && !plan_.authorize_50m_stage)
			{
				stage_result.status = MultiwayQualificationStageStatus::NotAuthorized;
				stage_result.after = stage_result.before;
				result.stopped = true;
				break;
			}
			if (stage_result.before.trajectories > stage_result.stage.target_trajectories)
			{
				stage_result.status = MultiwayQualificationStageStatus::Failed;
				stage_result.after = stage_result.before;
				result.stopped = true;
				break;
			}
			if (stage_result.before.trajectories == stage_result.stage.target_trajectories)
			{
				stage_result.status = MultiwayQualificationStageStatus::Completed;
				stage_result.after = stage_result.before;
				continue;
			}
			if (stage_result.before.batch_trajectories == 0U || (stage_result.stage.target_trajectories - stage_result.before.trajectories) % stage_result.before.batch_trajectories != 0U)
			{
				stage_result.status = MultiwayQualificationStageStatus::Failed;
				stage_result.after = stage_result.before;
				result.stopped = true;
				break;
			}
			while (true)
			{
				const auto progress_before_batch = callbacks_.progress(callbacks_.context);
				if (progress_before_batch.trajectories == stage_result.stage.target_trajectories)
					break;
				const auto pre_batch_snapshot = callbacks_.observe_working_set == nullptr
					? capture_multiway_process_memory_snapshot()
					: callbacks_.observe_working_set(callbacks_.observe_working_set_context);
				stage_result.gate = evaluate_multiway_working_set_runtime(
					pre_batch_snapshot, plan_.working_set_gate);
				if (stage_result.gate.blocked())
				{
					stage_result.status = MultiwayQualificationStageStatus::RuntimeRejected;
					stage_result.after = progress_before_batch;
					result.stopped = true;
					break;
				}
				if (!callbacks_.run_one_batch(callbacks_.context))
				{
					stage_result.status = MultiwayQualificationStageStatus::Failed;
					stage_result.after = callbacks_.progress(callbacks_.context);
					result.stopped = true;
					break;
				}
				const auto progress_after_batch = callbacks_.progress(callbacks_.context);
				const auto snapshot = callbacks_.observe_working_set == nullptr
					? capture_multiway_process_memory_snapshot()
					: callbacks_.observe_working_set(callbacks_.observe_working_set_context);
				stage_result.gate = evaluate_multiway_working_set_runtime(
					snapshot, plan_.working_set_gate);
				if (stage_result.gate.blocked())
				{
					stage_result.status = MultiwayQualificationStageStatus::RuntimeRejected;
					stage_result.after = progress_after_batch;
					result.stopped = true;
					break;
				}
				if (progress_after_batch.trajectories < progress_before_batch.trajectories || progress_after_batch.trajectories > stage_result.stage.target_trajectories || progress_after_batch.discarded_trajectories != 0U)
				{
					stage_result.status = MultiwayQualificationStageStatus::Failed;
					stage_result.after = progress_after_batch;
					result.stopped = true;
					break;
				}
			}
			if (result.stopped)
				break;
			stage_result.after = callbacks_.progress(callbacks_.context);
			if (stage_result.after.trajectories != stage_result.stage.target_trajectories || stage_result.after.discarded_trajectories != 0U)
			{
				stage_result.status = MultiwayQualificationStageStatus::Failed;
				result.stopped = true;
				break;
			}
			stage_result.status = MultiwayQualificationStageStatus::Completed;
		}
		return result;
	}

} // namespace texas::solver::multiway
