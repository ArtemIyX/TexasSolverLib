#pragma once

/** @file
 * @brief Declares memoized construction of flat HUNL solve graphs.
 */

#include "games/hunl_flat_graph.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace texas::games::hunl
{

	/** @brief Canonical fixed-storage key used to memoize flat-graph construction. */
#pragma pack(push, 8)
	struct HUNLFlatBuilderMemoKey
	{
		PlayerId cur_player = -1;										  ///< Acting player or chance sentinel.
		std::array<int, 2> contributions = { 0, 0 };					  ///< Current-street contributions.
		std::array<int, 2> stacks = { 0, 0 };							  ///< Remaining stacks.
		std::array<ActionId, HUNL_MAX_HISTORY_CODES> street_history = {}; ///< Current-street actions.
		std::array<bool, 2> folded = { false, false };					  ///< Fold status by player.
		std::array<bool, 2> all_in = { false, false };					  ///< All-in status by player.
		std::array<std::uint8_t, 5> board = { 0, 0, 0, 0, 0 };			  ///< Board prefix, zero-filled after board_count.
		std::array<std::array<std::uint8_t, 2>, 2> hole_cards = { {
			{ 0, 0 },
			{ 0, 0 },
		} }; ///< Private cards, zero-initialized when absent.
		std::array<std::uint8_t, 4> street_lengths = { 0, 0, 0, 0 }; ///< Active history-code length per street.
		std::array<int, HUNL_MAX_HISTORY_CODES> history_codes = {};	 ///< Flattened signed betting-history codes.
		Street street = Street::Preflop;							 ///< Current street.
		std::uint8_t board_count = 0;								 ///< Active card count in board.
		std::uint8_t history_count = 0;								 ///< Active count in history_codes.
		std::uint8_t street_history_count = 0;						 ///< Active count in street_history.
		std::uint8_t pending_board_deals = 0;						 ///< Chance cards still required before betting resumes.
		std::uint8_t street_num_raises = 0;							 ///< Raises made on this street.
		bool has_hole_cards = false;								 ///< Whether hole_cards contains a dealt private state.
		int to_call = 0;											 ///< Chips needed by the actor to call.
		PlayerId street_aggressor = -1;								 ///< Last aggressor or -1.

		/** @brief Captures the state fields that affect flat graph construction.
		 * @throws std::invalid_argument If variable-length state data exceeds fixed capacity.
		 */
		static HUNLFlatBuilderMemoKey from_state(const HUNLState& state);

		/** @brief Compares memoized state fields, including active counts and canonical storage. */
		bool operator==(const HUNLFlatBuilderMemoKey& other) const noexcept;
	};
#pragma pack(pop)

	/** @brief Builds a memoized flat solve graph from an HUNL configuration. */
	class HUNLFlatBuilder
	{
	public:
		/** @brief Builds a graph from the configured initial HUNL state.
		 * @param config Immutable game configuration.
		 * @return Flat representation of the reachable state graph.
		 * @throws std::invalid_argument If the configuration or fixed memo-key capacity is invalid.
		 */
		static HUNLFlatSolveGraph build(std::shared_ptr<const HUNLConfig> config);
	};

} // namespace texas::games::hunl

namespace std
{

#pragma pack(push, 8)
	template <>
	struct hash<texas::HUNLFlatBuilderMemoKey>
	{
		/** @brief Hashes a flat-builder memo key consistently with equality. */
		std::size_t operator()(const texas::HUNLFlatBuilderMemoKey& key) const noexcept;
	};
#pragma pack(pop)

} // namespace std
