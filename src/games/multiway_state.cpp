#include "games/multiway_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

bool valid_street(Street street) {
    return street == Street::Preflop || street == Street::Flop ||
           street == Street::Turn || street == Street::River;
}

}  // namespace

void MultiwayGameConfig::validate() const {
    const auto count = starting_stacks.size();
    if (count < 2U || count > 6U) {
        throw std::invalid_argument("MultiwayGameConfig requires two through six seats");
    }
    if (initial_contributions.size() != count ||
        (!initial_street_contributions.empty() && initial_street_contributions.size() != count)) {
        throw std::invalid_argument("MultiwayGameConfig seat vectors must have equal lengths");
    }
    if (first_player < 0 || static_cast<std::size_t>(first_player) >= count) {
        throw std::invalid_argument("MultiwayGameConfig first_player is out of range");
    }
    if (big_blind <= 0 || !valid_street(street)) {
        throw std::invalid_argument("MultiwayGameConfig has an invalid blind or street");
    }
    for (std::size_t seat = 0; seat < count; ++seat) {
        const auto street_contribution = initial_street_contributions.empty() ? 0 : initial_street_contributions[seat];
        if (starting_stacks[seat] < 0 || initial_contributions[seat] < 0 ||
            street_contribution < 0 || street_contribution > initial_contributions[seat] ||
            initial_contributions[seat] > starting_stacks[seat]) {
            throw std::invalid_argument("MultiwayGameConfig has invalid stack or contribution values");
        }
    }
}

void MultiwayBettingSnapshot::validate() const {
    const auto count = stacks.size();
    if (count < 2U || count > 6U || contributions.size() != count ||
        street_contributions.size() != count || folded.size() != count ||
        all_in.size() != count || may_raise.size() != count || pending.size() != count ||
        has_acted.size() != count || bet_faced_when_acted.size() != count) {
        throw std::invalid_argument("MultiwayBettingSnapshot requires equal two-through-six seat vectors");
    }
    if (big_blind <= 0 || last_full_raise_size <= 0 || current_bet < 0 || !valid_street(street) ||
        current_player < -1 || current_player >= static_cast<PlayerId>(count) ||
        last_aggressor < -1 || last_aggressor >= static_cast<PlayerId>(count)) {
        throw std::invalid_argument("MultiwayBettingSnapshot has invalid round metadata");
    }
    int max_street_contribution = 0;
    for (std::size_t seat = 0; seat < count; ++seat) {
        if (stacks[seat] < 0 || contributions[seat] < 0 || street_contributions[seat] < 0 ||
            street_contributions[seat] > contributions[seat] ||
            bet_faced_when_acted[seat] < 0 || bet_faced_when_acted[seat] > current_bet ||
            all_in[seat] != (stacks[seat] == 0) ||
            ((folded[seat] || all_in[seat]) && (may_raise[seat] || pending[seat]))) {
            throw std::invalid_argument("MultiwayBettingSnapshot has inconsistent seat state");
        }
        max_street_contribution = std::max(max_street_contribution, street_contributions[seat]);
    }
    if (max_street_contribution != current_bet) {
        throw std::invalid_argument("MultiwayBettingSnapshot current bet does not match street contributions");
    }
    if (current_player >= 0 &&
        (folded[static_cast<std::size_t>(current_player)] || all_in[static_cast<std::size_t>(current_player)] ||
         !pending[static_cast<std::size_t>(current_player)])) {
        throw std::invalid_argument("MultiwayBettingSnapshot current player is not actionable and pending");
    }
}

