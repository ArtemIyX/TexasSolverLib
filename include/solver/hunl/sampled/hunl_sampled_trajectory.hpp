#pragma once

/** @file
 * @brief Sampled traversal result, value deltas and deterministic merge helpers.
 */

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace texas::solver::hunl
{

	class HUNLSampledStorage;

	/** @brief Value estimate and work counters produced by one sampled traversal. */
#pragma pack(push, 8)
	struct HUNLSampledTraversalResult
	{
		/** @brief Sentinel indicating no sampled edge slot was recorded. */
		static constexpr std::uint16_t kNoSampledEdge = std::numeric_limits<std::uint16_t>::max();

		double value = 0.0;
		std::uint64_t nodes_visited = 0;
		std::uint64_t infosets_updated = 0;
		std::uint64_t chance_nodes_sampled = 0;
		std::uint64_t opponent_nodes_sampled = 0;
		std::array<std::uint16_t, 4> sampled_edge_slots = {
			kNoSampledEdge,
			kNoSampledEdge,
			kNoSampledEdge,
			kNoSampledEdge,
		};
		std::uint8_t sampled_edge_slot_count = 0;
	};
#pragma pack(pop)

	/** @brief Regret and average-strategy increment for one action-bucket value. */
#pragma pack(push, 8)
	struct HUNLSampledValueDelta
	{
		core::InfosetId infoset_id{};
		std::uint32_t bucket = 0;
		std::uint8_t action = 0;
		double regret = 0.0;
		double strategy_sum = 0.0;
		std::uint64_t trajectory_id = 0;
	};
#pragma pack(pop)

	/** @brief Worker-owned action buffers and unmerged value deltas. */
#pragma pack(push, 8)
	struct HUNLSampledWorkerScratch
	{
		std::vector<double> action_values;
		std::vector<double> strategy;
		std::vector<HUNLSampledValueDelta> deltas;
		std::size_t merge_cursor = 0;

		/** @brief Clears active contents while retaining capacity. */
		void clear_keep_capacity() noexcept;
		/** @brief Reserves space for at least count value deltas. */
		void reserve_deltas(std::size_t count);
	};
#pragma pack(pop)

	/** @brief Merges one worker's deltas into sampled storage. */
	void merge_hunl_sampled_worker_deltas(
		HUNLSampledStorage& storage,
		HUNLSampledWorkerScratch& scratch);
	/** @brief Deterministically merges a collection of worker delta streams. */
	void merge_hunl_sampled_worker_streams(
		HUNLSampledStorage& storage,
		std::vector<HUNLSampledWorkerScratch>& streams);

} // namespace texas::solver::hunl
