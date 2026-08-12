#pragma once

#include "core/namespaces.hpp"

#include "games/multiway_state.hpp"
#include "games/multiway_terminal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace texas::games::multiway {

struct MultiwayGameRules;

constexpr std::size_t kMultiwayFixedMaxSeats = 6U;
constexpr std::size_t kMultiwayFixedMaxActions = 4U;

struct MultiwayFixedActionMenu {
    std::array<MultiwayAction, kMultiwayFixedMaxActions> actions = {};
    std::uint8_t count = 0;

    [[nodiscard]] bool contains(MultiwayAction action) const noexcept;
};

// Hot-path state view. It owns no heap memory and is copied by value.
struct MultiwayFixedState {
    std::uint8_t seat_count = 0;
    std::array<int, kMultiwayFixedMaxSeats> stacks = {};
    std::array<int, kMultiwayFixedMaxSeats> contributions = {};
    std::array<int, kMultiwayFixedMaxSeats> street_contributions = {};
    std::array<bool, kMultiwayFixedMaxSeats> folded = {};
    std::array<bool, kMultiwayFixedMaxSeats> all_in = {};
    std::array<bool, kMultiwayFixedMaxSeats> may_raise = {};
    std::array<bool, kMultiwayFixedMaxSeats> pending = {};
    std::array<bool, kMultiwayFixedMaxSeats> has_acted = {};
    std::array<int, kMultiwayFixedMaxSeats> bet_faced_when_acted = {};
    PlayerId current_player = -1;
    PlayerId last_aggressor = -1;
    int current_bet = 0;
    int last_full_raise_size = 0;
    int big_blind = 0;
    Street street = Street::Flop;

    void validate() const;
    [[nodiscard]] MultiwayNextNodeKind next_node_kind() const noexcept;
    [[nodiscard]] MultiwayFixedActionMenu legal_actions() const noexcept;
    [[nodiscard]] MultiwayFixedState apply(MultiwayAction action, int target_street_contribution = 0) const;
    [[nodiscard]] MultiwayFixedState begin_next_street(Street next_street, PlayerId first_player) const;
};

// Cold boundary conversion. Hot traversal should retain MultiwayFixedState.
[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(const MultiwayGameConfig& config);
[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(
    const MultiwayGameRules& rules,
    PlayerId first_player = 0);
[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(const MultiwayBettingSnapshot& snapshot);

struct MultiwayFixedTerminalInput {
    std::uint8_t seat_count = 0;
    std::array<int, kMultiwayFixedMaxSeats> contributions = {};
    std::array<bool, kMultiwayFixedMaxSeats> folded = {};
    std::array<Strength, kMultiwayFixedMaxSeats> strengths = {};
    PlayerId odd_chip_first_seat = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
    bool flop_seen = true;

    void validate() const;
};

struct MultiwayFixedSidePot {
    int amount = 0;
    int contribution_cap = 0;
    std::array<PlayerId, kMultiwayFixedMaxSeats> eligible_players = {};
    std::uint8_t eligible_count = 0;
};

struct MultiwayFixedTerminalScratch {
    std::array<int, kMultiwayFixedMaxSeats> levels = {};
    std::uint8_t level_count = 0;
    std::array<PlayerId, kMultiwayFixedMaxSeats> winners = {};
};

struct MultiwayFixedTerminalResult {
    std::uint8_t seat_count = 0;
    std::array<MultiwayFixedSidePot, kMultiwayFixedMaxSeats> pots = {};
    std::uint8_t pot_count = 0;
    std::array<int, kMultiwayFixedMaxSeats> refunds = {};
    std::array<int, kMultiwayFixedMaxSeats> payouts = {};
    std::array<Value, kMultiwayFixedMaxSeats> utilities = {};
    int rake_taken = 0;
};

// Allocation-free settlement kernel. `scratch` is worker-owned and reusable.
void settle_multiway_terminal_fixed(
    const MultiwayFixedTerminalInput& input,
    MultiwayFixedTerminalScratch& scratch,
    MultiwayFixedTerminalResult& result);

// Applies the validated rules profile's rake policy at the settlement boundary.
void settle_multiway_terminal_fixed(
    const MultiwayFixedTerminalInput& input,
    const MultiwayGameRules& rules,
    MultiwayFixedTerminalScratch& scratch,
    MultiwayFixedTerminalResult& result);

}  // namespace texas::games::multiway