MultiwayState MultiwayState::initial(const MultiwayGameConfig& config) {
    config.validate();
    MultiwayState state;
    state.big_blind_ = config.big_blind;
    state.street_ = config.street;
    state.contributions_ = config.initial_contributions;
    state.street_contributions_ = config.initial_street_contributions.empty()
        ? std::vector<int>(config.starting_stacks.size(), 0)
        : config.initial_street_contributions;
    state.stacks_.resize(config.starting_stacks.size());
    state.folded_.assign(config.starting_stacks.size(), false);
    state.all_in_.resize(config.starting_stacks.size());
    state.may_raise_.resize(config.starting_stacks.size());
    state.pending_.resize(config.starting_stacks.size());
    state.has_acted_.assign(config.starting_stacks.size(), false);
    state.bet_faced_when_acted_.assign(config.starting_stacks.size(), 0);
    for (std::size_t seat = 0; seat < config.starting_stacks.size(); ++seat) {
        state.stacks_[seat] = config.starting_stacks[seat] - config.initial_contributions[seat];
        state.all_in_[seat] = state.stacks_[seat] == 0;
        state.may_raise_[seat] = !state.all_in_[seat];
        state.pending_[seat] = !state.all_in_[seat];
        state.current_bet_ = std::max(state.current_bet_, state.street_contributions_[seat]);
    }
    state.last_full_raise_size_ = config.big_blind;
    state.current_player_ = state.next_pending_after(config.first_player - 1);
    state.refresh_round_completion();
    return state;
}

MultiwayState MultiwayState::from_snapshot(const MultiwayBettingSnapshot& snapshot) {
    snapshot.validate();
    MultiwayState state;
    state.stacks_ = snapshot.stacks;
    state.contributions_ = snapshot.contributions;
    state.street_contributions_ = snapshot.street_contributions;
    state.folded_ = snapshot.folded;
    state.all_in_ = snapshot.all_in;
    state.may_raise_ = snapshot.may_raise;
    state.pending_ = snapshot.pending;
    state.has_acted_ = snapshot.has_acted;
    state.bet_faced_when_acted_ = snapshot.bet_faced_when_acted;
    state.current_player_ = snapshot.current_player;
    state.last_aggressor_ = snapshot.last_aggressor;
    state.current_bet_ = snapshot.current_bet;
    state.last_full_raise_size_ = snapshot.last_full_raise_size;
    state.big_blind_ = snapshot.big_blind;
    state.street_ = snapshot.street;
    state.refresh_round_completion();
    return state;
}

MultiwayBettingSnapshot MultiwayState::snapshot() const {
    MultiwayBettingSnapshot value;
    value.stacks = stacks_;
    value.contributions = contributions_;
    value.street_contributions = street_contributions_;
    value.folded = folded_;
    value.all_in = all_in_;
    value.may_raise = may_raise_;
    value.pending = pending_;
    value.has_acted = has_acted_;
    value.bet_faced_when_acted = bet_faced_when_acted_;
    value.current_player = current_player_;
    value.last_aggressor = last_aggressor_;
    value.current_bet = current_bet_;
    value.last_full_raise_size = last_full_raise_size_;
    value.big_blind = big_blind_;
    value.street = street_;
    return value;
}

bool MultiwayState::is_actionable(PlayerId player) const noexcept {
    return player >= 0 && static_cast<std::size_t>(player) < stacks_.size() &&
           !folded_[static_cast<std::size_t>(player)] && !all_in_[static_cast<std::size_t>(player)];
}

std::size_t MultiwayState::live_player_count() const noexcept {
    return static_cast<std::size_t>(std::count(folded_.begin(), folded_.end(), false));
}

bool MultiwayState::is_hand_over() const noexcept {
    if (live_player_count() <= 1U) {
        return true;
    }
    bool every_live_player_is_all_in = true;
    for (std::size_t seat = 0; seat < all_in_.size(); ++seat) {
        if (!folded_[seat] && !all_in_[seat]) {
            every_live_player_is_all_in = false;
            break;
        }
    }
    return every_live_player_is_all_in || (street_ == Street::River && current_player_ < 0);
}

bool MultiwayState::is_betting_round_complete() const noexcept {
    return current_player_ < 0 && !is_hand_over();
}

