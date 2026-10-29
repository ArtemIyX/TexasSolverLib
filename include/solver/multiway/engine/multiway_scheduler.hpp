#pragma once

/** @file
 * Deterministic trajectory seeds, scheduler metadata, and worker partitions.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	inline constexpr std::uint32_t MULTIWAY_PARTITION_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_TRAJECTORY_SEED_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_ACTION_SAMPLING_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_PUBLIC_CHANCE_ORDER_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_MERGE_ORDER_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE = 4U;
	inline constexpr std::uint32_t MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_SCHEDULER_POLICY_VERSION = 2U;

	/** Policy used to assign trajectory work to solver workers. */
	enum class MultiwaySchedulerMode : std::uint8_t
	{
		Fixed = 0U,
		StaticBlock = 1U,
		Dynamic = 2U,
	};

	/** Returns whether mode is a supported scheduler policy. */
	[[nodiscard]] constexpr bool multiway_scheduler_mode_valid(MultiwaySchedulerMode mode) noexcept
	{
		return mode == MultiwaySchedulerMode::Fixed || mode == MultiwaySchedulerMode::StaticBlock || mode == MultiwaySchedulerMode::Dynamic;
	}

	/** Returns a stable display name for a scheduler mode. */
	[[nodiscard]] const char* multiway_scheduler_mode_name(MultiwaySchedulerMode mode) noexcept;

	/** Seed, partition, scheduler, and merge versions for one trajectory batch. */
	struct MultiwayRunMetadata
	{
		std::uint32_t worker_count = 0U;
		std::uint64_t base_seed = 0U;
		std::uint64_t first_trajectory_id = 0U;
		std::uint64_t trajectory_count = 0U;
		std::uint64_t schedule_fingerprint = 0U;
		std::uint64_t merged_stream_fingerprint = 0U;
		std::uint32_t partition_version = MULTIWAY_PARTITION_VERSION;
		std::uint32_t trajectory_seed_version = MULTIWAY_TRAJECTORY_SEED_VERSION;
		std::uint32_t action_sampling_version = MULTIWAY_ACTION_SAMPLING_VERSION;
		std::uint32_t public_chance_order_version = MULTIWAY_PUBLIC_CHANCE_ORDER_VERSION;
		std::uint32_t merge_order_version = MULTIWAY_MERGE_ORDER_VERSION;
		MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		bool bitwise_deterministic = true;
	};

	/** Derives a stable random seed for a trajectory from its id and base seed. */
	[[nodiscard]] std::uint64_t multiway_deterministic_trajectory_seed(
		std::uint64_t base_seed,
		std::uint64_t trajectory_id) noexcept;

	/** Hashes the fixed worker and trajectory assignment settings. */
	[[nodiscard]] std::uint64_t multiway_deterministic_schedule_fingerprint(
		std::uint32_t worker_count,
		std::uint64_t base_seed,
		std::uint64_t first_trajectory_id,
		std::uint64_t trajectory_count) noexcept;

	/** Hashes worker, trajectory, scheduler mode, and dynamic chunk settings. */
	[[nodiscard]] std::uint64_t multiway_deterministic_schedule_fingerprint(
		std::uint32_t worker_count,
		std::uint64_t base_seed,
		std::uint64_t first_trajectory_id,
		std::uint64_t trajectory_count,
		MultiwaySchedulerMode scheduler_mode,
		std::uint32_t dynamic_chunk_size) noexcept;

	/** Strided half-open range of trajectory identifiers. */
	struct MultiwayTrajectoryRange
	{
		std::uint64_t begin = 0;
		std::uint64_t end = 0;
		std::uint64_t stride = 1U;

		[[nodiscard]] std::uint64_t size() const noexcept
		{
			return begin >= end ? 0U : (end - begin + stride - 1U) / stride;
		}
	};

	/** Assignment of a trajectory range to one worker index. */
	struct MultiwayWorkerBatch
	{
		std::size_t worker_index = 0;
		MultiwayTrajectoryRange trajectories{};
	};

	/** Builds deterministic worker ranges and dynamic chunk assignments. */
	class MultiwayScheduler
	{
	public:
		/** Returns the number of chunks using the configured default width. */
		[[nodiscard]] static std::size_t dynamic_chunk_count(
			std::uint64_t trajectory_count) noexcept;
		/** Returns the chunk count for an explicit chunk width. */
		[[nodiscard]] static std::size_t dynamic_chunk_count(
			std::uint64_t trajectory_count,
			std::uint32_t chunk_size) noexcept;
		/** Returns one chunk range using the default chunk width. */
		[[nodiscard]] static MultiwayTrajectoryRange dynamic_chunk_range(
			std::uint64_t trajectory_count,
			std::size_t chunk_index) noexcept;
		/** Returns one chunk range for an explicit chunk width. */
		[[nodiscard]] static MultiwayTrajectoryRange dynamic_chunk_range(
			std::uint64_t trajectory_count,
			std::size_t chunk_index,
			std::uint32_t chunk_size) noexcept;
		// Writes a deterministic contiguous partition into caller-owned storage.
		// Returns zero only when output is null or capacity is zero.
		[[nodiscard]] static std::size_t partition_deterministic_into(
			std::uint64_t trajectory_count,
			std::size_t requested_workers,
			MultiwayWorkerBatch* output,
			std::size_t output_capacity) noexcept;
		/** Writes static contiguous blocks to caller-owned output. */
		[[nodiscard]] static std::size_t partition_static_block_into(
			std::uint64_t trajectory_count,
			std::size_t requested_workers,
			MultiwayWorkerBatch* output,
			std::size_t output_capacity) noexcept;
		/** Writes interleaved trajectory ranges to caller-owned output. */
		[[nodiscard]] static std::size_t partition_round_robin_into(
			std::uint64_t trajectory_count,
			std::size_t requested_workers,
			MultiwayWorkerBatch* output,
			std::size_t output_capacity) noexcept;

		/** Allocates and returns deterministic contiguous worker partitions. */
		[[nodiscard]] static std::vector<MultiwayWorkerBatch> partition_deterministic(
			std::uint64_t trajectory_count,
			std::size_t requested_workers);
	};

} // namespace texas::solver::multiway
