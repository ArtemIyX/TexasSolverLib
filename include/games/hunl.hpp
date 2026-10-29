#pragma once

/** @file
 * @brief Declares HUNL configuration, state, action menus and range encoding.
 */

#include "core/legacy_namespace_compat.hpp"
#include "core/poker.hpp"

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace texas::util
{
	struct AbstractionTables;
}

namespace texas::games::hunl
{
	/** @brief Maximum number of encoded betting-history actions. */
	struct HUNLConfig;
	inline constexpr std::size_t HUNL_MAX_HISTORY_CODES = 48;

	/** @brief Selects the postflop graph representation used by the flat solver. */
	enum class HUNLFlatSolveMode : std::uint8_t
	{
		Auto = 0, ///< Choose a supported solve mode from the configuration.
		ExplicitHand = 1, ///< Solve an explicitly dealt private hand.
		Bucketed = 2, ///< Solve against bucket-indexed private ranges.
	};

	/** @brief Selects how configured private hand ranges are interpreted. */
	enum class HUNLRangePolicy : std::uint8_t
	{
		Unspecified = 0, ///< Infer from whether initial ranges are present.
		Uniform = 1, ///< Use uniform private hands.
		UseInitialRanges = 2, ///< Require and use the configured initial ranges.
		RequireExplicit = 3, ///< Require an explicit range contract.
	};

	/** @brief One private hand and its non-negative range weight. */
	struct HUNLWeightedHand
	{
		std::array<std::uint8_t, 2> hole = { 0, 0 }; ///< Two encoded private cards.
		double weight = 0.0; ///< Relative hand weight.
	};

	/** @brief One abstraction bucket and its non-negative range weight. */
	struct HUNLWeightedBucket
	{
		Street street = Street::Preflop; ///< Street for which the bucket is defined.
		std::uint32_t bucket = 0; ///< Bucket index within the street's abstraction.
		double weight = 0.0; ///< Relative bucket weight.
	};

	/** @brief Hand and/or bucket weights supplied for one player. */
	struct HUNLRangeInput
	{
		std::vector<HUNLWeightedHand> hand_weights; ///< Explicit private hands, if provided.
		std::vector<HUNLWeightedBucket> bucket_weights; ///< Abstract buckets, if provided.
	};

	/** @brief A two-player private deal with its normalized joint reach weight. */
	struct HUNLJointRangeDeal
	{
		std::array<std::array<std::uint8_t, 2>, 2> hole = {}; ///< Private hand by player.
		double weight = 0.0; ///< Normalized joint weight after blocker conditioning.
	};

	// Joint root reach after board and cross-player blocker conditioning.  This
	// is intentionally explicit rather than a Cartesian product hidden inside a
	// solver backend; sampled traversal may consume it without materializing any
	// additional private-state tables.
	/** @brief Builds normalized two-player deals after conditioning on card blockers.
	 * @param config Validated starting state and player ranges.
	 * @return Compatible private deals whose weights sum to one.
	 * @throws std::invalid_argument If the ranges have no compatible positive-mass deal.
	 */
	std::vector<HUNLJointRangeDeal> normalize_hunl_joint_range(
		const HUNLConfig& config);
	/** @brief Checks range feasibility without materializing joint deals.
	 * @param config Starting state and player ranges to check.
	 * @throws std::invalid_argument If the configuration or ranges are invalid or infeasible.
	 */
	void validate_hunl_joint_range_feasibility(const HUNLConfig& config);

	/** @brief Stable action identifiers used by the HUNL action menu. */
	inline constexpr ActionId ACTION_FOLD = 0;
	inline constexpr ActionId ACTION_CHECK = 1;
	inline constexpr ActionId ACTION_CALL = 2;
	inline constexpr ActionId ACTION_BET_33 = 3;
	inline constexpr ActionId ACTION_BET_75 = 4;
	inline constexpr ActionId ACTION_BET_100 = 5;
	inline constexpr ActionId ACTION_BET_150 = 6;
	inline constexpr ActionId ACTION_BET_200 = 7;
	inline constexpr ActionId ACTION_RAISE_33 = 8;
	inline constexpr ActionId ACTION_RAISE_75 = 9;
	inline constexpr ActionId ACTION_RAISE_100 = 10;
	inline constexpr ActionId ACTION_RAISE_150 = 11;
	inline constexpr ActionId ACTION_RAISE_200 = 12;
	inline constexpr ActionId ACTION_ALL_IN = 13;

