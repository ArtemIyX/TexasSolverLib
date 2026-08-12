#pragma once

#include "core/namespaces.hpp"

#include "games/multiway_state.hpp"

#include <cstdint>

namespace texas::games::multiway {

// Versioned supported cash-game rules profile. Chip values are exact integers.
// The current multiway engine supports the no-ante/no-straddle/no-rebuy profile
// only; unsupported profiles are rejected before a state is constructed.
struct MultiwayGameRules {
    std::uint64_t profile_version = 1;
    std::uint8_t player_count = 6;
    int initial_stack_chips = 10'000;
    int small_blind_chips = 50;
    int big_blind_chips = 100;
    int ante_chips = 0;
    int straddle_chips = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
    bool rebuys_enabled = false;

    [[nodiscard]] static constexpr MultiwayGameRules standard_6max() noexcept {
        return {};
    }

    void validate() const;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] MultiwayGameConfig make_initial_game_config(PlayerId first_player = 0) const;
};

}  // namespace texas::games::multiway
