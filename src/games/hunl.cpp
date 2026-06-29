#include "games/hunl.hpp"

#include "util/abstraction.hpp"
#include "games/hunl_eval.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace core {

namespace {

constexpr std::array<ActionId, 5> BET_ACTION_IDS = {
    ACTION_BET_33, ACTION_BET_75, ACTION_BET_100, ACTION_BET_150, ACTION_BET_200};
constexpr std::array<ActionId, 5> RAISE_ACTION_IDS = {
    ACTION_RAISE_33, ACTION_RAISE_75, ACTION_RAISE_100, ACTION_RAISE_150, ACTION_RAISE_200};
constexpr std::uint8_t OOP_PLAYER = 1;

}

std::optional<Street> street_from_u8(std::uint8_t value) {
    switch (value) {
        case 0:
            return Street::Preflop;
        case 1:
            return Street::Flop;
        case 2:
            return Street::Turn;
        case 3:
            return Street::River;
        case 4:
            return Street::Showdown;
        default:
            return std::nullopt;
    }
}

const char* street_token(Street street) {
    switch (street) {
        case Street::Preflop:
            return "p";
        case Street::Flop:
            return "f";
        case Street::Turn:
            return "t";
        case Street::River:
            return "r";
        case Street::Showdown:
            return "s";
    }
    throw std::logic_error("invalid Street");
}

std::uint8_t cards_to_deal(Street street) {
    switch (street) {
        case Street::Flop:
            return 3;
        case Street::Turn:
        case Street::River:
            return 1;
        default:
            return 0;
    }
}

bool is_opening_bet(ActionId action) {
    return std::find(BET_ACTION_IDS.begin(), BET_ACTION_IDS.end(), action) != BET_ACTION_IDS.end();
}

bool is_raise(ActionId action) {
    return std::find(RAISE_ACTION_IDS.begin(), RAISE_ACTION_IDS.end(), action) != RAISE_ACTION_IDS.end();
}

std::uint8_t rank_of(std::uint8_t card) {
    return static_cast<std::uint8_t>(card >> 2);
}

std::uint8_t suit_of(std::uint8_t card) {
    return static_cast<std::uint8_t>(card & 3U);
}

std::string card_to_string(std::uint8_t card) {
    static constexpr char RANKS[] = "23456789TJQKA";
    static constexpr char SUITS[] = "shdc";

    const auto rank = rank_of(card);
    const auto suit = suit_of(card);
    if (rank < 2 || rank > 14 || suit > 3) {
        throw std::invalid_argument("card_to_string received invalid encoded card");
    }

    std::string out;
    out.push_back(RANKS[rank - 2]);
    out.push_back(SUITS[suit]);
    return out;
}

std::string sorted_card_string(const std::vector<std::uint8_t>& cards) {
    std::vector<std::uint8_t> sorted = cards;
    std::sort(sorted.begin(), sorted.end());
    std::string out;
    out.reserve(sorted.size() * 2);
    for (const auto card : sorted) {
        out += card_to_string(card);
    }
    return out;
}

void HUNLConfig::validate() const {
    if (rake_rate != 0.0) {
        throw std::invalid_argument("HUNLConfig.validate: rake_rate must be 0.0");
    }
    if (rake_cap != 0) {
        throw std::invalid_argument("HUNLConfig.validate: rake_cap must be 0");
    }
    if (starting_stack <= 0) {
        throw std::invalid_argument("HUNLConfig.validate: starting_stack must be > 0");
    }
    if (big_blind <= 0) {
        throw std::invalid_argument("HUNLConfig.validate: big_blind must be > 0");
    }
    if (small_blind < 0 || ante < 0 || initial_pot < 0) {
        throw std::invalid_argument(
            "HUNLConfig.validate: small_blind, ante, initial_pot must be non-negative");
    }

    const auto c0 = initial_contributions[0];
    const auto c1 = initial_contributions[1];
    if (starting_street == Street::Preflop) {
        if (c0 == 0 && c1 == 0 && initial_pot == 0) {
            return;
        }

        const auto blind_sb = small_blind + ante;
        const auto blind_bb = big_blind + ante;
        const auto expected_pot = blind_sb + blind_bb;
        if (c0 == blind_sb && c1 == blind_bb && initial_pot == expected_pot) {
            return;
        }

        throw std::invalid_argument(
            "HUNLConfig.validate: invalid preflop initial_contributions / initial_pot combination");
    }

    if (initial_board.empty()) {
        throw std::invalid_argument(
            "HUNLConfig.validate: initial_board must be non-empty when starting_street > Preflop");
    }
    if (c0 < 0 || c1 < 0) {
        throw std::invalid_argument("HUNLConfig.validate: initial_contributions must be non-negative");
    }
    if (c0 > starting_stack || c1 > starting_stack) {
        throw std::invalid_argument(
            "HUNLConfig.validate: initial_contributions must not exceed starting_stack");
    }
    const auto contribution_sum = c0 + c1;
    if (contribution_sum != 0 && contribution_sum != initial_pot) {
        throw std::invalid_argument(
            "HUNLConfig.validate: initial_contributions must sum to initial_pot or both be zero");
    }
}

