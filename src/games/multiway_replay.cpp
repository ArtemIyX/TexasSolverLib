#include "games/multiway_replay.hpp"

#include <stdexcept>
#include <algorithm>

namespace texas::games::multiway {

using core::PlayerId;
using core::Street;

bool valid_board(Street street, const std::vector<std::uint8_t>& board) {
    const auto expected = street == Street::Flop ? 3U : street == Street::Turn ? 4U : street == Street::River ? 5U : 0U;
    if (board.size() != expected || !std::is_sorted(board.begin(), board.end())) return false;
    return std::adjacent_find(board.begin(), board.end()) == board.end() &&
        std::all_of(board.begin(), board.end(), [](std::uint8_t card) { return card < 52U; });
}

MultiwayHandHistory MultiwayHandHistory::from_rules(
    const MultiwayGameRules& rules,
    PlayerId first_player,
    std::uint64_t replay_hand_seed) {
    MultiwayHandHistory history;
    history.hand_seed = replay_hand_seed;
    history.initial_config = rules.make_initial_game_config(first_player);
    return history;
}

void MultiwayHandHistory::validate() const {
    if (schema_version != 1U) {
        throw std::invalid_argument("unsupported multiway hand-history schema");
    }
    initial_config.validate();
    for (const auto& event : events) {
        if (event.kind == MultiwayReplayEventKind::Decision) {
            if (event.decision.acting_seat < 0 ||
                event.decision.acting_seat >= static_cast<PlayerId>(initial_config.starting_stacks.size())) {
                throw std::invalid_argument("multiway replay decision has an invalid seat");
            }
            continue;
        }
        if (event.kind == MultiwayReplayEventKind::StreetTransition) {
            if (event.first_player < 0 ||
                event.first_player >= static_cast<PlayerId>(initial_config.starting_stacks.size())) {
                throw std::invalid_argument("multiway replay transition has an invalid first player");
            }
            if (!event.board.empty() && !valid_board(event.next_street, event.board)) {
                throw std::invalid_argument("multiway replay transition has an invalid board");
            }
            continue;
        }
        throw std::invalid_argument("multiway replay has an invalid event kind");
    }
}

MultiwayState apply_multiway_replay_event(
    const MultiwayState& state,
    const MultiwayReplayEvent& event) {
    if (event.kind == MultiwayReplayEventKind::Decision) {
        if (state.next_node_kind() != MultiwayNextNodeKind::BettingDecision ||
            event.decision.acting_seat != state.current_player()) {
            throw std::invalid_argument("multiway replay decision does not match the current actor");
        }
        return state.apply(event.decision.action, event.decision.target_street_contribution);
    }
    if (event.kind == MultiwayReplayEventKind::StreetTransition) {
        if (state.next_node_kind() != MultiwayNextNodeKind::StreetTransition) {
            throw std::invalid_argument("multiway replay transition is not currently legal");
        }
        return state.begin_next_street(event.next_street, event.first_player);
    }
    throw std::invalid_argument("multiway replay has an invalid event kind");
}

MultiwayState replay_multiway_hand(const MultiwayHandHistory& history) {
    history.validate();
    auto state = MultiwayState::initial(history.initial_config);
    for (const auto& event : history.events) {
        state = apply_multiway_replay_event(state, event);
    }
    return state;
}

}  // namespace texas::games::multiway
