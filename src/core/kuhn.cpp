#include "core/kuhn.hpp"

#include <stdexcept>

namespace core {

namespace {
inline constexpr std::array<int, 3> KUHN_DECK = {11, 12, 13};
}

KuhnState::KuhnState() : cards{-1, -1}, history{}, chance_phase(0) {}

KuhnState KuhnState::initial() {
    return KuhnState{};
}

std::string KuhnState::history_string() const {
    std::string s;
    s.reserve(history.size());
    for (const auto action : history) {
        s.push_back(action == BET ? 'b' : 'p');
    }
    return s;
}

bool KuhnState::is_terminal() const {
    if (chance_phase < 2) {
        return false;
    }
    const auto h = history_string();
    return h == "pp" || h == "bp" || h == "bb" || h == "pbp" || h == "pbb";
}

std::vector<Value> KuhnState::utility() const {
    if (!is_terminal()) {
        throw std::logic_error("utility called on non-terminal state");
    }
    const auto h = history_string();
    const bool p0_wins_showdown = cards[0] > cards[1];
    Value payoff = 0.0;
    if (h == "pp") {
        payoff = p0_wins_showdown ? 1.0 : -1.0;
    } else if (h == "bp") {
        payoff = 1.0;
    } else if (h == "bb") {
        payoff = p0_wins_showdown ? 2.0 : -2.0;
    } else if (h == "pbp") {
        payoff = -1.0;
    } else if (h == "pbb") {
        payoff = p0_wins_showdown ? 2.0 : -2.0;
    } else {
        throw std::logic_error("utility called on non-terminal history: " + h);
    }
    return {payoff, -payoff};
}

PlayerId KuhnState::current_player() const {
    if (chance_phase < 2) {
        return -1;
    }
    return static_cast<PlayerId>(history.size() % 2);
}

std::vector<ChanceOutcome> KuhnState::chance_outcomes() const {
    if (chance_phase >= 2) {
        return {};
    }

    std::array<bool, 3> dealt = {false, false, false};
    for (const auto card : cards) {
        if (card >= 11) {
            dealt[static_cast<std::size_t>(card - 11)] = true;
        }
    }
    std::vector<ChanceOutcome> outcomes;
    for (std::size_t i = 0; i < KUHN_DECK.size(); ++i) {
        if (!dealt[i]) {
            outcomes.push_back({KUHN_DECK[i], 1.0 / 3.0 - 0.0});
        }
    }
    const double p = 1.0 / static_cast<double>(outcomes.size());
    for (auto& outcome : outcomes) {
        outcome.probability = p;
    }
    return outcomes;
}

std::vector<ActionId> KuhnState::legal_actions() const {
    if (chance_phase < 2) {
        return {};
    }
    if (is_terminal()) {
        return {};
    }
    return {PASS, BET};
}

KuhnState KuhnState::next_state(ActionId action) const {
    KuhnState next = *this;
    if (chance_phase == 0) {
        next.cards[0] = action;
        next.chance_phase = 1;
    } else if (chance_phase == 1) {
        next.cards[1] = action;
        next.chance_phase = 2;
    } else {
        next.history.push_back(action);
    }
    return next;
}

std::string KuhnState::infoset_key(PlayerId player) const {
    return std::to_string(cards[static_cast<std::size_t>(player)]) + "|" + history_string();
}

std::size_t KuhnState::num_players() const {
    return 2;
}

std::unique_ptr<Game> KuhnState::clone() const {
    return std::make_unique<KuhnState>(*this);
}

std::unique_ptr<Game> KuhnState::apply(ActionId action) const {
    return std::make_unique<KuhnState>(next_state(action));
}

}  // namespace core
