#pragma once

/** @file
 * @brief Multiway rake policy values and operations.
 */

#include <cstdint>

namespace texas::games::multiway
{
	/** @brief Supported rake calculations for multiway pot settlement. */
	enum class MultiwayRakeMode : std::uint8_t
	{
		ExplicitZero,			  ///< Removes no chips from contested pots.
		PercentageOfContestedPot, ///< Applies a capped percentage to the contested-pot total.
	};

	/** @brief Value policy describing how rake is removed during settlement.
	 * Percentage rake is rounded down in chips and applied once to the combined
	 * contested-pot total. Uncalled-contribution refunds are not raked.
	 */
	struct MultiwayRakePolicy
	{
		MultiwayRakeMode mode = MultiwayRakeMode::ExplicitZero; ///< Rake calculation to use.
		std::uint16_t basis_points = 0;							///< Percentage rate in hundredths of a percent.
		int cap = 0;											///< Maximum rake in chips for percentage mode.
		bool no_flop_no_drop = true;							///< Skip percentage rake when flop_seen is false.

		/** @brief Returns the explicit-zero rake policy. */
		[[nodiscard]] static constexpr MultiwayRakePolicy explicit_zero() noexcept { return {}; }
		/** @brief Validates the field combination for the selected rake mode.
		 * @throws std::invalid_argument If the mode, rate, cap or no-flop setting is invalid.
		 */
		void validate() const;
		/** @brief Calculates capped rake on a contested pot.
		 * @param contested_pot Combined contested-pot amount in chips; must be non-negative.
		 * @param flop_seen Whether the flop was dealt.
		 * @return Zero for explicit-zero or skipped no-flop rake; otherwise floored percentage, capped in chips.
		 * @throws std::invalid_argument If the policy is invalid or contested_pot is negative.
		 */
		[[nodiscard]] int rake_for_contested_pot(int contested_pot, bool flop_seen) const;
		/** @brief Returns a compact value derived from this policy's fields. */
		[[nodiscard]] std::uint64_t identity() const noexcept;
	};
} // namespace texas::games::multiway
