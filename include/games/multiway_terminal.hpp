#pragma once

#include "games/hunl_eval.hpp"

#include <vector>

namespace core {

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

}  // namespace core
