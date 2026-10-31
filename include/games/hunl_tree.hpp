#pragma once

/** @file
 * @brief Declares recursive HUNL tree nodes, construction and memoization keys.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/hunl.hpp"
#include "games/hunl_terminal_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace texas::games::hunl
{

	/** @brief Memoization key forward-declared for tree construction. */
	struct MemoKey;

	/** @brief Node and contiguous child data for one recursive tree state. */
#pragma pack(push, 8)
	struct HUNLTreeNode
	{
		PlayerId player = -1;										  ///< Actor, or sentinel for chance and terminal nodes.
		TerminalKind terminal_kind = TerminalKind::non_terminal();	  ///< Terminal classification.
		bool depth_limited_leaf = false;							  ///< True when this node stops at configured tree depth.
		std::array<double, 2> terminal_utility = { 0.0, 0.0 };		  ///< Utility by player at terminal states.
		std::array<int, 2> contrib = { 0, 0 };						  ///< Current-street contributions by player.
		Street street = Street::Preflop;							  ///< Street represented by the node.
		std::vector<std::uint8_t> board;							  ///< Board dealt at this state.
		std::uint8_t num_actions = 0;								  ///< Number of actions at a decision node.
		std::vector<ActionId> legal_actions;						  ///< Legal actions aligned with children.
		std::vector<std::uint32_t> children;						  ///< Decision child indices aligned with legal_actions.
		std::optional<std::string> infoset_key = std::nullopt;		  ///< Decision information-set key, when present.
		std::optional<std::uint8_t> chance_action = std::nullopt;	  ///< Incoming chance card for a shared chance child.
		double chance_prob = 1.0;									  ///< Probability associated with the incoming chance action.
		std::vector<std::pair<std::uint8_t, double>> chance_outcomes; ///< Card and probability by outgoing chance branch.
		std::vector<std::uint32_t> chance_children;					  ///< Child indices aligned with chance_outcomes.

		/** @brief Creates an empty node with its actor, contributions, street and board. */
		static HUNLTreeNode empty(
			PlayerId player,
			const std::array<int, 2>& contrib,
			Street street,
			std::vector<std::uint8_t> board);
	};
#pragma pack(pop)

	/** @brief Memoized recursive HUNL game tree and its construction metadata. */
#pragma pack(push, 8)
	struct HUNLTree
	{
		std::vector<HUNLTreeNode> nodes;		  ///< Nodes indexed by their uint32_t IDs.
		std::uint32_t root = 0;					  ///< Root node ID.
		std::uint32_t max_depth = 0;			  ///< Maximum depth reached during construction.
		std::uint8_t max_actions = 0;			  ///< Maximum legal-action count among decision nodes.
		std::shared_ptr<const HUNLConfig> config; ///< Immutable source configuration.

		/** @brief Builds a memoized tree from the configured initial state.
		 * @param config Immutable game configuration.
		 */
		static HUNLTree build(std::shared_ptr<const HUNLConfig> config);

	private:
		std::uint32_t build_node(
			const HUNLState& state,
			std::unordered_map<MemoKey, std::uint32_t>& memo,
			std::uint32_t depth,
			std::uint32_t depth_limit_plies);
	};
#pragma pack(pop)

	/** @brief Variable-capacity state key used to deduplicate recursive tree nodes. */
#pragma pack(push, 8)
	struct MemoKey
	{
		PlayerId cur_player = -1;															 ///< Actor or chance sentinel.
		std::array<int, 2> contributions = { 0, 0 };										 ///< Current-street contributions.
		std::array<int, 2> stacks = { 0, 0 };												 ///< Remaining stacks.
		Street street = Street::Preflop;													 ///< Current street.
		std::vector<ActionId> street_history;												 ///< Current-street action sequence.
		std::vector<std::vector<std::uint8_t>> completed_streets;							 ///< Encoded completed-street history tokens.
		std::vector<std::string> current_street_tokens;										 ///< Current-street display tokens.
		std::array<bool, 2> folded = { false, false };										 ///< Fold status.
		std::array<bool, 2> all_in = { false, false };										 ///< All-in status.
		std::vector<std::uint8_t> board;													 ///< Community cards dealt so far.
		std::optional<std::array<std::array<std::uint8_t, 2>, 2>> hole_cards = std::nullopt; ///< Private cards, when dealt.
		std::uint8_t pending_board_deals = 0;												 ///< Chance cards left before betting resumes.
		int to_call = 0;																	 ///< Amount the actor must add to call.
		std::uint8_t street_num_raises = 0;													 ///< Raises on current street.
		PlayerId street_aggressor = -1;														 ///< Last aggressor or -1.

		/** @brief Extracts memoization fields from a game state. */
		static MemoKey from_state(const HUNLState& state);

		/** @brief Compares all state fields used for memoization. */
		bool operator==(const MemoKey& other) const noexcept;
	};
#pragma pack(pop)

	/** @brief Classifies a game state as non-terminal, fold or showdown. */
	TerminalKind classify_terminal_kind(const HUNLState& state);

} // namespace texas::games::hunl

namespace std
{

#pragma pack(push, 8)
	template <>
	struct hash<texas::MemoKey>
	{
		/** @brief Hashes a memo key consistently with its equality operator. */
		std::size_t operator()(const texas::MemoKey& key) const noexcept;
	};
#pragma pack(pop)

} // namespace std
