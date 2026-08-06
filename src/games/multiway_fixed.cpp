#include "games/multiway_fixed.hpp"

#include "games/multiway_rules.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace core {
namespace {

bool valid_street(Street street) noexcept {
    return street == Street::Preflop || street == Street::Flop ||
        street == Street::Turn || street == Street::River;
}

bool is_actionable(const MultiwayFixedState& state, PlayerId player) noexcept {
    return player >= 0 && static_cast<std::size_t>(player) < state.seat_count &&
        !state.folded[static_cast<std::size_t>(player)] && !state.all_in[static_cast<std::size_t>(player)];
}

std::size_t live_count(const MultiwayFixedState& state) noexcept {
    std::size_t count = 0;
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) count += !state.folded[seat];
    return count;
}

std::size_t actionable_count(const MultiwayFixedState& state) noexcept {
    std::size_t count = 0;
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        count += is_actionable(state, static_cast<PlayerId>(seat));
    }
    return count;
}

PlayerId next_pending_after(const MultiwayFixedState& state, PlayerId player) noexcept {
    for (PlayerId offset = 1; offset <= state.seat_count; ++offset) {
        const auto candidate = (player + offset + state.seat_count) % state.seat_count;
        if (state.pending[static_cast<std::size_t>(candidate)] && is_actionable(state, candidate)) {
            return candidate;
        }
    }
    return -1;
}

void refresh_round_completion(MultiwayFixedState& state) noexcept {
    if (live_count(state) <= 1U) {
        state.current_player = -1;
        return;
    }
    if (actionable_count(state) <= 1U) {
        bool lone_player_faces_bet = false;
        for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
            if (state.pending[seat] && is_actionable(state, static_cast<PlayerId>(seat)) &&
                state.street_contributions[seat] < state.current_bet) {
                lone_player_faces_bet = true;
                break;
            }
        }
        if (!lone_player_faces_bet) {
            state.current_player = -1;
            return;
        }
    }
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        if (state.pending[seat] && is_actionable(state, static_cast<PlayerId>(seat))) return;
    }
    state.current_player = -1;
}

void reset_pending_after_full_raise(MultiwayFixedState& state, PlayerId aggressor) noexcept {
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        const auto pending = static_cast<PlayerId>(seat) != aggressor &&
            is_actionable(state, static_cast<PlayerId>(seat));
        state.pending[seat] = pending;
        state.may_raise[seat] = pending;
    }
}

void refresh_raise_rights_after_short_raise(MultiwayFixedState& state, PlayerId aggressor) noexcept {
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        const auto player = static_cast<PlayerId>(seat);
        if (player == aggressor || !is_actionable(state, player)) continue;
        if (state.street_contributions[seat] < state.current_bet) state.pending[seat] = true;
        state.may_raise[seat] = !state.has_acted[seat] ||
            state.current_bet - state.bet_faced_when_acted[seat] >= state.last_full_raise_size;
    }
}

}  // namespace

bool MultiwayFixedActionMenu::contains(MultiwayAction action) const noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (actions[index] == action) return true;
    }
    return false;
}

void MultiwayFixedState::validate() const {
    if (seat_count < 2U || seat_count > kMultiwayFixedMaxSeats || big_blind <= 0 ||
        last_full_raise_size <= 0 || current_bet < 0 || !valid_street(street) ||
        current_player < -1 || current_player >= seat_count ||
        last_aggressor < -1 || last_aggressor >= seat_count) {
        throw std::invalid_argument("MultiwayFixedState has invalid dimensions or metadata");
    }
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
        if (stacks[seat] < 0 || contributions[seat] < 0 || street_contributions[seat] < 0 ||
            street_contributions[seat] > contributions[seat] || all_in[seat] != (stacks[seat] == 0) ||
            ((folded[seat] || all_in[seat]) && (may_raise[seat] || pending[seat]))) {
            throw std::invalid_argument("MultiwayFixedState has inconsistent seat state");
        }
    }
}

MultiwayNextNodeKind MultiwayFixedState::next_node_kind() const noexcept {
    if (live_count(*this) <= 1U) return MultiwayNextNodeKind::FoldTerminal;
    if (current_player >= 0) return MultiwayNextNodeKind::BettingDecision;
    if (actionable_count(*this) <= 1U && street != Street::River) return MultiwayNextNodeKind::BoardRunout;
    return street == Street::River ? MultiwayNextNodeKind::ShowdownTerminal : MultiwayNextNodeKind::StreetTransition;
}

