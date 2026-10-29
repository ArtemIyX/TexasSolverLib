#pragma once

/** @file
 * @brief Maps HUNL flat-graph information sets and hands to abstraction buckets.
 */

#include "games/hunl_flat_graph.hpp"
#include "util/abstraction.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Bucket IDs, hand counts and range weights for one information set. */
	struct HUNLFlatBucketEntry
	{
		InfosetId infoset_id{};						   ///< Owning information-set ID.
		Street street = Street::Preflop;			   ///< Street whose abstraction is used.
		std::vector<std::uint8_t> board;			   ///< Board associated with the information set.
		std::string canonical_board;				   ///< Canonically ordered board key.
		std::uint32_t bucket_count = 0;				   ///< Number of dense buckets represented.
		std::vector<std::uint32_t> dense_bucket_ids;   ///< Dense bucket ID by concrete hand index.
		std::vector<std::uint32_t> bucket_hand_counts; ///< Concrete-hand count by dense bucket.
		std::vector<double> bucket_weights;			   ///< Optional range mass by dense bucket.
	};

	/** @brief Abstraction tables and per-information-set dense bucket metadata. */
	class HUNLFlatBucketMap
	{
	public:
		/** @brief Builds a map for the graph using the supplied abstraction tables. */
		static HUNLFlatBucketMap from_abstraction(
			const HUNLFlatSolveGraph& graph,
			AbstractionTables tables);

		/** @brief Reports whether the map contains no information-set entries. */
		[[nodiscard]] bool empty() const noexcept;
		/** @brief Returns the abstraction tables used to build this map. */
		[[nodiscard]] const AbstractionTables& abstraction() const noexcept;
		/** @brief Returns metadata for an information set. */
		[[nodiscard]] const HUNLFlatBucketEntry& entry(InfosetId infoset_id) const;
		/** @brief Looks up the dense bucket for a concrete private hand. */
		[[nodiscard]] std::int32_t lookup_bucket(InfosetId infoset_id, const std::array<std::uint8_t, 2>& hole) const;
		/** @brief Returns the number of dense buckets for an information set. */
		[[nodiscard]] std::uint32_t bucket_count(InfosetId infoset_id) const;
		/** @brief Returns the concrete-hand to dense-bucket mapping. */
		[[nodiscard]] const std::vector<std::uint32_t>& dense_bucket_ids(InfosetId infoset_id) const;
		/** @brief Returns the number of concrete hands assigned to one dense bucket. */
		[[nodiscard]] std::uint32_t bucket_hand_count(InfosetId infoset_id, std::size_t bucket_idx) const;
		/** @brief Returns configured range weight for one dense bucket. */
		[[nodiscard]] double bucket_weight(InfosetId infoset_id, std::size_t bucket_idx) const;
		/** @brief Returns the bucket-weight vector, or null when no weights are set. */
		[[nodiscard]] const std::vector<double>* bucket_weights(InfosetId infoset_id) const;

		/** @brief Replaces weights for one information set. */
		void set_bucket_weights(InfosetId infoset_id, std::vector<double> weights);
		/** @brief Projects player hand or bucket range inputs onto graph information sets. */
		void apply_range_inputs(
			const HUNLFlatSolveGraph& graph,
			const std::array<std::optional<HUNLRangeInput>, 2>& player_ranges);

	private:
		static std::uint32_t bucket_count_for_street(const AbstractionTables& tables, Street street);
		HUNLFlatBucketEntry& entry_mut(InfosetId infoset_id);

		AbstractionTables tables_;
		std::vector<std::optional<HUNLFlatBucketEntry>> entries_;
	};

} // namespace texas::solver::hunl
