#pragma once

/** @file
 * @brief Deterministically partitions sampled trajectories among workers.
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Half-open interval of deterministic trajectory IDs. */
#pragma pack(push, 8)
	struct HUNLSampledTrajectoryRange
	{
		std::uint64_t begin = 0; ///< First trajectory ID.
		std::uint64_t end = 0;	 ///< One past the final trajectory ID.

		/** @brief Returns the number of trajectory IDs in the range. */
		[[nodiscard]] std::uint64_t size() const noexcept
		{
			return end - begin;
		}
	};
#pragma pack(pop)

	/** @brief One worker's assigned trajectory interval. */
#pragma pack(push, 8)
	struct HUNLSampledWorkerBatch
	{
		std::size_t worker_index = 0;			 ///< Worker receiving the batch.
		HUNLSampledTrajectoryRange trajectories; ///< Deterministic half-open interval.
	};
#pragma pack(pop)

	static_assert(
		std::is_trivially_copyable_v<HUNLSampledTrajectoryRange>,
		"HUNLSampledTrajectoryRange should stay trivially copyable");
	static_assert(
		std::is_trivially_copyable_v<HUNLSampledWorkerBatch>,
		"HUNLSampledWorkerBatch should stay trivially copyable");

	/** @brief Stateless deterministic worker-batch partitioner. */
	class HUNLSampledScheduler
	{
	public:
		/** @brief Partitions [0, trajectory_count) across worker_count workers. */
		[[nodiscard]] static std::vector<HUNLSampledWorkerBatch> partition_deterministic(
			std::uint64_t trajectory_count,
			std::size_t worker_count);
	};

} // namespace texas::solver::hunl