HUNLState HUNLState::initial(std::shared_ptr<const HUNLConfig> cfg) {
    if (!cfg) {
        throw std::invalid_argument("HUNLState::initial requires a non-null config");
    }
    cfg->validate();

    if (cfg->starting_street == Street::Preflop) {
        return initial_preflop(std::move(cfg));
    }

    const auto contributions = cfg->initial_contributions;
    const std::array<int, 2> stacks = {cfg->starting_stack, cfg->starting_stack};
    const std::array<bool, 2> all_in = {stacks[0] == 0, stacks[1] == 0};
    const auto hole = cfg->initial_hole_cards;
    const auto c0 = contributions[0];
    const auto c1 = contributions[1];

    int to_call = 0;
    PlayerId aggressor = -1;
    PlayerId first_actor = 1;
    if (c0 < c1) {
        to_call = c1 - c0;
        aggressor = 1;
        first_actor = 0;
    } else if (c1 < c0) {
        to_call = c0 - c1;
        aggressor = 0;
        first_actor = 1;
    }

    HUNLState state;
    state.hole_cards = hole;
    state.board = cfg->initial_board;
    state.street = cfg->starting_street;
    state.contributions = contributions;
    state.stacks = stacks;
    state.street_aggressor = aggressor;
    state.street_num_raises = to_call > 0 ? 1 : 0;
    state.to_call = to_call;
    state.cur_player = (all_in[0] || all_in[1] || !hole.has_value()) ? -1 : first_actor;
    state.folded = {false, false};
    state.all_in = all_in;
    state.config = std::move(cfg);
    return state;
}

HUNLState HUNLState::initial() {
    return initial(std::make_shared<const HUNLConfig>(default_tiny_subgame()));
}

HUNLState HUNLState::initial_preflop(std::shared_ptr<const HUNLConfig> cfg) {
    const auto blind_sb = cfg->small_blind + cfg->ante;
    const auto blind_bb = cfg->big_blind + cfg->ante;
    const auto sb_contrib = std::max(blind_sb, cfg->initial_contributions[0]);
    const auto bb_contrib = std::max(blind_bb, cfg->initial_contributions[1]);

    HUNLState state;
    state.hole_cards = cfg->initial_hole_cards;
    state.board = {};
    state.street = Street::Preflop;
    state.contributions = {sb_contrib, bb_contrib};
    state.stacks = {cfg->starting_stack - sb_contrib, cfg->starting_stack - bb_contrib};
    state.street_aggressor = 1;
    state.street_num_raises = 1;
    state.to_call = bb_contrib - sb_contrib;
    state.cur_player = state.hole_cards.has_value() ? 0 : -1;
    state.folded = {false, false};
    state.all_in = {state.stacks[0] == 0, state.stacks[1] == 0};
    state.config = std::move(cfg);
    return state;
}

HUNLState HUNLState::clone_with_hole_cards(
    const std::array<std::array<std::uint8_t, 2>, 2>& hole) const {
    PlayerId next_cur = 1;
    if (street == Street::Preflop || contributions[0] < contributions[1]) {
        next_cur = 0;
    }
    HUNLState next = *this;
    next.hole_cards = hole;
    next.cur_player = next_cur;
    return next;
}