MultiwayFixedActionMenu MultiwayFixedState::legal_actions() const noexcept {
    MultiwayFixedActionMenu menu;
    if (current_player < 0) return menu;
    const auto seat = static_cast<std::size_t>(current_player);
    const auto to_call = current_bet - street_contributions[seat];
    menu.actions[menu.count++] = to_call > 0 ? MultiwayAction::Fold : MultiwayAction::Check;
    if (to_call > 0) menu.actions[menu.count++] = MultiwayAction::Call;
    const auto all_in_target = static_cast<std::int64_t>(street_contributions[seat]) + stacks[seat];
    const auto minimum_full_raise = static_cast<std::int64_t>(current_bet) + last_full_raise_size;
    const auto has_opponent = actionable_count(*this) > 1U;
    if (has_opponent && may_raise[seat] && all_in_target > minimum_full_raise) {
        menu.actions[menu.count++] = to_call > 0 ? MultiwayAction::Raise : MultiwayAction::Bet;
    }
    if (has_opponent && may_raise[seat] && all_in_target > current_bet) {
        menu.actions[menu.count++] = MultiwayAction::AllIn;
    }
    return menu;
}

MultiwayFixedState MultiwayFixedState::apply(MultiwayAction action, int target_street_contribution) const {
    validate();
    if (current_player < 0 || !legal_actions().contains(action)) {
        throw std::invalid_argument("illegal fixed multiway action");
    }
    auto next = *this;
    const auto seat = static_cast<std::size_t>(current_player);
    const auto to_call = current_bet - street_contributions[seat];
    const auto pay = [&](int amount) {
        if (amount < 0 || amount > next.stacks[seat]) throw std::invalid_argument("fixed multiway action exceeds stack");
        next.stacks[seat] -= amount;
        next.contributions[seat] += amount;
        next.street_contributions[seat] += amount;
        next.all_in[seat] = next.stacks[seat] == 0;
    };
    const auto acted = [&] {
        next.pending[seat] = false;
        next.may_raise[seat] = false;
        next.has_acted[seat] = true;
        next.bet_faced_when_acted[seat] = next.current_bet;
    };
    switch (action) {
        case MultiwayAction::Fold:
            next.folded[seat] = true;
            acted();
            break;
        case MultiwayAction::Check:
            acted();
            break;
        case MultiwayAction::Call:
            pay(std::min(to_call, next.stacks[seat]));
            acted();
            break;
        case MultiwayAction::Bet:
        case MultiwayAction::Raise: {
            if (target_street_contribution <= next.current_bet ||
                target_street_contribution >= next.street_contributions[seat] + next.stacks[seat]) {
                throw std::invalid_argument("invalid fixed multiway raise target");
            }
            pay(target_street_contribution - next.street_contributions[seat]);
            const auto raise_size = target_street_contribution - next.current_bet;
            if (!next.all_in[seat] && raise_size < next.last_full_raise_size) {
                throw std::invalid_argument("fixed multiway raise is below full minimum");
            }
            next.current_bet = target_street_contribution;
            next.last_aggressor = current_player;
            next.has_acted[seat] = true;
            next.bet_faced_when_acted[seat] = next.current_bet;
            if (raise_size >= next.last_full_raise_size) {
                next.last_full_raise_size = raise_size;
                reset_pending_after_full_raise(next, current_player);
            } else {
                next.pending[seat] = false;
                next.may_raise[seat] = false;
                refresh_raise_rights_after_short_raise(next, current_player);
            }
            break;
        }
        case MultiwayAction::AllIn: {
            const auto target = next.street_contributions[seat] + next.stacks[seat];
            pay(next.stacks[seat]);
            if (target <= next.current_bet) {
                acted();
                break;
            }
            const auto raise_size = target - next.current_bet;
            next.current_bet = target;
            next.last_aggressor = current_player;
            next.has_acted[seat] = true;
            next.bet_faced_when_acted[seat] = next.current_bet;
            if (raise_size >= next.last_full_raise_size) {
                next.last_full_raise_size = raise_size;
                reset_pending_after_full_raise(next, current_player);
            } else {
                next.pending[seat] = false;
                next.may_raise[seat] = false;
                refresh_raise_rights_after_short_raise(next, current_player);
            }
            break;
        }
    }
    next.current_player = next_pending_after(next, current_player);
    refresh_round_completion(next);
    return next;
}

