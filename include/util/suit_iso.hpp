#pragma once

/** @file
 * Suit-symmetry classes and chance-node collapse data structures.
 */

#include "solver/generic/exploit.hpp"
#include "games/hunl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace texas::util
{

	/** Permutation mapping each original suit index to a canonical suit index. */
	using SuitPerm = std::array<std::uint8_t, 4>;

	/** Dealt-card outcomes grouped with the suit permutation to their representative. */
#pragma pack(push, 8)
	struct IsoClass
	{
		std::size_t representative_child_idx = 0;
		std::vector<std::pair<std::size_t, SuitPerm>> members;
	};
#pragma pack(pop)

	/** Canonical public chance outcome and its combined probability/multiplicity. */
#pragma pack(push, 8)
	struct PublicChanceClass
	{
		std::size_t representative_outcome_idx = 0;
		std::uint8_t representative_action = 0;
		double probability = 0.0;
		std::uint32_t multiplicity = 0;
	};
#pragma pack(pop)

	/** Child member of a collapsed class and its per-player hand mappings. */
#pragma pack(push, 8)
	struct CollapseMember
	{
		std::size_t child_idx = 0;
		std::array<std::vector<std::uint32_t>, 2> sigma;
		SuitPerm rel_perm = { 0, 1, 2, 3 };
	};
#pragma pack(pop)

	/** Representative child and equivalent children under suit symmetry. */
#pragma pack(push, 8)
	struct CollapseClass
	{
		std::size_t representative_child_idx = 0;
		std::vector<CollapseMember> members;
	};
#pragma pack(pop)

	/** Chance children partition and whether the node is symmetric. */
#pragma pack(push, 8)
	struct ChanceCollapse
	{
		std::vector<CollapseClass> classes;
		bool symmetric = false;
	};
#pragma pack(pop)

	/** Optional precomputed chance collapses indexed by flat-tree node. */
#pragma pack(push, 8)
	struct SuitIsoCache
	{
		std::vector<std::optional<ChanceCollapse>> nodes;

		/** Returns the cached collapse for node_idx, or null when absent. */
		const ChanceCollapse* get(std::size_t node_idx) const;
		/** Reports whether any cache entries are populated. */
		bool is_active() const;
	};
#pragma pack(pop)

	/** Applies a suit permutation to one card id. */
	std::uint8_t apply_perm_to_card(const SuitPerm& perm, std::uint8_t card);
	/** Tests whether a suit permutation fixes every card in the board prefix. */
	bool fixes_board(const SuitPerm& perm, const std::vector<std::uint8_t>& prefix_board);
	/** Returns indexes of suit permutations that leave the board fixed. */
	std::vector<std::size_t> board_stabilizer(const std::vector<std::uint8_t>& prefix_board);
	/** Groups dealt-card children by board-preserving suit symmetry. */
	std::vector<IsoClass> group_chance_children(
		const std::vector<std::uint8_t>& prefix_board,
		const std::vector<std::uint8_t>& dealt_cards);
	/** Canonicalizes and combines equivalent public chance outcomes. */
	std::vector<PublicChanceClass> canonicalize_public_chance_outcomes(
		const std::vector<std::uint8_t>& prefix_board,
		const std::vector<ChanceOutcome>& outcomes);
	/** Sorts hole cards and constructs the canonical hand-index list. */
	std::vector<std::array<std::uint8_t, 2>> build_hole_index(
		const std::vector<std::array<std::uint8_t, 2>>& holes);
	/** Maps hand indexes under a relative suit permutation when valid. */
	std::optional<std::vector<std::uint32_t>> hand_index_permutation(
		const std::vector<std::array<std::uint8_t, 2>>& holes,
		const std::vector<std::array<std::uint8_t, 2>>& hole_index,
		const SuitPerm& rel_perm);

	/** Precomputes available suit-collapse classes for flat-tree chance nodes. */
	SuitIsoCache build_suit_iso_cache(
		const std::vector<FlatNode>& nodes,
		const std::vector<std::optional<std::uint8_t>>& dealt_cards,
		const std::vector<std::uint8_t>& initial_board,
		const std::array<std::vector<std::array<std::uint8_t, 2>>, 2>& holes,
		const std::array<const std::vector<double>*, 2>& reach);

	/** Marks non-representative nodes that should be skipped during traversal. */
	std::vector<bool> member_skip_mask(const std::vector<FlatNode>& nodes, const SuitIsoCache& cache);

} // namespace texas::util
