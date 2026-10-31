#pragma once

/** @file
 * @brief Records and replays public multiway betting decisions and street transitions.
 */

#include "games/multiway_rules.hpp"
#include "core/poker.hpp"

#include <cstdint>
#include <vector>

namespace texas::games::multiway
{
	/** @brief Public decision event, including the policy seed used by its producer. */
#pragma pack(push, 8)
	struct MultiwayReplayDecision
	{
		core::PlayerId acting_seat = -1;			  ///< Seat that made the decision.
		MultiwayAction action = MultiwayAction::Fold; ///< Action applied by the engine.
		int target_street_contribution = 0;			  ///< Actor's total contribution on the current street after the action.
		std::uint64_t decision_seed = 0;			  ///< Seed retained for reproducing upstream policy sampling.
	};
#pragma pack(pop)

	/** @brief Kind of event in a public multiway hand history. */
	enum class MultiwayReplayEventKind : std::uint8_t
	{
		Decision,		  ///< Apply one betting action.
		StreetTransition, ///< Begin a street after betting is complete.
	};

	/** @brief One decision or street-transition event in replay order. */
#pragma pack(push, 8)
	struct MultiwayReplayEvent
	{
		MultiwayReplayEventKind kind = MultiwayReplayEventKind::Decision; ///< Which payload is active.
		MultiwayReplayDecision decision{};								  ///< Decision payload when kind is Decision.
		core::Street next_street = core::Street::Flop;					  ///< Destination street for a transition.
		core::PlayerId first_player = -1;								  ///< First actor after a transition.
		std::vector<std::uint8_t> board;								  ///< Canonical board dealt at transition; empty is accepted for legacy histories.
	};
#pragma pack(pop)

	/** @brief Versioned public configuration and ordered event stream for one hand. */
#pragma pack(push, 8)
	struct MultiwayHandHistory
	{
		std::uint64_t schema_version = 1;		 ///< Serialization schema version; currently only version 1 is accepted.
		std::uint64_t hand_seed = 0;			 ///< Hand-level seed retained for replay consumers.
		MultiwayGameConfig initial_config{};	 ///< Public state used to construct the initial engine state.
		std::vector<MultiwayReplayEvent> events; ///< Events applied in order.

		/** @brief Creates an empty history from table rules.
		 * @param rules Rules used to create the initial public configuration.
		 * @param first_player First actor in that configuration.
		 * @param hand_seed Seed recorded with the history.
		 */
		[[nodiscard]] static MultiwayHandHistory from_rules(
			const MultiwayGameRules& rules,
			core::PlayerId first_player = 0,
			std::uint64_t hand_seed = 0);
		/** @brief Validates the schema, initial configuration and event payloads.
		 * @throws std::invalid_argument If the schema or an event field is invalid.
		 */
		void validate() const;
	};
#pragma pack(pop)

	/** @brief Applies one event after checking that it matches the current state.
	 * @param state Current public betting state.
	 * @param event Decision or street transition to apply.
	 * @return State after the event.
	 * @throws std::invalid_argument If the event is malformed or not legal now.
	 */
	[[nodiscard]] MultiwayState apply_multiway_replay_event(
		const MultiwayState& state,
		const MultiwayReplayEvent& event);

	/** @brief Reconstructs the final public state by applying the history events in order.
	 * @param history Initial configuration and ordered event stream.
	 * @return State after the final event.
	 * @throws std::invalid_argument If validation or event application fails.
	 */
	[[nodiscard]] MultiwayState replay_multiway_hand(const MultiwayHandHistory& history);

} // namespace texas::games::multiway
