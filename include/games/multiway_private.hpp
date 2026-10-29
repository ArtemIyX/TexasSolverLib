#pragma once

/** @file
 * @brief Declares private-hand ranges, blocker-aware sampling and showdown evaluation.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/multiway_terminal.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <initializer_list>

namespace texas::games::multiway
{

	/** @brief One private hand and its non-negative range weight. */
	struct MultiwayWeightedHole
	{
		std::array<std::uint8_t, 2> hole = { 0, 0 }; ///< Unordered pair of encoded card indices.
		double weight = 0.0;						 ///< Relative mass assigned to this hand.
	};

	/** @brief Public board and one weighted private-hand range per seat. */
	struct MultiwayPrivateConfig
	{
		std::vector<std::uint8_t> board;					   ///< Known public cards, from zero through five.
		std::vector<std::vector<MultiwayWeightedHole>> ranges; ///< Weighted hands by seat.
		/** @brief Validates seat counts, card blockers and positive finite range mass.
		 * @throws std::invalid_argument If the board or any range is invalid.
		 */
		void validate() const;
	};

	/** @brief Result of checking whether configured ranges admit a joint deal. */
	enum class MultiwayPrivateRangeFeasibilityStatus : std::uint8_t
	{
		Feasible,			   ///< A blocker-compatible assignment was found.
		Infeasible,			   ///< Search exhausted the combinations without finding an assignment.
		SearchBudgetExhausted, ///< The node budget ended the search before a conclusion.
	};

	/** @brief Bounded-search status and counters for a private-range preflight. */
	struct MultiwayPrivateRangeFeasibilityResult
	{
		MultiwayPrivateRangeFeasibilityStatus status = MultiwayPrivateRangeFeasibilityStatus::Infeasible; ///< Search conclusion.
		std::uint64_t visited_nodes = 0;																  ///< Search nodes visited before conclusion.
		std::uint64_t node_budget = 0;																	  ///< Maximum nodes requested for the search.
		std::string reason;																				  ///< Human-readable explanation matching status.
	};

	/** @brief Checks for a compatible joint private deal before worker sampling.
	 * @param config Validated board and seat ranges to check.
	 * @param node_budget Maximum recursive search nodes; zero exhausts before visiting a node.
	 * @return Feasibility status, visited-node count, budget and reason.
	 * @throws std::invalid_argument If config fails validation.
	 */
	MultiwayPrivateRangeFeasibilityResult preflight_multiway_private_range_feasibility(
		const MultiwayPrivateConfig& config,
		std::uint64_t node_budget = 1'000'000U);

	/** @brief Fixed-capacity collection of up to six two-card private hands. */
	struct MultiwayFixedPrivateHoles
	{
		std::array<std::array<std::uint8_t, 2>, 6> values = {}; ///< Active hole pairs occupy entries [0, count).
		std::uint8_t count = 0U;								///< Number of active hands.

		/** @brief Constructs an empty collection. */
		MultiwayFixedPrivateHoles() = default;
		/** @brief Copies up to six hole pairs from an initializer list.
		 * @param input Source pairs; entries after the sixth are ignored.
		 */
		MultiwayFixedPrivateHoles(std::initializer_list<std::array<std::uint8_t, 2>> input)
		{
			*this = input;
		}
		/** @brief Replaces the active pairs with up to six initializer-list entries.
		 * @param input Source pairs; entries after the sixth are ignored.
		 * @return Reference to this collection.
		 */
		MultiwayFixedPrivateHoles& operator=(
			std::initializer_list<std::array<std::uint8_t, 2>> input) noexcept
		{
			count = static_cast<std::uint8_t>(std::min(input.size(), values.size()));
			std::copy_n(input.begin(), count, values.begin());
			return *this;
		}
		/** @brief Returns the number of active hole pairs. */
		[[nodiscard]] std::size_t size() const noexcept { return count; }
		/** @brief Reports whether there are no active hole pairs. */
		[[nodiscard]] bool empty() const noexcept { return count == 0U; }
		/** @brief Returns an iterator to the first active pair. */
		[[nodiscard]] auto begin() const noexcept { return values.begin(); }
		/** @brief Returns an iterator past the last active pair. */
		[[nodiscard]] auto end() const noexcept { return values.begin() + count; }
		/** @brief Returns a mutable iterator to the first active pair. */
		[[nodiscard]] auto begin() noexcept { return values.begin(); }
		/** @brief Returns a mutable iterator past the last active pair. */
		[[nodiscard]] auto end() noexcept { return values.begin() + count; }
		/** @brief Returns the active pair at index without bounds checking. */
		[[nodiscard]] auto& operator[](std::size_t index) noexcept { return values[index]; }
		/** @brief Returns the active pair at index without bounds checking. */
		[[nodiscard]] const auto& operator[](std::size_t index) const noexcept { return values[index]; }
		/** @brief Compares active pair counts and their ordered contents.
		 * @param left First collection.
		 * @param right Second collection.
		 * @return True when the active counts and pairs match.
		 */
		friend bool operator==(const MultiwayFixedPrivateHoles& left,
			const MultiwayFixedPrivateHoles& right) noexcept
		{
			return left.count == right.count && std::equal(left.begin(), left.end(), right.begin());
		}
	};

	/** @brief Blocker-compatible private deal and sampling-probability diagnostics. */
	struct MultiwayJointPrivateSample
	{
		MultiwayFixedPrivateHoles holes{}; ///< Accepted private hand by seat, if any.
		std::uint32_t attempts = 0;		   ///< Sequential joint-deal attempts made.
		// Samples are drawn sequentially from each seat's blocker-compatible
		// range mass. The probability fields contain the product of those
		// conditional proposal probabilities.
		double chance_reach = 0.0;				   ///< Product of conditional proposal probabilities for the accepted deal.
		double conditional_deal_probability = 0.0; ///< Product probability under sequential blocker-conditioned draws.
		double proposal_reach = 0.0;			   ///< Product of conditional proposal probabilities used by the sampler.
		double inclusion_reach = 0.0;			   ///< Inclusion weight recorded for the sample.
		std::uint32_t accepted_trajectories = 0;   ///< Accepted trajectory count reported by the sampler.
		std::uint32_t rejected_trajectories = 0;   ///< Rejected trajectory count reported by the sampler.
		std::uint32_t discarded_trajectories = 0;  ///< Discarded trajectory count reported by the sampler.

		/** @brief Reports whether no hands were accepted. */
		[[nodiscard]] bool empty() const noexcept { return holes.empty(); }
		/** @brief Returns the number of accepted seat hands. */
		[[nodiscard]] std::size_t size() const noexcept { return holes.size(); }
		/** @brief Returns an iterator to the first accepted hand. */
		[[nodiscard]] auto begin() const noexcept { return holes.begin(); }
		/** @brief Returns an iterator past the last accepted hand. */
		[[nodiscard]] auto end() const noexcept { return holes.end(); }
	};

	/** @brief Reusable fixed-size scratch state for one private-range draw. */
	struct MultiwayPrivateWorkerScratch
	{
		std::array<std::array<std::uint8_t, 2>, 6> holes = {}; ///< Sampled hand by seat.
		std::array<bool, 64> used = {};						   ///< Board and already-selected card blockers.
		std::uint8_t seat_count = 0;						   ///< Number of sampled seats on success.
		std::uint32_t attempts = 0;							   ///< Sequential joint-deal attempts made.
		double chance_reach = 0.0;							   ///< Product of conditional proposal probabilities.
		double conditional_deal_probability = 0.0;			   ///< Probability product under sequential draws.
		double proposal_reach = 0.0;						   ///< Proposal probability product.
		double inclusion_reach = 0.0;						   ///< Inclusion weight recorded for the draw.
		std::uint32_t accepted_trajectories = 0;			   ///< Accepted trajectory count.
		std::uint32_t rejected_trajectories = 0;			   ///< Rejected trajectory count.
		std::uint32_t discarded_trajectories = 0;			   ///< Discarded trajectory count.
	};

	/** @brief Canonicalized weighted ranges for blocker-aware worker sampling.
	 *
	 * Reversed and duplicate hole-card entries are merged during construction.
	 */
	class MultiwayCompiledPrivateRanges
	{
	public:
		/** @brief Compiles and validates per-seat ranges for worker draws.
		 * @param config Board and ranges to compile.
		 * @throws std::invalid_argument If config is invalid or infeasible.
		 * @throws std::runtime_error If feasibility preflight exhausts its node budget.
		 */
		explicit MultiwayCompiledPrivateRanges(const MultiwayPrivateConfig& config);

		/** @brief Attempts one sequential blocker-compatible draw into caller scratch.
		 *
		 * Scratch fields are reset and filled for the attempt. A false result means
		 * the selected sequence reached a seat with no compatible positive mass.
		 * @param seed Seed used to initialize the sampler.
		 * @param scratch Worker-owned fixed-size output and temporary storage.
		 * @return True when one hand was selected for every configured seat.
		 */
		[[nodiscard]] bool try_sample_into(
			std::uint64_t seed,
			MultiwayPrivateWorkerScratch& scratch) const noexcept;
		/** @brief Samples into scratch or reports failure with an exception.
		 * @param seed Seed used to initialize the sampler.
		 * @param scratch Worker-owned fixed-size output and temporary storage.
		 * @throws std::runtime_error If the sequential draw has no compatible continuation.
		 */
		void sample_into(std::uint64_t seed, MultiwayPrivateWorkerScratch& scratch) const;
		/** @brief Returns the number of configured seat ranges. */
		[[nodiscard]] std::size_t seat_count() const noexcept;

	private:
		std::vector<std::uint8_t> board_;
		std::vector<std::vector<MultiwayWeightedHole>> ranges_;
	};

	/** @brief Samples one blocker-compatible private hand per seat.
	 * @param config Board and weighted ranges to sample.
	 * @param seed Seed used to initialize the sampler.
	 * @return Accepted hands and proposal-probability diagnostics.
	 * @throws std::invalid_argument If config is invalid.
	 * @throws std::runtime_error If no compatible sequential draw is produced.
	 */
	MultiwayJointPrivateSample sample_multiway_private_hands(
		const MultiwayPrivateConfig& config,
		std::uint64_t seed);

	/** @brief Complete board, private cards and betting data for showdown evaluation. */
	struct MultiwayShowdownInput
	{
		std::vector<std::uint8_t> board;									  ///< Five distinct community-card indices.
		std::vector<std::array<std::uint8_t, 2>> holes;						  ///< Distinct private pair by seat.
		std::vector<int> contributions;										  ///< Total chips committed by each seat.
		std::vector<bool> folded;											  ///< Whether each seat folded before showdown.
		PlayerId odd_chip_first_seat = 0;									  ///< First seat for odd split-chip assignment.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Policy applied to contested pots.
		bool flop_seen = true;												  ///< Whether the flop was dealt for no-flop-no-drop rake.

		/** @brief Validates board, seat dimensions, card uniqueness and rake policy.
		 * @throws std::invalid_argument If any checked field is invalid.
		 */
		void validate() const;
	};

	/** @brief Evaluates each seat's seven-card hand, then settles the pots.
	 * @param input Complete board, private hands, contributions and settlement policy.
	 * @return Multiway terminal settlement with evaluated strengths.
	 * @throws std::invalid_argument If the showdown input or settlement data is invalid.
	 */
	MultiwayTerminalResult evaluate_multiway_showdown(const MultiwayShowdownInput& input);

} // namespace texas::games::multiway