ActionContext HUNLState::action_context() const {
    if (!config) {
        throw std::logic_error("HUNLState.action_context requires config");
    }
    const auto& cfg = *config;
    const auto pot = contributions[0] + contributions[1] + cfg.initial_pot -
                     cfg.initial_contributions[0] - cfg.initial_contributions[1];
    ActionContext ctx;
    ctx.pot = pot;
    ctx.to_call = to_call;
    ctx.stacks = stacks;
    ctx.contributions = contributions;
    ctx.cur_player = static_cast<std::uint8_t>(std::max(cur_player, 0));
    ctx.street = street;
    ctx.street_num_raises = street_num_raises;
    ctx.street_aggressor = street_aggressor;
    ctx.big_blind = cfg.big_blind;
    ctx.bet_size_fractions = cfg.bet_size_fractions;
    ctx.flop_bet_fractions = cfg.flop_bet_fractions;
    ctx.turn_bet_fractions = cfg.turn_bet_fractions;
    ctx.river_bet_fractions = cfg.river_bet_fractions;
    ctx.raise_size_xs = cfg.raise_size_xs;
    ctx.preflop_raise_cap = cfg.preflop_raise_cap;
    ctx.postflop_raise_cap = cfg.postflop_raise_cap;
    ctx.force_allin_threshold = cfg.force_allin_threshold;
    ctx.min_bet_bb = cfg.min_bet_bb;
    ctx.include_all_in = cfg.include_all_in;
    ctx.street_action_count = static_cast<std::uint32_t>(current_street_tokens.size());
    return ctx;
}

bool HUNLState::is_terminal() const {
    return folded[0] || folded[1] || street == Street::Showdown;
}

std::vector<Value> HUNLState::utility() const {
    if (!config) {
        throw std::logic_error("HUNLState.utility requires config");
    }
    const auto& cfg = *config;
    const auto bb = static_cast<double>(cfg.big_blind);
    const auto init_c0 = static_cast<double>(cfg.initial_contributions[0]);
    const auto init_c1 = static_cast<double>(cfg.initial_contributions[1]);
    const auto cs0 = static_cast<double>(contributions[0]) - init_c0;
    const auto cs1 = static_cast<double>(contributions[1]) - init_c1;
    const auto pot_total = static_cast<double>(cfg.initial_pot) + cs0 + cs1;

    if (folded[0]) {
        return {-cs0 / bb, (pot_total - cs1) / bb};
    }
    if (folded[1]) {
        return {(pot_total - cs0) / bb, -cs1 / bb};
    }

    if (!hole_cards.has_value() || board.size() < 5) {
        throw std::logic_error("showdown requires dealt hole cards and 5-card board");
    }

    std::array<std::uint8_t, 7> seven0 = {};
    std::array<std::uint8_t, 7> seven1 = {};
    seven0[0] = (*hole_cards)[0][0];
    seven0[1] = (*hole_cards)[0][1];
    seven1[0] = (*hole_cards)[1][0];
    seven1[1] = (*hole_cards)[1][1];
    for (std::size_t i = 0; i < 5; ++i) {
        seven0[i + 2] = board[i];
        seven1[i + 2] = board[i];
    }

    const auto s0 = Strength::evaluate_7(seven0);
    const auto s1 = Strength::evaluate_7(seven1);
    if (s0 > s1) {
        return {(pot_total - cs0) / bb, -cs1 / bb};
    }
    if (s1 > s0) {
        return {-cs0 / bb, (pot_total - cs1) / bb};
    }
    return {(pot_total / 2.0 - cs0) / bb, (pot_total / 2.0 - cs1) / bb};
}

PlayerId HUNLState::current_player() const {
    return is_terminal() ? -1 : cur_player;
}

std::vector<ChanceOutcome> HUNLState::chance_outcomes() const {
    if (cur_player != -1 || is_terminal() || !hole_cards.has_value()) {
        return {};
    }

    std::array<bool, 64> held = {};
    for (const auto c : {(*hole_cards)[0][0], (*hole_cards)[0][1], (*hole_cards)[1][0], (*hole_cards)[1][1]}) {
        held[c] = true;
    }
    for (const auto c : board) {
        held[c] = true;
    }

    std::vector<std::uint8_t> remaining;
    for (std::uint8_t r = 2; r <= 14; ++r) {
        for (std::uint8_t s = 0; s < 4; ++s) {
            const auto c = card_to_int(r, s);
            if (!held[c]) {
                remaining.push_back(c);
            }
        }
    }
    if (remaining.empty()) {
        return {};
    }

    const auto p = 1.0 / static_cast<double>(remaining.size());
    std::vector<ChanceOutcome> out;
    out.reserve(remaining.size());
    for (const auto c : remaining) {
        out.push_back({c, p});
    }
    return out;
}

