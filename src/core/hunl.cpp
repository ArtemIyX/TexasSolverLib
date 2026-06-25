#include "core/hunl.hpp"

#include <algorithm>
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
        throw std::invalid_argument(
            "HUNLConfig.validate: initial_contributions must be non-negative");
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
    return ctx.street == Street::Flop && ctx.cur_player == OOP_PLAYER && ctx.street_action_count == 0 &&
           ctx.to_call == 0 && ctx.street_aggressor < 0;
}

std::uint8_t raise_cap(const ActionContext& ctx) {
    return is_preflop(ctx) ? ctx.preflop_raise_cap : ctx.postflop_raise_cap;
}

}  // namespace core
