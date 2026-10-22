#pragma once

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

	enum class MultiwaySchedulerMode : std::uint8_t
	{
		Fixed = 0U,
		Dynamic = 1U,
	};

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
		bool bitwise_deterministic = true;
	};

	[[nodiscard]] std::uint64_t multiway_deterministic_trajectory_seed(
		std::uint64_t base_seed,
		std::uint64_t trajectory_id) noexcept;

	[[nodiscard]] std::uint64_t multiway_deterministic_schedule_fingerprint(
		std::uint32_t worker_count,
		std::uint64_t base_seed,
		std::uint64_t first_trajectory_id,
		std::uint64_t trajectory_count) noexcept;

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

	struct MultiwayWorkerBatch
	{
		std::size_t worker_index = 0;
		MultiwayTrajectoryRange trajectories{};
	};

	class MultiwayScheduler
	{
	public:
		[[nodiscard]] static std::size_t dynamic_chunk_count(
			std::uint64_t trajectory_count) noexcept;
		[[nodiscard]] static std::size_t dynamic_chunk_count(
			std::uint64_t trajectory_count,
			std::uint32_t chunk_size) noexcept;
		[[nodiscard]] static MultiwayTrajectoryRange dynamic_chunk_range(
			std::uint64_t trajectory_count,
			std::size_t chunk_index) noexcept;
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
		[[nodiscard]] static std::size_t partition_round_robin_into(
			std::uint64_t trajectory_count,
			std::size_t requested_workers,
			MultiwayWorkerBatch* output,
			std::size_t output_capacity) noexcept;

		[[nodiscard]] static std::vector<MultiwayWorkerBatch> partition_deterministic(
			std::uint64_t trajectory_count,
			std::size_t requested_workers);
	};

} // namespace texas::solver::multiway