std::vector<ActionId> HUNLState::legal_actions() const {
    if (is_terminal() || cur_player == -1) {
        return {};
    }
    return enumerate_legal_actions(action_context());
}

HUNLState HUNLState::apply(ActionId action) const {
    return cur_player == -1 ? apply_chance(static_cast<std::uint8_t>(action)) : apply_player(action);
}

HUNLState HUNLState::next_state(ActionId action) const {
    return apply(action);
}

std::string HUNLState::infoset_key(PlayerId player) const {
    return infoset_key(player, nullptr);
}

std::string HUNLState::infoset_key(PlayerId player, const AbstractionTables* abstraction) const {
    const auto player_idx = static_cast<std::size_t>(player);
    if (abstraction && street >= Street::Flop && hole_cards.has_value()) {
        const auto bucket = lookup_bucket(*abstraction, board, (*hole_cards)[player_idx], street);
        return "b" + std::to_string(bucket) + "|" + street_token(street) + "|" + format_history();
    }
    const auto hole = hole_cards.has_value() ? sorted_card_string(
        std::vector<std::uint8_t>{(*hole_cards)[player_idx][0], (*hole_cards)[player_idx][1]}) : std::string();
    const auto board_str = sorted_card_string(board);
    return hole + "|" + board_str + "|" + street_token(street) + "|" + format_history();
}

std::string HUNLState::format_history() const {
    std::vector<std::string> parts;
    parts.reserve(betting_tokens.size() + 1);
    for (const auto& street_tokens : betting_tokens) {
        std::string joined;
        for (const auto& token : street_tokens) {
            joined += token;
        }
        parts.push_back(std::move(joined));
    }
    std::string current;
    for (const auto& token : current_street_tokens) {
        current += token;
    }
    parts.push_back(std::move(current));

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += "/";
        }
        out += parts[i];
    }
    return out;
}

HUNLState HUNLState::apply_chance(std::uint8_t card) const {
    HUNLState next = *this;
    next.board.push_back(card);
    next.pending_board_deals = next.pending_board_deals > 0 ? next.pending_board_deals - 1 : 0;
    if (next.pending_board_deals > 0) {
        return next;
    }
    return after_board_dealt(std::move(next));
}

HUNLState HUNLState::after_board_dealt(HUNLState state) const {
    if (state.all_in[0] || state.all_in[1]) {
        if (state.board.size() >= 5) {
            state.street = Street::Showdown;
            state.cur_player = -1;
            return state;
        }
        state.cur_player = -1;
        state.pending_board_deals = 1;
        return state;
    }
    state.cur_player = 1;
    return state;
}