	/** @brief Reports whether an action identifier denotes an opening bet. */
	bool is_opening_bet(ActionId action);
	/** @brief Reports whether an action identifier denotes a raise. */
	bool is_raise(ActionId action);

	/** @brief Fixed-capacity information needed to construct an HUNL infoset key. */
	struct HUNLInfosetEncoding
	{
		std::array<std::uint8_t, 2> hole = { 0, 0 }; ///< Acting player's private cards.
		std::array<std::uint8_t, 5> board = { 0, 0, 0, 0, 0 }; ///< Board prefix; unused entries must be zero.
		std::array<std::uint8_t, 4> street_lengths = { 0, 0, 0, 0 }; ///< History-code count for each street segment.
		std::array<int, HUNL_MAX_HISTORY_CODES> history_codes = {}; ///< Concatenated signed action-history codes.
		std::uint8_t board_count = 0; ///< Number of active board cards.
		Street street = Street::Preflop; ///< Current street.
		std::uint8_t history_count = 0; ///< Number of active history codes.

		/** @brief Compares every active and canonical storage field. */
		bool operator==(const HUNLInfosetEncoding& other) const noexcept
		{
			return hole == other.hole && board == other.board && street_lengths == other.street_lengths && history_codes == other.history_codes && board_count == other.board_count && street == other.street && history_count == other.history_count;
		}
	};

	/** @brief Hashes an encoded HUNL information set. */
	struct HUNLInfosetEncodingHash
	{
		/** @brief Produces a hash consistent with HUNLInfosetEncoding equality. */
		std::size_t operator()(const HUNLInfosetEncoding& encoding) const noexcept;
	};

	/** @brief Validates dimensions, cards, street segments and canonical unused storage.
	 * @throws std::invalid_argument If the encoding is invalid.
	 */
	void validate_hunl_infoset_encoding(const HUNLInfosetEncoding& encoding);
	/** @brief Encodes a validated information set as a stable text key.
	 * @throws std::invalid_argument If the encoding is invalid.
	 */
	std::string hunl_infoset_key(const HUNLInfosetEncoding& encoding);

