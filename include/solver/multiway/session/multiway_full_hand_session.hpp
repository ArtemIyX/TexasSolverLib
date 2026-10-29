#pragma once

/** @file
 * Full-hand replay state, range-belief updates, decisions, and settlement.
 */

#include "games/multiway_replay.hpp"
#include "solver/multiway/session/multiway_range_belief.hpp"
#include "solver/multiway/resolver/multiway_resolver.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "games/multiway_terminal.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <utility>

namespace texas::solver::multiway
{

	/** Maintains public hand history and beliefs across a complete hand. */
	class MultiwayFullHandSession
	{
	public:
		/** Creates a hand from rules, first actor, and deterministic hand seed. */
		explicit MultiwayFullHandSession(
			games::multiway::MultiwayGameRules rules,
			core::PlayerId first_player = 0,
			std::uint64_t hand_seed = 0);
		/** Creates a hand from explicit config, board, and hand seed. */
		MultiwayFullHandSession(
			games::multiway::MultiwayGameConfig config,
			std::vector<std::uint8_t> board,
			std::uint64_t hand_seed = 0);

		[[nodiscard]] const games::multiway::MultiwayHandHistory& history() const noexcept { return history_; }
		[[nodiscard]] const games::multiway::MultiwayState& state() const noexcept { return state_; }
		[[nodiscard]] const MultiwayRangeBeliefs& beliefs() const noexcept { return beliefs_; }
		[[nodiscard]] const std::vector<std::uint8_t>& board() const noexcept { return board_; }
		/** Applies a replay event and optionally updates the acting seat's belief. */
		const games::multiway::MultiwayState& observe(
			const games::multiway::MultiwayReplayEvent& event,
			const MultiwayRangeBeliefObservation* observation = nullptr);

		/** Resolves the current state for hero cards under the supplied config. */
		[[nodiscard]] MultiwayResolverResult decide(
			PlayerId hero_seat,
			std::array<std::uint8_t, 2> hero_cards,
			const MultiwayResolverConfig& config,
			std::chrono::steady_clock::time_point deadline = {}) const;
		/** Maps an observed preflop action into the configured abstraction menu. */
		[[nodiscard]] MultiwayActionTranslation translate_preflop_action(
			const MultiwayActionDescriptor& observed,
			const std::vector<MultiwayActionDescriptor>& menu,
			const MultiwayActionAbstractionConfig& abstraction = {}) const;
		/** Settles the current hand for all seat-ordered hole cards. */
		[[nodiscard]] games::multiway::MultiwayTerminalResult settle(
			const std::vector<std::array<std::uint8_t, 2>>& holes) const;
		/** Clears any frozen policy associated with the actual hero hand. */
		void clear_actual_hand_policy() noexcept
		{
			actual_hand_frozen_ = false;
			frozen_policy_.reset();
		}
		/** Reports whether an actual-hand policy is currently frozen. */
		[[nodiscard]] bool actual_hand_policy_frozen() const noexcept { return actual_hand_frozen_; }
		/** Freezes current actual-hand policy state without replacing its payload. */
		void freeze_actual_hand_policy() noexcept { actual_hand_frozen_ = true; }
		/** Stores and freezes a resolver policy for the actual hand. */
		void freeze_actual_hand_policy(MultiwayResolverResult policy)
		{
			frozen_policy_ = std::move(policy);
			actual_hand_frozen_ = true;
		}

	private:
		games::multiway::MultiwayHandHistory history_;
		games::multiway::MultiwayState state_;
		MultiwayRangeBeliefs beliefs_;
		std::vector<std::uint8_t> board_;
		mutable std::optional<MultiwayResolverResult> frozen_policy_;
		bool actual_hand_frozen_ = false;
	};

} // namespace texas::solver::multiway
