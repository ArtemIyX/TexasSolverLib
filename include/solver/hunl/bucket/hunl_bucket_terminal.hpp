#pragma once

/** @file
 * @brief Precomputes bucket-pair showdown matrices for HUNL terminal evaluation.
 */

#include "games/hunl_eval.hpp"
#include "solver/hunl/bucket/hunl_bucket_map.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Pairwise win/tie counts between two players' abstraction buckets. */
	struct HUNLBucketShowdownMatrix
	{
		std::uint32_t bucket_count_p0 = 0; ///< Number of player-zero buckets.
		std::uint32_t bucket_count_p1 = 0; ///< Number of player-one buckets.
		std::vector<std::uint32_t> valid_pair_counts; ///< Compatible pair count indexed by bucket pair.
		std::vector<std::int32_t> net_win_counts; ///< Wins minus losses indexed by bucket pair.
		std::vector<std::uint32_t> tie_pair_counts; ///< Tied compatible-pair count indexed by bucket pair.
		std::vector<std::uint32_t> bucket_hand_counts_p0; ///< Concrete hands per player-zero bucket.
		std::vector<std::uint32_t> bucket_hand_counts_p1; ///< Concrete hands per player-one bucket.

		/** @brief Returns blocker-compatible private-pair count for a bucket pair. */
		[[nodiscard]] std::uint32_t valid_pair_count(std::size_t bucket0, std::size_t bucket1) const;
		/** @brief Returns wins minus losses for a bucket pair. */
		[[nodiscard]] std::int32_t net_win_count(std::size_t bucket0, std::size_t bucket1) const;
		/** @brief Returns tied compatible-pair count for a bucket pair. */
		[[nodiscard]] std::uint32_t tie_pair_count(std::size_t bucket0, std::size_t bucket1) const;
		/** @brief Estimates memory occupied by matrix vectors. */
		[[nodiscard]] std::uint64_t estimated_bytes() const noexcept;
	};

	/** @brief Terminal-node reference to a cached board and contribution vector. */
	struct HUNLBucketTerminalBinding
	{
		std::uint32_t cache_index = 0; ///< Index into the showdown matrix cache.
		std::array<int, 2> contributions = { 0, 0 }; ///< Contributions used to compute terminal payoff.
	};

	/** @brief Board and bucket dimensions identifying a cached showdown matrix. */
	struct HUNLBucketTerminalCacheKey
	{
		HUNLFlatPackedBoard board{}; ///< Board shared by the cached matrix.
		std::uint32_t bucket_count_p0 = 0; ///< Player-zero bucket count.
		std::uint32_t bucket_count_p1 = 0; ///< Player-one bucket count.

		/** @brief Compares the board and both bucket dimensions. */
		bool operator==(const HUNLBucketTerminalCacheKey& other) const noexcept;
	};

	/** @brief Cached showdown matrices and payoff bindings indexed by terminal node. */
	class HUNLBucketTerminalTable
	{
	public:
		/** @brief Builds terminal matrices for showdown nodes in graph. */
		static HUNLBucketTerminalTable build(
			const HUNLFlatSolveGraph& graph,
			const HUNLFlatBucketMap& bucket_map);

		/** @brief Reports whether a node has a cached showdown matrix. */
		[[nodiscard]] bool has_showdown_matrix(std::uint32_t node_idx) const noexcept;
		/** @brief Returns the cached matrix associated with a node. */
		[[nodiscard]] const HUNLBucketShowdownMatrix& showdown_matrix(std::uint32_t node_idx) const;
		/** @brief Computes expected showdown payoff using optional bucket reach weights. */
		[[nodiscard]] double expected_showdown_value(
			std::uint32_t node_idx,
			const std::vector<double>* p0_bucket_weights = nullptr,
			const std::vector<double>* p1_bucket_weights = nullptr) const;
		/** @brief Estimates memory occupied by cached showdown matrices and bindings. */
		[[nodiscard]] std::uint64_t estimated_bytes() const noexcept;

	private:
		std::vector<HUNLBucketShowdownMatrix> showdown_cache_;
		std::unordered_map<std::uint32_t, HUNLBucketTerminalBinding> node_bindings_;
		std::array<int, 2> initial_contributions_ = { 0, 0 };
		int initial_pot_ = 0;
		int big_blind_ = 100;
	};

	/** @brief Computes the current fallback value for a depth-limited node and player zero. */
	double heuristic_depth_limited_value_p0(
		const HUNLFlatNodeMeta& node,
		const HUNLConfig& config);

} // namespace texas::solver::hunl

namespace std
{

	template <>
	struct hash<texas::HUNLBucketTerminalCacheKey>
	{
		/** @brief Hashes a cache key consistently with its equality operator. */
		std::size_t operator()(const texas::HUNLBucketTerminalCacheKey& key) const noexcept;
	};

} // namespace std
