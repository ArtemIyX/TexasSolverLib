#include "solver/multiway/engine/multiway_scheduler.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "test_harness.hpp"

#include <array>

TEST_CASE(multiway_scheduler_partitions_trajectories_in_fixed_order)
{
	const auto batches = texas::MultiwayScheduler::partition_deterministic(10U, 3U);
	EXPECT_EQ(batches.size(), 3U);
	EXPECT_EQ(batches[0].trajectories.begin, 0U);
	EXPECT_EQ(batches[0].trajectories.end, 4U);
	EXPECT_EQ(batches[1].trajectories.begin, 4U);
	EXPECT_EQ(batches[1].trajectories.end, 7U);
	EXPECT_EQ(batches[2].trajectories.begin, 7U);
	EXPECT_EQ(batches[2].trajectories.end, 10U);
}

TEST_CASE(multiway_scheduler_handles_empty_and_oversized_worker_requests)
{
	const auto empty = texas::MultiwayScheduler::partition_deterministic(0U, 0U);
	EXPECT_EQ(empty.size(), 1U);
	EXPECT_EQ(empty[0].trajectories.size(), 0U);

	const auto clamped = texas::MultiwayScheduler::partition_deterministic(2U, 100U);
	EXPECT_EQ(clamped.size(), 2U);
	EXPECT_EQ(clamped[0].trajectories.size(), 1U);
	EXPECT_EQ(clamped[1].trajectories.size(), 1U);
}

TEST_CASE(multiway_scheduler_writes_reusable_fixed_partition_storage)
{
	texas::MultiwayWorkerBatch batches[4] = {};
	const auto first_count = texas::MultiwayScheduler::partition_deterministic_into(
		17U, 4U, batches, 4U);
	EXPECT_EQ(first_count, 4U);
	EXPECT_EQ(batches[0].trajectories.begin, 0U);
	EXPECT_EQ(batches[0].trajectories.end, 5U);
	EXPECT_EQ(batches[3].trajectories.begin, 13U);
	EXPECT_EQ(batches[3].trajectories.end, 17U);

	const auto second_count = texas::MultiwayScheduler::partition_deterministic_into(
		2U, 4U, batches, 4U);
	EXPECT_EQ(second_count, 2U);
	EXPECT_EQ(batches[0].trajectories.size(), 1U);
	EXPECT_EQ(batches[1].trajectories.size(), 1U);
	EXPECT_EQ(texas::MultiwayScheduler::partition_deterministic_into(1U, 1U, nullptr, 0U), 0U);
}

TEST_CASE(multiway_scheduler_round_robin_partitions_cover_each_trajectory_once)
{
	texas::MultiwayWorkerBatch batches[4] = {};
	const auto count = texas::MultiwayScheduler::partition_round_robin_into(
		10U, 3U, batches, 4U);
	EXPECT_EQ(count, 3U);
	EXPECT_EQ(batches[0].trajectories.begin, 0U);
	EXPECT_EQ(batches[0].trajectories.end, 10U);
	EXPECT_EQ(batches[0].trajectories.stride, 3U);
	EXPECT_EQ(batches[0].trajectories.size(), 4U);
	EXPECT_EQ(batches[1].trajectories.begin, 1U);
	EXPECT_EQ(batches[1].trajectories.size(), 3U);
	EXPECT_EQ(batches[2].trajectories.begin, 2U);
	EXPECT_EQ(batches[2].trajectories.size(), 3U);
	EXPECT_EQ(texas::MultiwayScheduler::partition_round_robin_into(
				  1U, 2U, nullptr, 2U),
		0U);
}

TEST_CASE(multiway_scheduler_dynamic_chunks_cover_each_trajectory_once)
{
	constexpr std::uint64_t trajectory_count = 19U;
	const auto chunk_count = texas::MultiwayScheduler::dynamic_chunk_count(trajectory_count);
	EXPECT_EQ(chunk_count, std::size_t{ 5 });
	std::array<bool, trajectory_count> seen{};
	for (std::size_t chunk = 0U; chunk < chunk_count; ++chunk)
	{
		const auto range = texas::MultiwayScheduler::dynamic_chunk_range(trajectory_count, chunk);
		EXPECT_TRUE(range.size() > 0U);
		EXPECT_TRUE(range.size() <= texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE);
		for (auto trajectory = range.begin; trajectory < range.end; ++trajectory)
		{
			EXPECT_TRUE(!seen[trajectory]);
			seen[trajectory] = true;
		}
	}
	for (const auto value : seen)
		EXPECT_TRUE(value);
}

