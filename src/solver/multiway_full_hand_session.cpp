#include "solver/multiway_full_hand_session.hpp"

#include <array>
#include <stdexcept>

namespace texas::solver::multiway {

MultiwayFullHandSession::MultiwayFullHandSession(
    games::multiway::MultiwayGameRules rules,
    core::PlayerId first_player,
    std::uint64_t hand_seed)
    : history_(games::multiway::MultiwayHandHistory::from_rules(rules, first_player, hand_seed)),
      state_(games::multiway::MultiwayState::initial(rules, first_player)) {
    std::array<MultiwayRangeBeliefSeatInput, MULTIWAY_RANGE_BELIEF_MAX_SEATS> inputs{};
    beliefs_.reset_uniform(rules.player_count, inputs.data());
}

const games::multiway::MultiwayState& MultiwayFullHandSession::observe(
    const games::multiway::MultiwayReplayEvent& event,
    const MultiwayRangeBeliefObservation* observation) {
    if (event.kind == games::multiway::MultiwayReplayEventKind::Decision && observation != nullptr) {
        if (beliefs_.apply_observation(
                static_cast<std::size_t>(event.decision.acting_seat), *observation) ==
            MultiwayRangeBeliefUpdateResult::NoPosteriorMass) {
            throw std::invalid_argument("full-hand observation has no posterior mass");
        }
    }
    state_ = games::multiway::apply_multiway_replay_event(state_, event);
    history_.events.push_back(event);
    return state_;
}

}  // namespace texas::solver::multiway
