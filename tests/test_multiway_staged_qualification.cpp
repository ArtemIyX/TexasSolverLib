#include "solver/multiway/workflow/multiway_staged_qualification.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace
{

	struct FakeQualification
	{
		std::uint64_t trajectories = 0U;
		std::uint64_t batches = 0U;
		std::uint64_t batch_trajectories = 10U;
		std::uint64_t fingerprint = 0x1234U;
		std::uint64_t fail_on_batch = 0U;
		std::uint64_t observed_working_set = 1U;
		bool working_set_available = true;
		std::uint64_t runtime_sample_count = 0U;

		static texas::solver::multiway::MultiwayQualificationProgress progress(
			const void* context) noexcept
		{
			const auto& self = *static_cast<const FakeQualification*>(context);
			return { self.trajectories, self.batches, self.batch_trajectories,
				self.fingerprint, 0U };
		}

		static bool run_one_batch(void* context) noexcept
		{
			auto& self = *static_cast<FakeQualification*>(context);
			if (self.fail_on_batch != 0U && self.batches + 1U == self.fail_on_batch)
				return false;
			self.trajectories += self.batch_trajectories;
			++self.batches;
			self.fingerprint += self.trajectories;
			return true;
		}

		static texas::MultiwayProcessMemorySnapshot observe(const void* context) noexcept
		{
			const auto& self = *static_cast<const FakeQualification*>(context);
			texas::MultiwayProcessMemorySnapshot snapshot;
			snapshot.current_working_set_available = self.working_set_available;
			snapshot.peak_working_set_available = self.working_set_available;
			snapshot.current_working_set_bytes = self.observed_working_set;
			snapshot.peak_working_set_bytes = self.observed_working_set;
			return snapshot;
		}
	};

	texas::solver::multiway::MultiwayStagedQualificationPlan small_plan()
	{
		texas::solver::multiway::MultiwayStagedQualificationPlan plan;
		plan.stage_count = 3U;
		plan.stages[0] = { texas::solver::multiway::MultiwayQualificationStageId::FiftyThousand, 50U, false };
		plan.stages[1] = { texas::solver::multiway::MultiwayQualificationStageId::FiveHundredThousand, 500U, false };
		plan.stages[2] = { texas::solver::multiway::MultiwayQualificationStageId::FiveMillion, 5'000U, false };
		plan.working_set_gate = { 48U, 0U };
		return plan;
	}

	texas::MultiwayMemoryPreflight admitted_preflight(std::uint64_t working_set)
	{
		texas::MultiwayMemoryPreflight preflight;
		preflight.status = texas::MultiwayMemoryStatus::Ok;
		preflight.highest_admitted_stage = texas::MultiwayMemoryAdmissionStage::WorkerDeltas;
		preflight.estimate.estimated_working_set_peak_bytes = working_set;
		return preflight;
	}

	texas::solver::multiway::MultiwayStagedQualificationResult run(
		FakeQualification& fake,
		texas::MultiwayMemoryPreflight preflight,
		texas::solver::multiway::MultiwayStagedQualificationPlan plan)
	{
		using namespace texas::solver::multiway;
		return MultiwayStagedQualificationRunner(
			plan, preflight,
			{ &fake, &FakeQualification::progress, &FakeQualification::run_one_batch,
				&FakeQualification::observe, &fake }).run();
	}

} // namespace

TEST_CASE(multiway_staged_qualification_default_stages_are_explicit_and_ordered)
{
	const auto stages = texas::solver::multiway::multiway_default_qualification_stages();
	EXPECT_EQ(stages[0].target_trajectories, 50'000U);
	EXPECT_EQ(stages[1].target_trajectories, 500'000U);
	EXPECT_EQ(stages[2].target_trajectories, 5'000'000U);
	EXPECT_EQ(stages[3].target_trajectories, 50'000'000U);
	EXPECT_TRUE(stages[3].requires_explicit_authorization);
}

TEST_CASE(multiway_staged_qualification_progresses_without_discards)
{
	FakeQualification fake;
	const auto result = run(fake, admitted_preflight(1U), small_plan());
	EXPECT_TRUE(result.completed());
	EXPECT_EQ(result.stage_count, 3U);
	EXPECT_EQ(fake.trajectories, 5'000U);
	for (std::size_t index = 0U; index < result.stage_count; ++index)
	{
		EXPECT_EQ(result.stages[index].status,
			texas::solver::multiway::MultiwayQualificationStageStatus::Completed);
		EXPECT_EQ(result.stages[index].after.discarded_trajectories, 0U);
	}
}

TEST_CASE(multiway_staged_qualification_rejects_preflight_before_execution)
{
	FakeQualification fake;
	const auto result = run(fake, admitted_preflight(49U), small_plan());
	EXPECT_TRUE(result.stopped);
	EXPECT_EQ(result.preflight_gate.status, texas::MultiwayWorkingSetGateStatus::Rejected);
	EXPECT_EQ(result.preflight_gate.phase, texas::MultiwayWorkingSetGatePhase::Preflight);
	EXPECT_EQ(result.preflight_gate.admission_stage, texas::MultiwayMemoryAdmissionStage::WorkerDeltas);
	EXPECT_EQ(fake.batches, 0U);
}

TEST_CASE(multiway_working_set_gate_distinguishes_48_gib_and_64_gib_limits)
{
	const auto preflight = admitted_preflight(50U);
	const auto conservative = texas::evaluate_multiway_working_set_preflight(
		preflight, { 48U, 0U });
	const auto compatibility = texas::evaluate_multiway_working_set_preflight(
		preflight, { 64U, 0U });
	EXPECT_EQ(conservative.status, texas::MultiwayWorkingSetGateStatus::Rejected);
	EXPECT_EQ(compatibility.status, texas::MultiwayWorkingSetGateStatus::Admitted);
}

TEST_CASE(multiway_staged_qualification_rejects_runtime_working_set_growth)
{
	FakeQualification fake;
	fake.observed_working_set = 49U;
	const auto result = run(fake, admitted_preflight(1U), small_plan());
	EXPECT_TRUE(result.stopped);
	EXPECT_EQ(result.stages[0].status,
		texas::solver::multiway::MultiwayQualificationStageStatus::RuntimeRejected);
	EXPECT_EQ(result.stages[0].gate.phase, texas::MultiwayWorkingSetGatePhase::Runtime);
	EXPECT_EQ(std::string(result.stages[0].gate.rejected_category), "process working set");
	EXPECT_EQ(fake.trajectories, 0U);
}

TEST_CASE(multiway_staged_qualification_rejects_unavailable_runtime_working_set)
{
	FakeQualification fake;
	fake.working_set_available = false;
	const auto result = run(fake, admitted_preflight(1U), small_plan());
	EXPECT_TRUE(result.stopped);
	EXPECT_EQ(result.stages[0].status,
		texas::solver::multiway::MultiwayQualificationStageStatus::RuntimeRejected);
	EXPECT_EQ(result.stages[0].gate.status, texas::MultiwayWorkingSetGateStatus::Unavailable);
	EXPECT_EQ(std::string(result.stages[0].gate.rejected_category), "process working set telemetry");
	EXPECT_EQ(fake.batches, 0U);
}

TEST_CASE(multiway_working_set_gate_saturates_overflow)
{
	const auto preflight = admitted_preflight(std::numeric_limits<std::uint64_t>::max());
	const auto result = texas::evaluate_multiway_working_set_preflight(
		preflight, { std::numeric_limits<std::uint64_t>::max(), 1U });
	EXPECT_EQ(result.compared_working_set_bytes, std::numeric_limits<std::uint64_t>::max());
	EXPECT_EQ(result.status, texas::MultiwayWorkingSetGateStatus::Rejected);
}

TEST_CASE(multiway_staged_qualification_preserves_deterministic_and_transactional_failure)
{
	FakeQualification first;
	FakeQualification second;
	first.fail_on_batch = 2U;
	second.fail_on_batch = 2U;
	const auto first_result = run(first, admitted_preflight(1U), small_plan());
	const auto second_result = run(second, admitted_preflight(1U), small_plan());
	EXPECT_EQ(first_result.stages[0].status,
		texas::solver::multiway::MultiwayQualificationStageStatus::Failed);
	EXPECT_EQ(first.trajectories, first.batch_trajectories);
	EXPECT_EQ(first.trajectories, second.trajectories);
	EXPECT_EQ(first.fingerprint, second.fingerprint);
	EXPECT_EQ(first_result.stages[0].after.trajectories,
		second_result.stages[0].after.trajectories);
}

TEST_CASE(multiway_staged_qualification_does_not_authorize_50m_automatically)
{
	texas::solver::multiway::MultiwayStagedQualificationPlan plan;
	plan.validate();
	EXPECT_TRUE(!plan.authorize_50m_stage);
	EXPECT_TRUE(plan.stages[3].requires_explicit_authorization);
}