bool MultiwayState::requires_street_transition() const noexcept {
    return is_betting_round_complete() && street_ != Street::River;
}

PlayerId MultiwayState::next_pending_after(PlayerId player) const noexcept {
    const auto count = static_cast<PlayerId>(pending_.size());
    if (count == 0) return -1;
    for (PlayerId offset = 1; offset <= count; ++offset) {
        const auto candidate = (player + offset + count) % count;
        if (pending_[static_cast<std::size_t>(candidate)] && is_actionable(candidate)) {
            return candidate;
        }
    }
    return -1;
}

void MultiwayState::select_next_player() {
    current_player_ = next_pending_after(current_player_);
}

void MultiwayState::refresh_round_completion() {
    if (live_player_count() <= 1U || is_hand_over()) {
        current_player_ = -1;
        return;
    }
    bool has_pending = false;
    for (std::size_t seat = 0; seat < pending_.size(); ++seat) {
        if (pending_[seat] && is_actionable(static_cast<PlayerId>(seat))) {
            has_pending = true;
            break;
        }
    }
    if (!has_pending) current_player_ = -1;
}

void MultiwayState::reset_pending_after_full_raise(PlayerId aggressor) {
    for (std::size_t seat = 0; seat < pending_.size(); ++seat) {
        const auto player = static_cast<PlayerId>(seat);
        const auto pending = player != aggressor && is_actionable(player);
        pending_[seat] = pending;
        may_raise_[seat] = pending;
    }
}

void MultiwayState::refresh_raise_rights_after_short_raise(PlayerId aggressor) {
    for (std::size_t seat = 0; seat < pending_.size(); ++seat) {
        const auto player = static_cast<PlayerId>(seat);
        if (player == aggressor || !is_actionable(player)) continue;
        if (street_contributions_[seat] < current_bet_) pending_[seat] = true;
        may_raise_[seat] = !has_acted_[seat] ||
            current_bet_ - bet_faced_when_acted_[seat] >= last_full_raise_size_;
    }
}

std::vector<MultiwayAction> MultiwayState::legal_actions() const {
    if (current_player_ < 0) return {};
    const auto seat = static_cast<std::size_t>(current_player_);
    const auto to_call = current_bet_ - street_contributions_[seat];
    std::vector<MultiwayAction> actions;
    if (to_call > 0) {
        actions = {MultiwayAction::Fold, MultiwayAction::Call};
    } else {
        actions = {MultiwayAction::Check};
    }
    if (may_raise_[seat] && stacks_[seat] > to_call) {
        actions.push_back(to_call > 0 ? MultiwayAction::Raise : MultiwayAction::Bet);
        actions.push_back(MultiwayAction::AllIn);
    }
    return actions;
}

