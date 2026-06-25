#include "core/leduc.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

inline constexpr std::array<int, 6> LEDUC_DECK = {11, 11, 12, 12, 13, 13};
inline constexpr int LEDUC_FIRST_RAISE_SIZE = 2;
inline constexpr int LEDUC_SECOND_RAISE_SIZE = 4;
inline constexpr std::uint8_t LEDUC_MAX_RAISES = 2;
inline constexpr int LEDUC_ANTE_SIZE = 1;

}

LeducState::LeducState()
    : private_cards{},
      public_card(std::nullopt),
      round1_history{},
      round2_history{},
      ante{LEDUC_ANTE_SIZE, LEDUC_ANTE_SIZE},
      folded{false, false},
      round_num(1),
      num_raises(0),
      num_calls(0),
      stakes(LEDUC_ANTE_SIZE),
      cur_player(-1) {}

LeducState LeducState::initial() {
    return LeducState{};
}

int LeducState::pot() const {
    return ante[0] + ante[1];
}

std::string LeducState::round_string(const std::vector<ActionId>& history) const {
    std::string out;
    out.reserve(history.size());
    for (const auto action : history) {
        switch (action) {
            case LEDUC_FOLD:
                out.push_back('f');
                break;
            case LEDUC_CALL:
                out.push_back('c');
                break;
            case LEDUC_RAISE:
                out.push_back('r');
                break;
            default:
                throw std::logic_error("invalid Leduc action in history");
        }
    }
    return out;
}

bool LeducState::round_complete() const {
    const auto remaining = static_cast<std::uint8_t>(2 - (folded[0] ? 1 : 0) - (folded[1] ? 1 : 0));
    if (num_raises == 0) {
        return num_calls == remaining;
    }
    return num_calls == remaining - 1;
}

bool LeducState::is_terminal() const {
    if (folded[0] || folded[1]) {
        return true;
    }
    return round_num == 2 && round_complete();
}

std::vector<Value> LeducState::utility() const {
    if (!is_terminal()) {
        throw std::logic_error("utility called on non-terminal state");
    }

    const auto ante0 = static_cast<Value>(ante[0]);
    const auto ante1 = static_cast<Value>(ante[1]);
    if (folded[0]) {
        return {-ante0, ante0};
    }
    if (folded[1]) {
        return {ante1, -ante1};
    }

    if (!public_card.has_value() || private_cards.size() != 2) {
        throw std::logic_error("showdown requires both private cards and public card");
    }

    const int public_value = *public_card;
    const int card0 = private_cards[0];
    const int card1 = private_cards[1];

    if (card0 == public_value && card1 != public_value) {
        return {ante1, -ante1};
    }
    if (card1 == public_value && card0 != public_value) {
        return {-ante0, ante0};
    }
    if (card0 > card1) {
        return {ante1, -ante1};
    }
    if (card1 > card0) {
        return {-ante0, ante0};
    }
    return {0.0, 0.0};
}

PlayerId LeducState::current_player() const {
    if (is_terminal()) {
        return -1;
    }
    return cur_player;
}

std::vector<ChanceOutcome> LeducState::chance_outcomes() const {
    if (cur_player != -1 || is_terminal()) {
        return {};
    }

    std::vector<int> remaining(LEDUC_DECK.begin(), LEDUC_DECK.end());
    for (const auto card : private_cards) {
        auto it = std::find(remaining.begin(), remaining.end(), card);
        if (it != remaining.end()) {
            remaining.erase(it);
        }
    }
    if (public_card.has_value()) {
        auto it = std::find(remaining.begin(), remaining.end(), *public_card);
        if (it != remaining.end()) {
            remaining.erase(it);
        }
    }

    const double probability = 1.0 / static_cast<double>(remaining.size());
    std::vector<ChanceOutcome> outcomes;
    outcomes.reserve(remaining.size());
    for (const auto card : remaining) {
        outcomes.push_back({card, probability});
    }
    return outcomes;
}

