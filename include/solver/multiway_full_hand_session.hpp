#pragma once

#include "games/multiway_replay.hpp"
#include "solver/multiway_range_belief.hpp"

namespace texas::solver::multiway {

class MultiwayFullHandSession {
public:
    explicit MultiwayFullHandSession(
        games::multiway::MultiwayGameRules rules,
        core::PlayerId first_player = 0,
        std::uint64_t hand_seed = 0);

    [[nodiscard]] const games::multiway::MultiwayHandHistory& history() const noexcept { return history_; }
    [[nodiscard]] const games::multiway::MultiwayState& state() const noexcept { return state_; }
    [[nodiscard]] const MultiwayRangeBeliefs& beliefs() const noexcept { return beliefs_; }
    const games::multiway::MultiwayState& observe(
        const games::multiway::MultiwayReplayEvent& event,
        const MultiwayRangeBeliefObservation* observation = nullptr);

private:
    games::multiway::MultiwayHandHistory history_;
    games::multiway::MultiwayState state_;
    MultiwayRangeBeliefs beliefs_;
};

}  // namespace texas::solver::multiway