	/**
	 * @brief Complete no-limit hold'em configuration.
	 *
	 * @throws std::invalid_argument from validate() when a setting or initial state is invalid.
	 */
	struct HUNLConfig
	{
		int starting_stack = 10'000; ///< Starting chips per player.
		int small_blind = 50; ///< Small-blind amount.
		int big_blind = 100; ///< Big-blind amount and solver chip unit reference.
		int ante = 0; ///< Ante posted by each player.
		Street starting_street = Street::Preflop; ///< Street at which the state begins.
		std::vector<std::uint8_t> initial_board; ///< Known board prefix for a postflop start.
		int initial_pot = 0; ///< Chips already in the pot at the starting state.
		std::array<int, 2> initial_contributions = { 0, 0 }; ///< Starting contributions by player.
		std::optional<std::array<std::array<std::uint8_t, 2>, 2>> initial_hole_cards = std::nullopt; ///< Explicit private cards by player, when solving a fixed deal.
		std::uint8_t preflop_raise_cap = 4; ///< Maximum raises on the preflop street.
		std::uint8_t postflop_raise_cap = 3; ///< Maximum raises on each postflop street.
		std::vector<double> bet_size_fractions = { 0.33, 0.75, 1.00, 1.50, 2.00 }; ///< Default pot-relative opening-bet sizes.
		std::optional<std::vector<double>> flop_bet_fractions = std::nullopt; ///< Optional flop-specific bet menu.
		std::optional<std::vector<double>> turn_bet_fractions = std::nullopt; ///< Optional turn-specific bet menu.
		std::optional<std::vector<double>> river_bet_fractions = std::nullopt; ///< Optional river-specific bet menu.
		std::vector<double> raise_size_xs = { 3.0 }; ///< Multipliers used to form raise-to candidates.
		bool include_all_in = true; ///< Whether the action menu can include an all-in action.
		std::optional<double> auto_all_in_spr_threshold = std::nullopt; ///< Optional SPR threshold that adds an all-in action.
		int force_allin_threshold = 1; ///< Remaining-stack threshold for forcing an all-in action.
		int min_bet_bb = 1; ///< Minimum opening bet in big blinds.
		bool allow_oop_flop_lead = false; ///< Whether the out-of-position player may lead first on the flop.
		double rake_rate = 0.0; ///< Fraction of contested pot taken as rake.
		int rake_cap = 0; ///< Maximum rake in chips; zero disables the cap.
		std::array<std::uint16_t, 3> bucket_counts_by_street = { 0, 0, 0 }; ///< Bucket counts for flop, turn and river.
		std::optional<std::string> abstraction_path = std::nullopt; ///< Optional abstraction artifact path.
		std::optional<std::string> abstraction_version = std::nullopt; ///< Optional abstraction version identifier.
		std::uint32_t depth_limit_plies = 0; ///< Maximum tree depth; zero means unlimited.
		HUNLFlatSolveMode flat_solve_mode = HUNLFlatSolveMode::Auto; ///< Flat solver's private-hand mode.
		// Range/bucket solving is a separate solve contract from explicit-hand solving.
		// The current postflop solver rejects this contract until its joint-combo reach
		// and terminal-value pipeline is implemented.
		HUNLRangePolicy range_policy = HUNLRangePolicy::Unspecified; ///< Range interpretation requested by the caller.
		std::array<std::optional<HUNLRangeInput>, 2> initial_ranges = { std::nullopt, std::nullopt }; ///< Optional starting range per player.
		/** @brief Validates chip settings, street state, action menus and range inputs.
		 * @throws std::invalid_argument If any configuration field is inconsistent or out of range.
		 */
		void validate() const;
	};

	/** @brief Resolves an unspecified range policy from the configured ranges.
	 * @param config Configuration to inspect.
	 */
	HUNLRangePolicy resolve_range_policy(const HUNLConfig& config);
	/** @brief Returns the configured abstraction bucket count for a street. */
	std::size_t configured_bucket_count(const HUNLConfig& config, Street street);

	/** @brief Runtime betting data and action-menu settings for one action enumeration. */
	struct ActionContext
	{
		int pot = 0; ///< Current pot size in chips.
		int to_call = 0; ///< Additional chips the acting player must match to call.
		std::array<int, 2> stacks = { 0, 0 }; ///< Remaining stacks by player.
		std::array<int, 2> contributions = { 0, 0 }; ///< Contributions by player on this street.
		std::uint8_t cur_player = 0; ///< Acting player index.
		Street street = Street::Preflop; ///< Current street.
		std::uint8_t street_num_raises = 0; ///< Raises already made on this street.
		PlayerId street_aggressor = -1; ///< Most recent aggressor, or -1 when none.
		int big_blind = 100; ///< Big-blind size used to scale configured thresholds.
		std::vector<double> bet_size_fractions = { 0.33, 0.75, 1.00, 1.50, 2.00 }; ///< Default pot-relative bet sizes.
		std::optional<std::vector<double>> flop_bet_fractions = std::nullopt; ///< Optional flop-specific bet sizes.
		std::optional<std::vector<double>> turn_bet_fractions = std::nullopt; ///< Optional turn-specific bet sizes.
		std::optional<std::vector<double>> river_bet_fractions = std::nullopt; ///< Optional river-specific bet sizes.
		std::vector<double> raise_size_xs = { 3.0 }; ///< Multipliers used to form raise-to candidates.
		std::uint8_t preflop_raise_cap = 4; ///< Maximum preflop raises.
		std::uint8_t postflop_raise_cap = 3; ///< Maximum raises on each postflop street.
		int force_allin_threshold = 1; ///< Remaining-stack threshold for forcing an all-in action.
		int min_bet_bb = 1; ///< Minimum opening bet, in big blinds.
		bool include_all_in = true; ///< Whether an all-in candidate is allowed.
		std::optional<double> auto_all_in_spr_threshold = std::nullopt; ///< SPR threshold for automatic all-in inclusion.
		bool allow_oop_flop_lead = false; ///< Whether the out-of-position player may lead first on the flop.
		std::uint32_t street_action_count = 0; ///< Number of actions already taken on this street.
	};