MultiwayState MultiwayState::apply(MultiwayAction action, int target_street_contribution) const {
    if (current_player_ < 0) throw std::logic_error("cannot act after betting is complete");
    const auto available_actions = legal_actions();
    if (std::find(available_actions.begin(), available_actions.end(), action) == available_actions.end()) {
        throw std::invalid_argument("illegal multiway action");
    }

    MultiwayState next = *this;
    const auto seat = static_cast<std::size_t>(current_player_);
    const auto to_call = current_bet_ - street_contributions_[seat];
    const auto pay = [&](int amount) {
        if (amount < 0 || amount > next.stacks_[seat]) throw std::invalid_argument("action exceeds stack");
        next.stacks_[seat] -= amount;
        next.contributions_[seat] += amount;
        next.street_contributions_[seat] += amount;
        next.all_in_[seat] = next.stacks_[seat] == 0;
    };

    switch (action) {
        case MultiwayAction::Fold:
            next.folded_[seat] = true;
            next.pending_[seat] = false;
            next.may_raise_[seat] = false;
            next.has_acted_[seat] = true;
            next.bet_faced_when_acted_[seat] = next.current_bet_;
            break;
        case MultiwayAction::Check:
            next.pending_[seat] = false;
            next.may_raise_[seat] = false;
            next.has_acted_[seat] = true;
            next.bet_faced_when_acted_[seat] = next.current_bet_;
            break;
        case MultiwayAction::Call:
            pay(std::min(to_call, next.stacks_[seat]));
            next.pending_[seat] = false;
            next.may_raise_[seat] = false;
            next.has_acted_[seat] = true;
            next.bet_faced_when_acted_[seat] = next.current_bet_;
            break;
        case MultiwayAction::Bet:
        case MultiwayAction::Raise: {
            if (target_street_contribution <= next.current_bet_) {
                throw std::invalid_argument("bet or raise must exceed the current bet");
            }
            const auto amount = target_street_contribution - next.street_contributions_[seat];
            pay(amount);
            const auto raise_size = target_street_contribution - next.current_bet_;
            const bool full_raise = raise_size >= next.last_full_raise_size_;
            if (!next.all_in_[seat] && raise_size < next.last_full_raise_size_) {
                throw std::invalid_argument("non-all-in raise is smaller than the full-raise minimum");
            }
            next.current_bet_ = target_street_contribution;
            next.last_aggressor_ = current_player_;
            next.has_acted_[seat] = true;
            next.bet_faced_when_acted_[seat] = next.current_bet_;
            if (full_raise) {
                next.last_full_raise_size_ = raise_size;
                next.reset_pending_after_full_raise(current_player_);
            } else {
                next.pending_[seat] = false;
                next.may_raise_[seat] = false;
                next.refresh_raise_rights_after_short_raise(current_player_);
            }
            break;
        }
        case MultiwayAction::AllIn: {
            const auto target = next.street_contributions_[seat] + next.stacks_[seat];
            pay(next.stacks_[seat]);
            if (target <= next.current_bet_) {
                next.pending_[seat] = false;
                next.may_raise_[seat] = false;
                next.has_acted_[seat] = true;
                next.bet_faced_when_acted_[seat] = next.current_bet_;
                break;
            }
            const auto raise_size = target - next.current_bet_;
            next.current_bet_ = target;
            next.last_aggressor_ = current_player_;
            next.has_acted_[seat] = true;
            next.bet_faced_when_acted_[seat] = next.current_bet_;
            if (raise_size >= next.last_full_raise_size_) {
                next.last_full_raise_size_ = raise_size;
                next.reset_pending_after_full_raise(current_player_);
            } else {
                next.pending_[seat] = false;
                next.may_raise_[seat] = false;
                next.refresh_raise_rights_after_short_raise(current_player_);
            }
            break;
        }
    }
    next.select_next_player();
    next.refresh_round_completion();
    return next;
}

MultiwayState MultiwayState::begin_next_street(Street next_street, PlayerId first_player) const {
    if (!requires_street_transition()) throw std::logic_error("street transition is not available");
    if (!valid_street(next_street) || static_cast<std::uint8_t>(next_street) != static_cast<std::uint8_t>(street_) + 1U) {
        throw std::invalid_argument("next street must immediately follow the current street");
    }
    if (first_player < 0 || static_cast<std::size_t>(first_player) >= stacks_.size()) {
        throw std::invalid_argument("first_player is out of range");
    }
    MultiwayState next = *this;
    next.street_ = next_street;
    next.street_contributions_.assign(stacks_.size(), 0);
    next.current_bet_ = 0;
    next.last_full_raise_size_ = big_blind_;
    next.last_aggressor_ = -1;
    for (std::size_t seat = 0; seat < stacks_.size(); ++seat) {
        next.pending_[seat] = next.is_actionable(static_cast<PlayerId>(seat));
        next.may_raise_[seat] = next.pending_[seat];
        next.has_acted_[seat] = false;
        next.bet_faced_when_acted_[seat] = 0;
    }
    next.current_player_ = next.next_pending_after(first_player - 1);
    next.refresh_round_completion();
    return next;
}

}  // namespace core