HUNLState HUNLState::apply_player(ActionId action) const {
    const auto ctx = action_context();
    const auto player = static_cast<std::size_t>(cur_player);
    auto contributions_next = contributions;
    auto stacks_next = stacks;
    auto folded_next = folded;
    auto all_in_next = all_in;
    auto street_aggressor_next = street_aggressor;
    auto street_num_raises_next = street_num_raises;
    auto to_call_next = to_call;
    std::string token;

    if (action == ACTION_FOLD) {
        folded_next[player] = true;
        token = "f";
    } else if (action == ACTION_CHECK) {
        token = "x";
    } else if (action == ACTION_CALL) {
        const auto pay = std::min(to_call, stacks_next[player]);
        contributions_next[player] += pay;
        stacks_next[player] -= pay;
        if (stacks_next[player] == 0) {
            all_in_next[player] = true;
        }
        to_call_next = 0;
        token = "c";
    } else if (action == ACTION_ALL_IN) {
        const auto pay = stacks_next[player];
        contributions_next[player] += pay;
        stacks_next[player] = 0;
        all_in_next[player] = true;
        const auto opp = 1U - player;
        to_call_next = std::max(contributions_next[player] - contributions_next[opp], 0);
        street_aggressor_next = static_cast<PlayerId>(player);
        ++street_num_raises_next;
        token = "A";
    } else if (is_opening_bet(action)) {
        const auto amount = compute_bet_amount(static_cast<std::uint8_t>(action), ctx);
        contributions_next[player] += amount;
        stacks_next[player] -= amount;
        if (stacks_next[player] == 0) {
            all_in_next[player] = true;
        }
        const auto opp = 1U - player;
        to_call_next = contributions_next[player] - contributions_next[opp];
        street_aggressor_next = static_cast<PlayerId>(player);
        ++street_num_raises_next;
        token = "b" + std::to_string(amount);
    } else if (is_raise(action)) {
        const auto new_contrib = compute_raise_to(static_cast<std::uint8_t>(action), ctx);
        const auto pay = new_contrib - contributions_next[player];
        contributions_next[player] = new_contrib;
        stacks_next[player] -= pay;
        if (stacks_next[player] == 0) {
            all_in_next[player] = true;
        }
        const auto opp = 1U - player;
        to_call_next = contributions_next[player] - contributions_next[opp];
        street_aggressor_next = static_cast<PlayerId>(player);
        ++street_num_raises_next;
        token = "r" + std::to_string(new_contrib);
    } else {
        throw std::invalid_argument("Unknown HUNL action");
    }

    HUNLState new_state = *this;
    new_state.contributions = contributions_next;
    new_state.stacks = stacks_next;
    new_state.street_history.push_back(action);
    new_state.current_street_tokens.push_back(token);
    new_state.street_aggressor = street_aggressor_next;
    new_state.street_num_raises = street_num_raises_next;
    new_state.to_call = to_call_next;
    new_state.folded = folded_next;
    new_state.all_in = all_in_next;

    if (new_state.folded[0] || new_state.folded[1]) {
        new_state.cur_player = -1;
        return new_state;
    }
    if (street_complete(action, new_state)) {
        return begin_street_transition(std::move(new_state));
    }

    const auto next_player = 1U - player;
    if (new_state.all_in[next_player]) {
        const auto refund = std::max(new_state.contributions[player] - new_state.contributions[next_player], 0);
        if (refund > 0) {
            new_state.contributions[player] -= refund;
            new_state.stacks[player] += refund;
            new_state.all_in[player] = new_state.stacks[player] == 0;
        }
        new_state.to_call = 0;
        return begin_street_transition(std::move(new_state));
    }

    new_state.cur_player = static_cast<PlayerId>(next_player);
    return new_state;
}

bool HUNLState::street_complete(ActionId action, const HUNLState& new_state) const {
    if (action == ACTION_FOLD) {
        return false;
    }
    if (new_state.to_call > 0) {
        return false;
    }
    if (action == ACTION_ALL_IN && to_call > 0) {
        return true;
    }
    const auto player = cur_player;
    const auto opponent = 1 - player;
    if (action == ACTION_CHECK && street_aggressor == -1 && new_state.street_history.size() >= 2) {
        return true;
    }
    if (street == Street::Preflop && action == ACTION_CHECK && player == 1 &&
        street_aggressor == 1 && street_num_raises == 1) {
        return true;
    }
    if (action == ACTION_CALL) {
        const auto preflop_sb_limp =
            street == Street::Preflop && street_aggressor == opponent && street_num_raises == 1 && player == 0;
        return !preflop_sb_limp;
    }
    return false;
}

HUNLState HUNLState::begin_street_transition(HUNLState state) const {
    state.betting_tokens.push_back(state.current_street_tokens);
    state.current_street_tokens.clear();
    if (state.street == Street::River) {
        state.street = Street::Showdown;
        state.cur_player = -1;
        return state;
    }
    if (state.all_in[0] || state.all_in[1]) {
        state.cur_player = -1;
        state.pending_board_deals = 1;
        state.street_history.clear();
        state.street_aggressor = -1;
        state.street_num_raises = 0;
        state.to_call = 0;
        return state;
    }
    const auto next_street = street_from_u8(static_cast<std::uint8_t>(state.street) + 1);
    if (!next_street.has_value()) {
        throw std::logic_error("next street out of range");
    }
    state.street = *next_street;
    state.cur_player = -1;
    state.pending_board_deals = cards_to_deal(*next_street);
    state.street_history.clear();
    state.street_aggressor = -1;
    state.street_num_raises = 0;
    state.to_call = 0;
    return state;
}

bool is_preflop(const ActionContext& ctx) {
    return ctx.street == Street::Preflop;
}

const std::vector<double>& bet_menu(const ActionContext& ctx) {
    switch (ctx.street) {
        case Street::Flop:
            if (ctx.flop_bet_fractions.has_value()) {
                return *ctx.flop_bet_fractions;
            }
            break;
        case Street::Turn:
            if (ctx.turn_bet_fractions.has_value()) {
                return *ctx.turn_bet_fractions;
            }
            break;
        case Street::River:
            if (ctx.river_bet_fractions.has_value()) {
                return *ctx.river_bet_fractions;
            }
            break;
        default:
            break;
    }
    return ctx.bet_size_fractions;
}