	/** @brief Hold'em game state used by the solver pipeline. */
	struct HUNLState
	{
		std::optional<std::array<std::array<std::uint8_t, 2>, 2>> hole_cards = std::nullopt; ///< Private cards by player, absent while undealt.
		std::vector<std::uint8_t> board; ///< Community cards dealt so far.
		Street street = Street::Preflop; ///< Current betting street.
		std::array<int, 2> contributions = { 0, 0 }; ///< Current-street contributions by player.
		std::array<int, 2> stacks = { 0, 0 }; ///< Chips remaining by player.
		std::vector<ActionId> street_history; ///< Action identifiers on the current street.
		PlayerId street_aggressor = -1; ///< Last player to aggress on the current street, or -1.
		std::uint8_t street_num_raises = 0; ///< Raises made on the current street.
		int to_call = 0; ///< Chips required from the actor to call.
		PlayerId cur_player = -1; ///< Acting player, or the chance sentinel when dealing.
		std::array<bool, 2> folded = { false, false }; ///< Fold status by player.
		std::array<bool, 2> all_in = { false, false }; ///< All-in status by player.
		std::shared_ptr<const HUNLConfig> config; ///< Immutable settings governing this state.
		std::vector<std::vector<std::string>> betting_tokens; ///< Completed-street tokens for display/key formatting.
		std::vector<std::string> current_street_tokens; ///< Current-street tokens for display/key formatting.
		std::vector<std::vector<int>> betting_history_codes; ///< Encoded action history for completed streets.
		std::vector<int> current_street_history_codes; ///< Encoded actions on the current street.
		std::uint8_t pending_board_deals = 0; ///< Remaining chance cards before the next betting street.

		/** @brief Creates the default initial game state. */
		static HUNLState initial();
		/** @brief Creates an initial state from configuration, including postflop starts.
		 * @param config Immutable game configuration.
		 * @throws std::invalid_argument If config is null or invalid.
		 */
		static HUNLState initial(std::shared_ptr<const HUNLConfig> config);

		/** @brief Copies the state and attaches an explicit two-player deal.
		 * @throws std::invalid_argument If cards are invalid, duplicated or conflict with the board.
		 */
		HUNLState clone_with_hole_cards(const std::array<std::array<std::uint8_t, 2>, 2>& hole) const;
		/** @brief Builds the action-menu context from this state and its configuration. */
		ActionContext action_context() const;
		/** @brief Reports whether betting or showdown has reached a terminal state. */
		bool is_terminal() const;
		/** @brief Returns chip utility for both players at a terminal state.
		 * @throws std::logic_error If config is absent or showdown lacks dealt hands or a complete board.
		 */
		std::vector<Value> utility() const;
		/** @brief Returns the acting player, or -1 when chance acts. */
		PlayerId current_player() const;
		/** @brief Returns equally likely legal card deals for the current chance node.
		 * @throws std::invalid_argument If the stored board or hole cards are invalid.
		 */
		std::vector<ChanceOutcome> chance_outcomes() const;
		/** @brief Returns legal player actions, or an empty list outside a decision node. */
		std::vector<ActionId> legal_actions() const;
		/** @brief Applies either a chance card or a legal player action.
		 * @throws std::invalid_argument If action is not available in the current state.
		 */
		HUNLState apply(ActionId action) const;
		/** @brief Applies one legal action and returns the resulting state.
		 * @throws std::invalid_argument If action is unavailable in this state.
		 */
		HUNLState next_state(ActionId action) const;
		/** @brief Encodes the acting player's private and public information. */
		HUNLInfosetEncoding infoset_encoding(PlayerId player) const;
		/** @brief Returns the unabstracted stable information-set key for a player. */
		std::string infoset_key(PlayerId player) const;
		/** @brief Returns a key using the supplied abstraction tables when applicable. */
		std::string infoset_key(PlayerId player, const texas::util::AbstractionTables* abstraction) const;
		/** @brief Formats the recorded betting tokens as a readable history. */
		std::string format_history() const;