std::vector<ActionId> LeducState::legal_actions() const {
    if (is_terminal() || cur_player == -1) {
        return {};
    }

    const auto player = static_cast<std::size_t>(cur_player);
    std::vector<ActionId> actions;
    actions.reserve(3);
    if (stakes > ante[player]) {
        actions.push_back(LEDUC_FOLD);
    }
    actions.push_back(LEDUC_CALL);
    if (num_raises < LEDUC_MAX_RAISES) {
        actions.push_back(LEDUC_RAISE);
    }
    return actions;
}

LeducState LeducState::apply_chance(ActionId action) const {
    LeducState next = *this;
    if (next.private_cards.size() < 2) {
        next.private_cards.push_back(action);
        next.cur_player = next.private_cards.size() < 2 ? -1 : 0;
        return next;
    }

    next.public_card = action;
    next.round_num = 2;
    next.num_raises = 0;
    next.num_calls = 0;
    next.cur_player = first_non_folded(next.folded);
    return next;
}

LeducState LeducState::apply_player(ActionId action) const {
    LeducState next = *this;
    const auto player = static_cast<std::size_t>(next.cur_player);

    switch (action) {
        case LEDUC_FOLD:
            next.folded[player] = true;
            break;
        case LEDUC_CALL:
            next.ante[player] = next.stakes;
            ++next.num_calls;
            break;
        case LEDUC_RAISE: {
            const int raise_amount =
                next.round_num == 1 ? LEDUC_FIRST_RAISE_SIZE : LEDUC_SECOND_RAISE_SIZE;
            next.stakes += raise_amount;
            next.ante[player] = next.stakes;
            ++next.num_raises;
            next.num_calls = 0;
            break;
        }
        default:
            throw std::logic_error("invalid Leduc action");
    }

    if (next.round_num == 1) {
        next.round1_history.push_back(action);
    } else {
        next.round2_history.push_back(action);
    }

    if (next.folded[0] || next.folded[1]) {
        next.cur_player = -1;
        return next;
    }

    if (next.round_complete()) {
        if (next.round_num == 1) {
            next.cur_player = -1;
        } else {
            next.cur_player = -1;
        }
        return next;
    }

    next.cur_player = next_player(next.cur_player, next.folded);
    return next;
}

LeducState LeducState::next_state(ActionId action) const {
    return cur_player == -1 ? apply_chance(action) : apply_player(action);
}

std::string LeducState::infoset_key(PlayerId player) const {
    const auto private_card = private_cards.at(static_cast<std::size_t>(player));
    const auto round1 = round_string(round1_history);
    if (!public_card.has_value()) {
        return std::to_string(private_card) + "|" + round1;
    }
    const auto round2 = round_string(round2_history);
    return std::to_string(private_card) + "|" + round1 + "|" + std::to_string(*public_card) + "|" + round2;
}

std::size_t LeducState::num_players() const {
    return 2;
}

std::unique_ptr<Game> LeducState::clone() const {
    return std::make_unique<LeducState>(*this);
}

std::unique_ptr<Game> LeducState::apply(ActionId action) const {
    return std::make_unique<LeducState>(next_state(action));
}

PlayerId LeducState::first_non_folded(const std::array<bool, 2>& folded_players) {
    for (std::size_t i = 0; i < folded_players.size(); ++i) {
        if (!folded_players[i]) {
            return static_cast<PlayerId>(i);
        }
    }
    return -1;
}

PlayerId LeducState::next_player(PlayerId player, const std::array<bool, 2>& folded_players) {
    for (int offset = 1; offset <= 2; ++offset) {
        const auto candidate = static_cast<std::size_t>((player + offset) % 2);
        if (!folded_players[candidate]) {
            return static_cast<PlayerId>(candidate);
        }
    }
    return -1;
}

}  // namespace core
