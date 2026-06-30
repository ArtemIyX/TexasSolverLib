#pragma once

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace core {

struct AbstractionTables;
inline constexpr std::size_t HUNL_MAX_HISTORY_CODES = 48;

/**
 * @brief Street in the Hold'em game tree.
 */
enum class Street : std::uint8_t {
    Preflop = 0,
    Flop = 1,
    Turn = 2,
    River = 3,
    Showdown = 4,
};

enum class HUNLFlatSolveMode : std::uint8_t {
    Auto = 0,
    ExplicitHand = 1,
    Bucketed = 2,
};

enum class HUNLRangePolicy : std::uint8_t {
    Unspecified = 0,
    Uniform = 1,
    UseInitialRanges = 2,
    RequireExplicit = 3,
};

struct HUNLWeightedHand {
    std::array<std::uint8_t, 2> hole = {0, 0};
    double weight = 0.0;
};

struct HUNLWeightedBucket {
    Street street = Street::Preflop;
    std::uint32_t bucket = 0;
    double weight = 0.0;
};

struct HUNLRangeInput {
    std::vector<HUNLWeightedHand> hand_weights;
    std::vector<HUNLWeightedBucket> bucket_weights;
};

std::optional<Street> street_from_u8(std::uint8_t value);
const char* street_token(Street street);
std::uint8_t cards_to_deal(Street street);

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

bool is_opening_bet(ActionId action);
bool is_raise(ActionId action);

constexpr std::uint8_t card_to_int(std::uint8_t rank, std::uint8_t suit) {
    return static_cast<std::uint8_t>(rank * 4 + suit);
}

std::uint8_t rank_of(std::uint8_t card);
std::uint8_t suit_of(std::uint8_t card);
std::string card_to_string(std::uint8_t card);
std::string sorted_card_string(const std::vector<std::uint8_t>& cards);

struct HUNLInfosetEncoding {
    std::array<std::uint8_t, 2> hole = {0, 0};
    std::array<std::uint8_t, 5> board = {0, 0, 0, 0, 0};
    std::array<std::uint8_t, 4> street_lengths = {0, 0, 0, 0};
    std::array<int, HUNL_MAX_HISTORY_CODES> history_codes = {};
    std::uint8_t board_count = 0;
    Street street = Street::Preflop;
    std::uint8_t history_count = 0;

    bool operator==(const HUNLInfosetEncoding& other) const noexcept {
        return hole == other.hole &&
               board == other.board &&
               street_lengths == other.street_lengths &&
               history_codes == other.history_codes &&
               board_count == other.board_count &&
               street == other.street &&
               history_count == other.history_count;
    }
};

struct HUNLInfosetEncodingHash {
    std::size_t operator()(const HUNLInfosetEncoding& encoding) const noexcept;
};

std::string hunl_infoset_key(const HUNLInfosetEncoding& encoding);

/**
 * @brief Complete no-limit hold'em configuration.
 */
struct HUNLConfig {
    int starting_stack = 10'000;
    int small_blind = 50;
    int big_blind = 100;
    int ante = 0;
    Street starting_street = Street::Preflop;
    std::vector<std::uint8_t> initial_board;
    int initial_pot = 0;
    std::array<int, 2> initial_contributions = {0, 0};
    std::optional<std::array<std::array<std::uint8_t, 2>, 2>> initial_hole_cards = std::nullopt;
    std::uint8_t preflop_raise_cap = 4;
    std::uint8_t postflop_raise_cap = 3;
    std::vector<double> bet_size_fractions = {0.33, 0.75, 1.00, 1.50, 2.00};
    std::optional<std::vector<double>> flop_bet_fractions = std::nullopt;
    std::optional<std::vector<double>> turn_bet_fractions = std::nullopt;
    std::optional<std::vector<double>> river_bet_fractions = std::nullopt;
    std::vector<double> raise_size_xs = {3.0};
    bool include_all_in = true;
    int force_allin_threshold = 1;
    int min_bet_bb = 1;
    double rake_rate = 0.0;
    int rake_cap = 0;
    std::optional<std::string> abstraction_path = std::nullopt;
    std::optional<std::string> abstraction_version = std::nullopt;
    HUNLFlatSolveMode flat_solve_mode = HUNLFlatSolveMode::Auto;
    HUNLRangePolicy range_policy = HUNLRangePolicy::Unspecified;
    std::array<std::optional<HUNLRangeInput>, 2> initial_ranges = {std::nullopt, std::nullopt};
    std::array<std::optional<HUNLRangeInput>, 2> player_ranges = {std::nullopt, std::nullopt};
    bool use_pcs = false;

    void validate() const;
};

HUNLRangePolicy resolve_range_policy(const HUNLConfig& config);

/**
 * @brief Runtime context for action enumeration.
 */
struct ActionContext {
    int pot = 0;
    int to_call = 0;
    std::array<int, 2> stacks = {0, 0};
    std::array<int, 2> contributions = {0, 0};
    std::uint8_t cur_player = 0;
    Street street = Street::Preflop;
    std::uint8_t street_num_raises = 0;
    PlayerId street_aggressor = -1;
    int big_blind = 100;
    std::vector<double> bet_size_fractions = {0.33, 0.75, 1.00, 1.50, 2.00};
    std::optional<std::vector<double>> flop_bet_fractions = std::nullopt;
    std::optional<std::vector<double>> turn_bet_fractions = std::nullopt;
    std::optional<std::vector<double>> river_bet_fractions = std::nullopt;
    std::vector<double> raise_size_xs = {3.0};
    std::uint8_t preflop_raise_cap = 4;
    std::uint8_t postflop_raise_cap = 3;
    int force_allin_threshold = 1;
    int min_bet_bb = 1;
    bool include_all_in = true;
    std::uint32_t street_action_count = 0;
};

/**
 * @brief Hold'em game state used by the solver pipeline.
 */
struct HUNLState {
    std::optional<std::array<std::array<std::uint8_t, 2>, 2>> hole_cards = std::nullopt;
    std::vector<std::uint8_t> board;
    Street street = Street::Preflop;
    std::array<int, 2> contributions = {0, 0};
    std::array<int, 2> stacks = {0, 0};
    std::vector<ActionId> street_history;
    PlayerId street_aggressor = -1;
    std::uint8_t street_num_raises = 0;
    int to_call = 0;
    PlayerId cur_player = -1;
    std::array<bool, 2> folded = {false, false};
    std::array<bool, 2> all_in = {false, false};
    std::shared_ptr<const HUNLConfig> config;
    std::vector<std::vector<std::string>> betting_tokens;
    std::vector<std::string> current_street_tokens;
    std::vector<std::vector<int>> betting_history_codes;
    std::vector<int> current_street_history_codes;
    std::uint8_t pending_board_deals = 0;

    static HUNLState initial();
    static HUNLState initial(std::shared_ptr<const HUNLConfig> config);

    HUNLState clone_with_hole_cards(const std::array<std::array<std::uint8_t, 2>, 2>& hole) const;
    ActionContext action_context() const;
    bool is_terminal() const;
    std::vector<Value> utility() const;
    PlayerId current_player() const;
    std::vector<ChanceOutcome> chance_outcomes() const;
    std::vector<ActionId> legal_actions() const;
    HUNLState apply(ActionId action) const;
    HUNLState next_state(ActionId action) const;
    HUNLInfosetEncoding infoset_encoding(PlayerId player) const;
    std::string infoset_key(PlayerId player) const;
    std::string infoset_key(PlayerId player, const AbstractionTables* abstraction) const;
    std::string format_history() const;

private:
    static HUNLState initial_preflop(std::shared_ptr<const HUNLConfig> config);
    HUNLState apply_chance(std::uint8_t card) const;
    HUNLState after_board_dealt(HUNLState state) const;
    HUNLState apply_player(ActionId action) const;
    bool street_complete(ActionId action, const HUNLState& new_state) const;
    HUNLState begin_street_transition(HUNLState state) const;
};

bool is_preflop(const ActionContext& ctx);
const std::vector<double>& bet_menu(const ActionContext& ctx);
std::vector<double> raise_menu(const ActionContext& ctx);
bool is_oop_flop_first_action(const ActionContext& ctx);
std::uint8_t raise_cap(const ActionContext& ctx);
int min_bet(const ActionContext& ctx);
int force_allin_chip_threshold(const ActionContext& ctx);
int stack_remaining(const ActionContext& ctx);
int min_raise_increment(const ActionContext& ctx);
int python_round_positive(double value);
int bet_amount_for_fraction(const ActionContext& ctx, double fraction);
int raise_to_for_multiplier(const ActionContext& ctx, double multiplier);
int compute_bet_amount(std::uint8_t action_id, const ActionContext& ctx);
int compute_raise_to(std::uint8_t action_id, const ActionContext& ctx);
std::vector<ActionId> enumerate_bets(const ActionContext& ctx);
std::vector<ActionId> enumerate_raises(const ActionContext& ctx);
std::vector<ActionId> enumerate_legal_actions(const ActionContext& ctx);
HUNLConfig default_tiny_subgame();
HUNLConfig benchmark_turn_subgame();

}  // namespace core