	private:
		static HUNLState initial_preflop(std::shared_ptr<const HUNLConfig> config);
		HUNLState apply_chance(std::uint8_t card) const;
		HUNLState after_board_dealt(HUNLState state) const;
		HUNLState apply_player(ActionId action) const;
		bool street_complete(ActionId action, const HUNLState& new_state) const;
		HUNLState begin_street_transition(HUNLState state) const;
	};

	/** @brief Reports whether the context is on the preflop street. */
	bool is_preflop(const ActionContext& ctx);
	/** @brief Returns the active opening-bet menu for the current street. */
	const std::vector<double>& bet_menu(const ActionContext& ctx);
	/** @brief Returns raise multipliers allowed by the context and raise cap. */
	std::vector<double> raise_menu(const ActionContext& ctx);
	/** @brief Reports whether this is the out-of-position player's first flop action. */
	bool is_oop_flop_first_action(const ActionContext& ctx);
	/** @brief Computes effective remaining stack divided by pot size. */
	double stack_to_pot_ratio(const ActionContext& ctx);
	/** @brief Reports whether the context permits adding an all-in action. */
	bool should_include_all_in(const ActionContext& ctx);
	/** @brief Returns the raise cap for the current street. */
	std::uint8_t raise_cap(const ActionContext& ctx);
	/** @brief Returns the minimum opening bet in chips. */
	int min_bet(const ActionContext& ctx);
	/** @brief Returns the remaining-stack threshold used to force all-in. */
	int force_allin_chip_threshold(const ActionContext& ctx);
	/** @brief Returns the acting player's remaining stack. */
	int stack_remaining(const ActionContext& ctx);
	/** @brief Returns the minimum legal raise increment in chips. */
	int min_raise_increment(const ActionContext& ctx);
	/** @brief Rounds a finite non-negative value using half-to-even rounding.
	 * @throws std::invalid_argument If value is negative, non-finite or outside int range.
	 */
	int python_round_positive(double value);
	/** @brief Converts a pot fraction into an opening-bet chip amount. */
	int bet_amount_for_fraction(const ActionContext& ctx, double fraction);
	/** @brief Converts a raise multiplier into a raise-to chip amount. */
	int raise_to_for_multiplier(const ActionContext& ctx, double multiplier);
	/** @brief Maps a bet action identifier to its chip amount. */
	int compute_bet_amount(std::uint8_t action_id, const ActionContext& ctx);
	/** @brief Maps a raise action identifier to its raise-to amount. */
	int compute_raise_to(std::uint8_t action_id, const ActionContext& ctx);
	/** @brief Enumerates opening-bet actions available in the context. */
	std::vector<ActionId> enumerate_bets(const ActionContext& ctx);
	/** @brief Enumerates raise actions available in the context. */
	std::vector<ActionId> enumerate_raises(const ActionContext& ctx);
	/** @brief Enumerates every legal player action in the context. */
	std::vector<ActionId> enumerate_legal_actions(const ActionContext& ctx);
	/** @brief Returns a small preflop configuration suitable for examples and tests. */
	HUNLConfig default_tiny_subgame();
	/** @brief Returns the benchmark turn-subgame configuration. */
	HUNLConfig benchmark_turn_subgame();
	/** @brief Returns a conservative flop real-time-analysis configuration. */
	HUNLConfig rta_flop_conservative();
	/** @brief Returns a balanced flop real-time-analysis configuration. */
	HUNLConfig rta_flop_balanced();

} // namespace texas::games::hunl
