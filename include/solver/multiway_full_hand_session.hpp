#pragma once

#include "games/multiway_replay.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_resolver.hpp"

#include <array>
#include <chrono>

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

    [[nodiscard]] MultiwayResolverResult decide(
        PlayerId hero_seat,
        std::array<std::uint8_t, 2> hero_cards,
        const MultiwayResolverConfig& config,
        std::chrono::steady_clock::time_point deadline = {}) const;
    void clear_actual_hand_policy() noexcept { actual_hand_frozen_ = false; }
    [[nodiscard]] bool actual_hand_policy_frozen() const noexcept { return actual_hand_frozen_; }
    void freeze_actual_hand_policy() noexcept { actual_hand_frozen_ = true; }

private:
    games::multiway::MultiwayHandHistory history_;
    games::multiway::MultiwayState state_;
    MultiwayRangeBeliefs beliefs_;
    bool actual_hand_frozen_ = false;
};

}  // namespace texas::solver::multiway
