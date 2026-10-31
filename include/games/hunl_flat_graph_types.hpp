#pragma once

/** @file
 * @brief Lightweight records used by flat HUNL graph interchange.
 */

#include "core/street_types.hpp"
#include "core/value_types.hpp"
#include "games/hunl_terminal_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace texas::games::hunl
{
	struct HUNLConfig;
	struct HUNLTree;

	/** @brief Node category in a flat HUNL solve graph. */
	enum class HUNLFlatNodeType : std::uint8_t
	{
		TerminalFold = 0,	  ///< Fold settlement node.
		TerminalShowdown = 1, ///< Showdown settlement node.
		DepthLimited = 2,	  ///< Non-terminal leaf at the configured depth limit.
		Chance = 3,			  ///< Public card-deal node.
		Decision = 4,		  ///< Player action node.
	};

	/** @brief Compact representation of a community-card board prefix. */
	struct HUNLFlatPackedBoard
	{
		std::array<std::uint8_t, 5> cards = { 0, 0, 0, 0, 0 }; ///< Active cards followed by zeroed unused entries.
		std::uint8_t count = 0;								   ///< Number of active board cards.
	};

	/** @brief Sentinel used when an infoset has no debug-key entry. */
	inline constexpr std::uint32_t HUNLFlatInvalidDebugIndex = std::numeric_limits<std::uint32_t>::max();

	/** @brief Compact metadata and packed ranges for one graph node. */
	struct HUNLFlatNodeMeta
	{
		std::uint32_t child_begin = 0;						   ///< First child index in graph children.
		std::uint32_t child_count = 0;						   ///< Number of child indices for this node.
		std::uint32_t action_begin = 0;						   ///< First aligned action in graph actions.
		std::uint32_t chance_begin = 0;						   ///< First chance outcome in graph outcomes.
		std::uint32_t chance_count = 0;						   ///< Number of chance outcomes for this node.
		core::InfosetId infoset_id{};						   ///< Owning information set when has_infoset is true.
		std::array<int, 2> contributions = { 0, 0 };		   ///< Current-street contributions by player.
		std::array<double, 2> terminal_utility = { 0.0, 0.0 }; ///< Player utilities at a terminal leaf.
		HUNLFlatPackedBoard board{};						   ///< Board observed at this node.
		TerminalKind terminal_kind{};						   ///< Fold/showdown classification for leaves.
		core::PlayerId player = -1;							   ///< Acting player, or sentinel for chance/terminal nodes.
		HUNLFlatNodeType type = HUNLFlatNodeType::Decision;	   ///< Node category.
		core::Street street = core::Street::Preflop;		   ///< Street associated with this node.
		std::uint8_t action_count = 0;						   ///< Number of actions at a decision node.
		bool has_infoset = false;							   ///< Whether infoset_id identifies a decision information set.
	};

	/** @brief One public-card outcome and its child node. */
	struct HUNLFlatChanceOutcome
	{
		std::uint8_t action = 0;		///< Encoded card dealt by the outcome.
		double probability = 0.0;		///< Outcome probability.
		std::uint32_t child = 0;		///< Child node index.
		std::uint32_t multiplicity = 1; ///< Number of concrete outcomes represented by this entry.
	};

	/** @brief Flat metadata for one information set and its contiguous node range. */
	struct HUNLFlatInfoset
	{
		core::InfosetId id{};									   ///< Stable ID within this graph.
		std::uint32_t node_begin = 0;							   ///< First node reference in graph infoset nodes.
		std::uint32_t node_count = 0;							   ///< Number of node references in this information set.
		HUNLFlatPackedBoard board{};							   ///< Public board shared by the information set.
		std::uint32_t debug_key_index = HUNLFlatInvalidDebugIndex; ///< Index in debug keys or invalid sentinel.
		core::PlayerId player = -1;								   ///< Player who owns the information set.
		core::Street street = core::Street::Preflop;			   ///< Street shared by member nodes.
		std::uint8_t action_count = 0;							   ///< Shared action count.
	};

	/** @brief Half-open contiguous range encoded as a start and element count. */
	struct HUNLFlatSlice
	{
		std::uint32_t begin = 0; ///< First element in the associated flat array.
		std::uint32_t count = 0; ///< Number of elements in the range.
	};

	/** @brief Half-open node-index interval assigned to a worker. */
	struct HUNLFlatWorkerRange
	{
		std::uint32_t begin = 0; ///< First node index.
		std::uint32_t end = 0;	 ///< One past the last node index.
	};

	static_assert(std::is_trivially_copyable_v<HUNLFlatChanceOutcome>);
	static_assert(std::is_trivially_copyable_v<HUNLFlatPackedBoard>);
	static_assert(std::is_trivially_copyable_v<HUNLFlatSlice>);
	static_assert(std::is_trivially_copyable_v<HUNLFlatWorkerRange>);
	static_assert(std::is_trivially_copyable_v<HUNLFlatNodeMeta>);

	/** @brief Compact indexed representation of a reachable HUNL solve graph. */
	struct HUNLFlatSolveGraph
	{
		std::vector<HUNLFlatNodeMeta> node_meta;						   ///< Metadata indexed by node ID.
		std::vector<std::uint32_t> children;							   ///< Child node indices in per-node contiguous ranges.
		std::vector<core::ActionId> actions;							   ///< Actions aligned with decision-node child ranges.
		std::vector<HUNLFlatChanceOutcome> chance_outcomes;				   ///< Chance entries in per-node contiguous ranges.
		std::vector<HUNLFlatInfoset> infosets;							   ///< Information-set metadata indexed by InfosetId.
		std::vector<std::string> infoset_debug_keys;					   ///< Optional-readable keys indexed by debug_key_index.
		std::vector<std::uint32_t> infoset_nodes;						   ///< Node indices grouped by information set.
		std::vector<std::uint32_t> forward_order;						   ///< Topological order from roots toward leaves.
		std::vector<std::uint32_t> reverse_order;						   ///< Reverse topological order.
		std::vector<std::uint32_t> node_depths;							   ///< Topological depth indexed by node.
		std::array<HUNLFlatSlice, 5> street_slices = {};				   ///< Street-order ranges for the five Street values.
		std::vector<std::uint32_t> street_order;						   ///< Node indices grouped by street.
		std::vector<HUNLFlatSlice> depth_slices;						   ///< Depth-order ranges indexed by depth.
		std::vector<std::uint32_t> depth_order;							   ///< Node indices grouped by topological depth.
		std::vector<std::vector<HUNLFlatWorkerRange>> depth_worker_ranges; ///< Worker partitions per depth.
		std::vector<std::uint32_t> terminal_nodes;						   ///< Terminal and depth-limited leaf indices.
		std::vector<double> terminal_node_values;						   ///< Player-zero values aligned with terminal_nodes.
		std::vector<std::uint32_t> fold_terminal_nodes;					   ///< Fold-leaf indices.
		std::vector<double> fold_terminal_values;						   ///< Player-zero values aligned with fold_terminal_nodes.
		std::vector<std::uint32_t> showdown_terminal_nodes;				   ///< Showdown-leaf indices.
		std::vector<double> showdown_terminal_values;					   ///< Player-zero values aligned with showdown_terminal_nodes.
		std::uint32_t root = 0;											   ///< Root node index.
		std::uint32_t max_depth = 0;									   ///< Maximum construction depth.
		std::uint8_t max_actions = 0;									   ///< Maximum decision action count.
		std::shared_ptr<const HUNLConfig> config;						   ///< Immutable configuration used to build the graph.

		/** @brief Returns the number of graph nodes. */
		[[nodiscard]] std::size_t node_count() const noexcept;
		/** @brief Unpacks the board stored on a node.
		 * @throws std::out_of_range If node_idx is outside node_meta.
		 */
		[[nodiscard]] std::vector<std::uint8_t> node_board(std::uint32_t node_idx) const;
		/** @brief Unpacks the board stored on an information set.
		 * @throws std::out_of_range If infoset_id is outside infosets.
		 */
		[[nodiscard]] std::vector<std::uint8_t> infoset_board(core::InfosetId infoset_id) const;
		/** @brief Returns the debug key for an information set, or an empty view when unavailable. */
		[[nodiscard]] std::string_view infoset_key(core::InfosetId infoset_id) const noexcept;
		/** @brief Returns the debug key referenced by an information set, or empty when unavailable. */
		[[nodiscard]] std::string_view infoset_key(const HUNLFlatInfoset& infoset) const noexcept;
		/** @brief Packs up to five board cards into fixed-capacity storage.
		 * @throws std::invalid_argument If board contains more than five cards.
		 */
		[[nodiscard]] static HUNLFlatPackedBoard pack_board(const std::vector<std::uint8_t>& board);
		/** @brief Returns the active card prefix stored in a packed board. */
		[[nodiscard]] static std::vector<std::uint8_t> unpack_board(const HUNLFlatPackedBoard& board);
		/** @brief Converts an already-built recursive tree into the flat graph representation. */
		static HUNLFlatSolveGraph build(const HUNLTree& tree);
		/** @brief Builds a recursive tree from config and converts it to a flat graph. */
		static HUNLFlatSolveGraph build(std::shared_ptr<const HUNLConfig> config);
	};
} // namespace texas::games::hunl