std::vector<double> raise_menu(const ActionContext& ctx) {
    const auto len = std::min(ctx.raise_size_xs.size(), RAISE_ACTION_IDS.size());
    return std::vector<double>(ctx.raise_size_xs.begin(), ctx.raise_size_xs.begin() + static_cast<std::ptrdiff_t>(len));
}

bool is_oop_flop_first_action(const ActionContext& ctx) {
    return ctx.street == Street::Flop &&
           ctx.cur_player == OOP_PLAYER &&
           ctx.street_action_count == 0 &&
           ctx.to_call == 0 &&
           ctx.street_aggressor < 0;
}

std::uint8_t raise_cap(const ActionContext& ctx) {
    return is_preflop(ctx) ? ctx.preflop_raise_cap : ctx.postflop_raise_cap;
}

int min_bet(const ActionContext& ctx) {
    return ctx.min_bet_bb * ctx.big_blind;
}

int force_allin_chip_threshold(const ActionContext& ctx) {
    return ctx.force_allin_threshold * ctx.big_blind;
}

int stack_remaining(const ActionContext& ctx) {
    return ctx.stacks[ctx.cur_player];
}

int min_raise_increment(const ActionContext& ctx) {
    return std::max(ctx.to_call, ctx.big_blind);
}

int python_round_positive(double value) {
    if (value < 0.0) {
        throw std::invalid_argument("python_round_positive expects non-negative input");
    }
    const auto floor_value = std::floor(value);
    const auto fraction = value - floor_value;
    if (fraction < 0.5) {
        return static_cast<int>(floor_value);
    }
    if (fraction > 0.5) {
        return static_cast<int>(floor_value + 1.0);
    }
    const auto floor_int = static_cast<int>(floor_value);
    return (floor_int % 2 == 0) ? floor_int : floor_int + 1;
}

int bet_amount_for_fraction(const ActionContext& ctx, double fraction) {
    return std::max(python_round_positive(static_cast<double>(ctx.pot) * fraction), min_bet(ctx));
}

int raise_to_for_multiplier(const ActionContext& ctx, double multiplier) {
    const auto aggressor_idx = static_cast<std::size_t>(std::max(ctx.street_aggressor, 0));
    const auto aggressor_contrib = ctx.contributions[aggressor_idx];
    const auto raise_to = python_round_positive(static_cast<double>(aggressor_contrib) * multiplier);
    const auto min_raise_to = aggressor_contrib + min_raise_increment(ctx);
    return std::max(raise_to, min_raise_to);
}

int compute_bet_amount(std::uint8_t action_id, const ActionContext& ctx) {
    const auto stack = stack_remaining(ctx);
    if (action_id == ACTION_ALL_IN) {
        return stack;
    }
    const auto it = std::find(BET_ACTION_IDS.begin(), BET_ACTION_IDS.end(), action_id);
    if (it == BET_ACTION_IDS.end()) {
        throw std::invalid_argument("compute_bet_amount: action is not a bet");
    }
    const auto idx = static_cast<std::size_t>(std::distance(BET_ACTION_IDS.begin(), it));
    return std::min(bet_amount_for_fraction(ctx, bet_menu(ctx).at(idx)), stack);
}

int compute_raise_to(std::uint8_t action_id, const ActionContext& ctx) {
    const auto cur_contrib = ctx.contributions[ctx.cur_player];
    const auto stack = stack_remaining(ctx);
    const auto max_raise_to = cur_contrib + stack;
    if (action_id == ACTION_ALL_IN) {
        return max_raise_to;
    }
    const auto raise_values = raise_menu(ctx);
    const auto it = std::find(RAISE_ACTION_IDS.begin(), RAISE_ACTION_IDS.end(), action_id);
    if (it == RAISE_ACTION_IDS.end()) {
        throw std::invalid_argument("compute_raise_to: action is not a raise");
    }
    const auto idx = static_cast<std::size_t>(std::distance(RAISE_ACTION_IDS.begin(), it));
    return std::min(raise_to_for_multiplier(ctx, raise_values.at(idx)), max_raise_to);
}

