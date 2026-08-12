#pragma once

#include "core/namespaces.hpp"

#include "games/hunl_eval.hpp"
#include "games/multiway_rake.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {
enum class MultiwayValueUnits : std::uint8_t;
}

namespace texas::games::multiway {

struct MultiwayGameRules;

// Input to terminal settlement after betting progression has completed.
// Contributions are total chips committed by each seat, including folded seats.
struct MultiwayTerminalInput {
    std::vector<int> contributions;
    std::vector<bool> folded;
    std::vector<Strength> strengths;
    // First seat in the table's odd-chip order (normally first live seat left
    // of the button).  It is explicit so settlement never relies on raw seat
    // identifiers as an unstated game rule.
    PlayerId odd_chip_first_seat = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
    // A flop was dealt before this terminal. It controls no-flop-no-drop rake.
    bool flop_seen = true;

    void validate() const;
};

struct MultiwaySidePot {
    int amount = 0;
    int contribution_cap = 0;
    std::vector<PlayerId> eligible_players;
};

struct MultiwayPotLayout {
    std::vector<MultiwaySidePot> pots;
    std::vector<int> refunds;
};

struct MultiwayTerminalResult {
    std::vector<MultiwaySidePot> pots;
    std::vector<int> refunds;
    std::vector<int> payouts;
    std::vector<Value> utilities;
    // Chips removed from contested pots exactly once by `rake_policy`.
    int rake_taken = 0;
    // Payouts and refunds always remain chips. Utilities use this explicit
    // unit so an adapter cannot label raw chip results as normalized values.
    texas::solver::multiway::MultiwayValueUnits utility_units{};
};

MultiwayPotLayout build_multiway_pot_layout(
    const std::vector<int>& contributions,
    const std::vector<bool>& folded);

// Builds ordered side pots and settles each one. Folded players contribute but
// are never eligible. Tied-pot odd chips follow the cyclic order beginning at
// `MultiwayTerminalInput::odd_chip_first_seat`.
// A layer funded by only one player is returned as an uncalled-bet refund.
MultiwayTerminalResult settle_multiway_terminal(const MultiwayTerminalInput& input);
MultiwayTerminalResult settle_multiway_terminal(
    const MultiwayTerminalInput& input,
    const MultiwayPotLayout& layout);
// Applies the validated rules profile's rake policy at the settlement boundary.
MultiwayTerminalResult settle_multiway_terminal(
    const MultiwayTerminalInput& input,
    const MultiwayGameRules& rules);

}  // namespace texas::games::multiway