TEST_CASE(multiway_scheduler_dynamic_chunk_size_is_configurable)
{
	constexpr std::uint64_t trajectory_count = 19U;
	for (const std::uint32_t chunk_size : { 1U, 2U, 4U, 8U, 16U })
	{
		const auto chunk_count = texas::MultiwayScheduler::dynamic_chunk_count(trajectory_count, chunk_size);
		EXPECT_EQ(chunk_count, static_cast<std::size_t>((trajectory_count + chunk_size - 1U) / chunk_size));
		std::array<bool, trajectory_count> seen{};
		for (std::size_t chunk = 0U; chunk < chunk_count; ++chunk)
		{
			const auto range = texas::MultiwayScheduler::dynamic_chunk_range(trajectory_count, chunk, chunk_size);
			EXPECT_TRUE(range.size() > 0U);
			EXPECT_TRUE(range.size() <= chunk_size);
			for (auto trajectory = range.begin; trajectory < range.end; ++trajectory)
			{
				EXPECT_TRUE(!seen[trajectory]);
				seen[trajectory] = true;
			}
		}
		for (const auto value : seen)
			EXPECT_TRUE(value);
	}
	EXPECT_EQ(texas::MultiwayScheduler::dynamic_chunk_count(trajectory_count, 0U), std::size_t{ 0 });
	EXPECT_EQ(texas::MultiwayScheduler::dynamic_chunk_range(trajectory_count, 0U, 0U).size(), std::uint64_t{ 0 });
}

TEST_CASE(multiway_scheduler_fixes_trajectory_seeds_and_versioned_run_identity)
{
	const auto first_seed = texas::multiway_deterministic_trajectory_seed(0x1234U, 17U);
	EXPECT_EQ(first_seed, texas::multiway_deterministic_trajectory_seed(0x1234U, 17U));
	EXPECT_TRUE(first_seed != texas::multiway_deterministic_trajectory_seed(0x1234U, 18U));

	const auto first = texas::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U);
	const auto repeat = texas::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U);
	EXPECT_EQ(first, repeat);
	EXPECT_TRUE(first != texas::multiway_deterministic_schedule_fingerprint(2U, 7U, 9U, 32U));
	EXPECT_TRUE(first != texas::multiway_deterministic_schedule_fingerprint(4U, 8U, 9U, 32U));
	std::array<std::uint64_t, 5> dynamic_fingerprints{};
	for (std::size_t index = 0U; index < dynamic_fingerprints.size(); ++index)
	{
		const auto chunk_size = static_cast<std::uint32_t>(1U << index);
		dynamic_fingerprints[index] = texas::multiway_deterministic_schedule_fingerprint(
			4U, 7U, 9U, 32U, texas::MultiwaySchedulerMode::Dynamic, chunk_size);
		EXPECT_TRUE(dynamic_fingerprints[index] != 0U);
		for (std::size_t previous = 0U; previous < index; ++previous)
			EXPECT_TRUE(dynamic_fingerprints[index] != dynamic_fingerprints[previous]);
	}
	EXPECT_TRUE(dynamic_fingerprints[0U] != texas::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U, texas::MultiwaySchedulerMode::Fixed, 4U));
}

TEST_CASE(multiway_scheduler_reports_deterministic_run_metadata)
{
	texas::MultiwaySolverLimits limits;
	limits.worker_count = 1U;
	limits.trajectories_per_batch = 1U;
	limits.max_public_states = 1U;
	limits.max_sparse_rows = 1U;
	limits.max_sparse_values = 1U;
	limits.max_worker_delta_entries = 1U;
	limits.validate();
	EXPECT_TRUE(texas::MultiwayRunMetadata{}.bitwise_deterministic);
}
