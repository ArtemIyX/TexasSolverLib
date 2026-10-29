#pragma once

/** @file
 * @brief Declares the versioned supported cash-game rules profile for multiway games.
 */

#include "games/multiway_state.hpp"
#include "core/types.hpp"

#include <cstdint>

namespace texas::games::multiway
{

	/** @brief Versioned cash-game configuration accepted by the current multiway engine.
	 *
	 * Chip values are exact integers. Validation accepts profiles without antes,
	 * straddles or rebuys, with two through six players and a valid rake policy.
	 */
	struct MultiwayGameRules
	{
		std::uint64_t profile_version = 1; ///< Nonzero version included in identity().
		std::uint8_t player_count = 6; ///< Seat count, from two through six.
		int initial_stack_chips = 10'000; ///< Starting stack assigned to each player.
		int small_blind_chips = 50; ///< Small blind contribution in chips.
		int big_blind_chips = 100; ///< Big blind contribution in chips.
		int ante_chips = 0; ///< Must be zero for the supported profile.
		int straddle_chips = 0; ///< Must be zero for the supported profile.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Rake applied at settlement.
		bool rebuys_enabled = false; ///< Must be false for the supported profile.

		/** @brief Returns the default six-seat cash-game profile. */
		[[nodiscard]] static constexpr MultiwayGameRules standard_6max() noexcept
		{
			return {};
		}

		/** @brief Validates that this profile is supported by the multiway engine.
		 * @throws std::invalid_argument If a field is outside the supported profile or rake policy.
		 */
		void validate() const;
		/** @brief Computes a nonzero fingerprint from the profile fields.
		 * @return Identity value for this configuration.
		 */
		[[nodiscard]] std::uint64_t identity() const noexcept;
		/** @brief Builds the initial preflop game config, posting both blinds.
		 *
		 * first_player is treated as the first voluntary preflop actor; the blinds
		 * occupy the two preceding seats in table order.
		 * @param first_player First voluntary actor, in [0, player_count).
		 * @return Initial game configuration with the selected rake policy.
		 * @throws std::invalid_argument If this profile or first_player is invalid.
		 */
		[[nodiscard]] MultiwayGameConfig make_initial_game_config(core::PlayerId first_player = 0) const;
	};

} // namespace texas::games::multiway