std::vector<ActionId> enumerate_bets(const ActionContext& ctx) {
    const auto stack = stack_remaining(ctx);
    const auto force_threshold = force_allin_chip_threshold(ctx);
    std::vector<int> seen_amounts;
    std::vector<ActionId> actions;
    const auto& menu = bet_menu(ctx);
    for (std::size_t i = 0; i < menu.size() && i < BET_ACTION_IDS.size(); ++i) {
        const auto raw_amount = bet_amount_for_fraction(ctx, menu[i]);
        if (raw_amount >= stack || (stack - raw_amount) <= force_threshold) {
            continue;
        }
        if (std::find(seen_amounts.begin(), seen_amounts.end(), raw_amount) != seen_amounts.end()) {
            continue;
        }
        seen_amounts.push_back(raw_amount);
        actions.push_back(BET_ACTION_IDS[i]);
    }
    return actions;
}

std::vector<ActionId> enumerate_raises(const ActionContext& ctx) {
    const auto cur_contrib = ctx.contributions[ctx.cur_player];
    const auto stack = stack_remaining(ctx);
    const auto max_raise_to = cur_contrib + stack;
    const auto force_threshold = force_allin_chip_threshold(ctx);
    std::vector<int> seen_raise_tos;
    std::vector<ActionId> actions;
    const auto raise_values = raise_menu(ctx);
    for (std::size_t i = 0; i < raise_values.size() && i < RAISE_ACTION_IDS.size(); ++i) {
        const auto raise_to = raise_to_for_multiplier(ctx, raise_values[i]);
        const auto chips_added = raise_to - cur_contrib;
        if (raise_to >= max_raise_to || (stack - chips_added) <= force_threshold) {
            continue;
        }
        if (std::find(seen_raise_tos.begin(), seen_raise_tos.end(), raise_to) != seen_raise_tos.end()) {
            continue;
        }
        seen_raise_tos.push_back(raise_to);
        actions.push_back(RAISE_ACTION_IDS[i]);
    }
    return actions;
}

std::vector<ActionId> enumerate_legal_actions(const ActionContext& ctx) {
    std::vector<ActionId> actions;
    const auto stack = stack_remaining(ctx);
    if (stack <= 0) {
        return actions;
    }

    const auto facing_bet = ctx.to_call > 0;
    if (facing_bet) {
        actions.push_back(ACTION_FOLD);
        actions.push_back(ACTION_CALL);
    } else {
        actions.push_back(ACTION_CHECK);
    }

    const auto cap_reached = ctx.street_num_raises >= raise_cap(ctx);
    const auto flop_no_donk = !facing_bet && is_oop_flop_first_action(ctx);
    if (!cap_reached && !flop_no_donk) {
        if (facing_bet) {
            const auto raises = enumerate_raises(ctx);
            actions.insert(actions.end(), raises.begin(), raises.end());
        } else {
            const auto bets = enumerate_bets(ctx);
            actions.insert(actions.end(), bets.begin(), bets.end());
        }
    }

    const auto can_actually_raise = stack > ctx.to_call;
    if (ctx.include_all_in && !cap_reached && !flop_no_donk && can_actually_raise) {
        actions.push_back(ACTION_ALL_IN);
    }
    return actions;
}

HUNLConfig default_tiny_subgame() {
    HUNLConfig cfg;
    cfg.starting_stack = 1000;
    cfg.starting_street = Street::River;
    cfg.initial_board = {
        card_to_int(14, 0), card_to_int(7, 3), card_to_int(2, 2), card_to_int(13, 1), card_to_int(5, 0)};
    cfg.initial_pot = 1000;
    cfg.initial_contributions = {500, 500};
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {card_to_int(14, 1), card_to_int(13, 3)},
        {card_to_int(12, 2), card_to_int(12, 1)},
    }};
    return cfg;
}

HUNLConfig benchmark_turn_subgame() {
    HUNLConfig cfg;
    cfg.starting_stack = 1000;
    cfg.starting_street = Street::Turn;
    cfg.initial_board = {
        card_to_int(14, 0), card_to_int(7, 3), card_to_int(2, 2), card_to_int(13, 1)};
    cfg.initial_pot = 1000;
    cfg.initial_contributions = {500, 500};
    cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {card_to_int(14, 1), card_to_int(13, 3)},
        {card_to_int(12, 2), card_to_int(12, 1)},
    }};
    return cfg;
}

}  // namespace core


