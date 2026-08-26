#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "games/multiway_rules.hpp"

#include <cstdint>
#include <vector>

namespace texas::games::multiway {

// Public, deterministic event log. Decision seeds are retained even though
// engine action application itself is deterministic, so policy sampling can be
// reproduced without exposing private cards or implementation-specific state.
struct MultiwayReplayDecision {
    PlayerId acting_seat = -1;
    MultiwayAction action = MultiwayAction::Fold;
    int target_street_contribution = 0;
    std::uint64_t decision_seed = 0;
};

enum class MultiwayReplayEventKind : std::uint8_t {
    Decision,
    StreetTransition,
};

struct MultiwayReplayEvent {
    MultiwayReplayEventKind kind = MultiwayReplayEventKind::Decision;
    MultiwayReplayDecision decision{};
    Street next_street = Street::Flop;
    PlayerId first_player = -1;
};

struct MultiwayHandHistory {
    std::uint64_t schema_version = 1;
    std::uint64_t hand_seed = 0;
    MultiwayGameConfig initial_config{};
    std::vector<MultiwayReplayEvent> events;

    [[nodiscard]] static MultiwayHandHistory from_rules(
        const MultiwayGameRules& rules,
        PlayerId first_player = 0,
        std::uint64_t hand_seed = 0);
    void validate() const;
};

// Applies one event through the engine. Decisions must name the exact actor
// selected by the state, and transitions are accepted only after betting is
// complete. Illegal or malformed history is rejected with invalid_argument.
[[nodiscard]] MultiwayState apply_multiway_replay_event(
    const MultiwayState& state,
    const MultiwayReplayEvent& event);

// Reconstructs the exact public betting state from the initial public config
// and ordered replay events. No caller-provided state is trusted.
[[nodiscard]] MultiwayState replay_multiway_hand(const MultiwayHandHistory& history);

}  // namespace texas::games::multiway