MultiwayFixedState MultiwayFixedState::begin_next_street(Street next_street, PlayerId first_player) const {
    validate();
    if (next_node_kind() != MultiwayNextNodeKind::StreetTransition || !valid_street(next_street) ||
        static_cast<std::uint8_t>(next_street) != static_cast<std::uint8_t>(street) + 1U ||
        first_player < 0 || first_player >= seat_count) {
        throw std::invalid_argument("invalid fixed multiway street transition");
    }
    auto next = *this;
    next.street = next_street;
    next.current_bet = 0;
    next.last_full_raise_size = big_blind;
    next.last_aggressor = -1;
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
        next.street_contributions[seat] = 0;
        next.pending[seat] = is_actionable(next, static_cast<PlayerId>(seat));
        next.may_raise[seat] = next.pending[seat];
        next.has_acted[seat] = false;
        next.bet_faced_when_acted[seat] = 0;
    }
    next.current_player = next_pending_after(next, first_player - 1);
    refresh_round_completion(next);
    return next;
}

MultiwayFixedState make_multiway_fixed_state(const MultiwayGameConfig& config) {
    config.validate();
    MultiwayFixedState state;
    state.seat_count = static_cast<std::uint8_t>(config.starting_stacks.size());
    state.current_player = config.first_player;
    state.last_full_raise_size = config.big_blind;
    state.big_blind = config.big_blind;
    state.street = config.street;
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        const auto street_contribution = config.initial_street_contributions.empty() ? 0 : config.initial_street_contributions[seat];
        state.stacks[seat] = config.starting_stacks[seat] - config.initial_contributions[seat];
        state.contributions[seat] = config.initial_contributions[seat];
        state.street_contributions[seat] = street_contribution;
        state.all_in[seat] = state.stacks[seat] == 0;
        state.pending[seat] = !state.all_in[seat];
        state.may_raise[seat] = !state.all_in[seat];
        state.current_bet = std::max(state.current_bet, street_contribution);
    }
    state.current_player = next_pending_after(state, config.first_player - 1);
    refresh_round_completion(state);
    return state;
}

MultiwayFixedState make_multiway_fixed_state(const MultiwayGameRules& rules, PlayerId first_player) {
    return make_multiway_fixed_state(rules.make_initial_game_config(first_player));
}

MultiwayFixedState make_multiway_fixed_state(const MultiwayBettingSnapshot& snapshot) {
    snapshot.validate();
    MultiwayFixedState state;
    state.seat_count = static_cast<std::uint8_t>(snapshot.stacks.size());
    state.current_player = snapshot.current_player;
    state.last_aggressor = snapshot.last_aggressor;
    state.current_bet = snapshot.current_bet;
    state.last_full_raise_size = snapshot.last_full_raise_size;
    state.big_blind = snapshot.big_blind;
    state.street = snapshot.street;
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        state.stacks[seat] = snapshot.stacks[seat];
        state.contributions[seat] = snapshot.contributions[seat];
        state.street_contributions[seat] = snapshot.street_contributions[seat];
        state.folded[seat] = snapshot.folded[seat];
        state.all_in[seat] = snapshot.all_in[seat];
        state.may_raise[seat] = snapshot.may_raise[seat];
        state.pending[seat] = snapshot.pending[seat];
        state.has_acted[seat] = snapshot.has_acted[seat];
        state.bet_faced_when_acted[seat] = snapshot.bet_faced_when_acted[seat];
    }
    return state;
}

void MultiwayFixedTerminalInput::validate() const {
    if (seat_count < 2U || seat_count > kMultiwayFixedMaxSeats || odd_chip_first_seat < 0 ||
        odd_chip_first_seat >= seat_count) {
        throw std::invalid_argument("MultiwayFixedTerminalInput has invalid dimensions or odd-chip seat");
    }
    bool has_live = false;
    std::int64_t total = 0;
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
        if (contributions[seat] < 0) throw std::invalid_argument("MultiwayFixedTerminalInput has negative contribution");
        total += contributions[seat];
        has_live = has_live || !folded[seat];
    }
    if (!has_live) throw std::invalid_argument("MultiwayFixedTerminalInput requires a live player");
    if (total > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("MultiwayFixedTerminalInput exceeds supported chip total");
    }
    rake_policy.validate();
}

