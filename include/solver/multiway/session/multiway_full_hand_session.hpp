#pragma once

#include "games/multiway_replay.hpp"
#include "solver/multiway/session/multiway_range_belief.hpp"
#include "solver/multiway/resolver/multiway_resolver.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "games/multiway_terminal.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <utility>

namespace texas::solver::multiway {

class MultiwayFullHandSession {
public:
    explicit MultiwayFullHandSession(
        games::multiway::MultiwayGameRules rules,
        core::PlayerId first_player = 0,
        std::uint64_t hand_seed = 0);
    MultiwayFullHandSession(
        games::multiway::MultiwayGameConfig config,
        std::vector<std::uint8_t> board,
        std::uint64_t hand_seed = 0);

    [[nodiscard]] const games::multiway::MultiwayHandHistory& history() const noexcept { return history_; }
    [[nodiscard]] const games::multiway::MultiwayState& state() const noexcept { return state_; }
    [[nodiscard]] const MultiwayRangeBeliefs& beliefs() const noexcept { return beliefs_; }
    [[nodiscard]] const std::vector<std::uint8_t>& board() const noexcept { return board_; }
    const games::multiway::MultiwayState& observe(
        const games::multiway::MultiwayReplayEvent& event,
        const MultiwayRangeBeliefObservation* observation = nullptr);

    [[nodiscard]] MultiwayResolverResult decide(
        PlayerId hero_seat,
        std::array<std::uint8_t, 2> hero_cards,
        const MultiwayResolverConfig& config,
        std::chrono::steady_clock::time_point deadline = {}) const;
    [[nodiscard]] MultiwayActionTranslation translate_preflop_action(
        const MultiwayActionDescriptor& observed,
        const std::vector<MultiwayActionDescriptor>& menu,
        const MultiwayActionAbstractionConfig& abstraction = {}) const;
    [[nodiscard]] games::multiway::MultiwayTerminalResult settle(
        const std::vector<std::array<std::uint8_t, 2>>& holes) const;
    void clear_actual_hand_policy() noexcept { actual_hand_frozen_ = false; frozen_policy_.reset(); }
    [[nodiscard]] bool actual_hand_policy_frozen() const noexcept { return actual_hand_frozen_; }
    void freeze_actual_hand_policy() noexcept { actual_hand_frozen_ = true; }
    void freeze_actual_hand_policy(MultiwayResolverResult policy) {
        frozen_policy_ = std::move(policy);
        actual_hand_frozen_ = true;
    }

private:
    games::multiway::MultiwayHandHistory history_;
    games::multiway::MultiwayState state_;
    MultiwayRangeBeliefs beliefs_;
    std::vector<std::uint8_t> board_;
    mutable std::optional<MultiwayResolverResult> frozen_policy_;
    bool actual_hand_frozen_ = false;
};

}  // namespace texas::solver::multiway