void settle_multiway_terminal_fixed(
    const MultiwayFixedTerminalInput& input,
    MultiwayFixedTerminalScratch& scratch,
    MultiwayFixedTerminalResult& result) {
    input.validate();
    result = {};
    result.seat_count = input.seat_count;
    scratch.level_count = 0U;
    for (std::size_t seat = 0; seat < input.seat_count; ++seat) {
        const auto level = input.contributions[seat];
        if (level == 0) continue;
        std::size_t insert = 0;
        while (insert < scratch.level_count && scratch.levels[insert] < level) ++insert;
        if (insert < scratch.level_count && scratch.levels[insert] == level) continue;
        for (std::size_t index = scratch.level_count; index > insert; --index) {
            scratch.levels[index] = scratch.levels[index - 1U];
        }
        scratch.levels[insert] = level;
        ++scratch.level_count;
    }
    int previous_level = 0;
    for (std::size_t level_index = 0; level_index < scratch.level_count; ++level_index) {
        const auto level = scratch.levels[level_index];
        std::uint8_t contributors = 0;
        MultiwayFixedSidePot pot;
        pot.contribution_cap = level;
        for (std::size_t seat = 0; seat < input.seat_count; ++seat) {
            if (input.contributions[seat] < level) continue;
            ++contributors;
            if (!input.folded[seat]) pot.eligible_players[pot.eligible_count++] = static_cast<PlayerId>(seat);
        }
        const auto amount = (level - previous_level) * contributors;
        previous_level = level;
        if (contributors == 1U) {
            for (std::size_t seat = 0; seat < input.seat_count; ++seat) {
                if (input.contributions[seat] >= level) result.refunds[seat] += amount;
            }
            continue;
        }
        if (contributors == 0U || pot.eligible_count == 0U || result.pot_count == kMultiwayFixedMaxSeats) {
            throw std::logic_error("invalid fixed multiway side pot");
        }
        pot.amount = amount;
        result.pots[result.pot_count++] = pot;
    }
    int contested_total = 0;
    for (std::size_t pot = 0; pot < result.pot_count; ++pot) contested_total += result.pots[pot].amount;
    result.rake_taken = input.rake_policy.rake_for_contested_pot(contested_total, input.flop_seen);
    auto remaining_rake = result.rake_taken;
    for (std::size_t pot = 0; pot < result.pot_count && remaining_rake > 0; ++pot) {
        const auto taken = std::min(result.pots[pot].amount, remaining_rake);
        result.pots[pot].amount -= taken;
        remaining_rake -= taken;
    }
    for (std::size_t pot_index = 0; pot_index < result.pot_count; ++pot_index) {
        const auto& pot = result.pots[pot_index];
        if (pot.amount == 0) continue;
        auto best = input.strengths[static_cast<std::size_t>(pot.eligible_players[0])];
        for (std::size_t index = 1; index < pot.eligible_count; ++index) {
            best = std::max(best, input.strengths[static_cast<std::size_t>(pot.eligible_players[index])]);
        }
        std::uint8_t winner_count = 0;
        for (std::size_t index = 0; index < pot.eligible_count; ++index) {
            const auto player = pot.eligible_players[index];
            if (input.strengths[static_cast<std::size_t>(player)] == best) scratch.winners[winner_count++] = player;
        }
        const auto share = pot.amount / winner_count;
        auto remainder = pot.amount % winner_count;
        for (PlayerId offset = 0; offset < input.seat_count; ++offset) {
            const auto player = (input.odd_chip_first_seat + offset) % input.seat_count;
            bool winner = false;
            for (std::size_t index = 0; index < winner_count; ++index) winner = winner || scratch.winners[index] == player;
            if (!winner) continue;
            result.payouts[static_cast<std::size_t>(player)] += share;
            if (remainder > 0) {
                ++result.payouts[static_cast<std::size_t>(player)];
                --remainder;
            }
        }
    }
    for (std::size_t seat = 0; seat < input.seat_count; ++seat) {
        result.utilities[seat] = static_cast<Value>(result.payouts[seat] + result.refunds[seat] - input.contributions[seat]);
    }
}

void settle_multiway_terminal_fixed(
    const MultiwayFixedTerminalInput& input,
    const MultiwayGameRules& rules,
    MultiwayFixedTerminalScratch& scratch,
    MultiwayFixedTerminalResult& result) {
    rules.validate();
    if (input.seat_count != rules.player_count) {
        throw std::invalid_argument("fixed multiway terminal input does not match rule seat count");
    }
    auto ruled_input = input;
    ruled_input.rake_policy = rules.rake_policy;
    settle_multiway_terminal_fixed(ruled_input, scratch, result);
}

}  // namespace core
