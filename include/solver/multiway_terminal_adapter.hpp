#pragma once

#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace core {

// One canonical public chance outcome. The board is complete through this
// edge, so consumers never need to reconstruct a runout from a card label.
struct MultiwayBoardChanceEdge {
    std::uint8_t dealt_card = 0;
    std::vector<std::uint8_t> board;
    MultiwayBoardRunoutState board_runout{};
    Probability probability = 0.0;
};

// Result of a root-owned non-runout street transition. Board chance must have
// been applied before calling this operation.
struct MultiwayStreetTransition {
    MultiwayBettingSnapshot betting;
    std::vector<std::uint8_t> board;
    MultiwayBoardRunoutState board_runout{};
};

// Admission-ready binding for a canonical public chance successor. The caller
// assigns its coordinator id/history/action menu, while retaining this parent
// identity and typed edge unchanged.
struct MultiwayPublicBoardChanceEdge {
    MultiwayPublicStateId parent_id{};
    MultiwayPublicParentEdge incoming_edge{};
    MultiwayBoardChanceEdge chance{};
};

struct MultiwayPublicStreetTransition {
    MultiwayPublicStateId parent_id{};
    MultiwayPublicParentEdge incoming_edge{};
    MultiwayStreetTransition transition{};
};

// Cold integrated boundary for public chance, betting transitions, and
// terminal settlement. It owns no traversal or policy state.
class MultiwayTerminalAdapter {
public:
    explicit MultiwayTerminalAdapter(const MultiwayRootSnapshot& root);

    // Returns one-card, card-id-ordered chance edges after excluding the
    // sampled private deal. A StreetTransition can be advanced card-by-card
    // until the next street's board is complete; BoardRunout advances to river.
    [[nodiscard]] std::vector<MultiwayBoardChanceEdge> canonical_board_chance_edges(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const MultiwayJointPrivateSample& private_deal) const;

    [[nodiscard]] std::vector<MultiwayPublicBoardChanceEdge> canonical_public_board_chance_edges(
        MultiwayPublicStateId parent_id,
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const MultiwayJointPrivateSample& private_deal) const;

    // Applies the root's fixed next-street seat. The supplied board must be
    // exactly complete for the immediately following street.
    [[nodiscard]] MultiwayStreetTransition apply_street_transition(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board) const;

    [[nodiscard]] MultiwayPublicStreetTransition apply_public_street_transition(
        MultiwayPublicStateId parent_id,
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board) const;

    // Resolves exactly a fold terminal or a completed showdown/runout by
    // delegating to the established terminal and showdown layers.
    [[nodiscard]] MultiwayTerminalResult resolve_terminal(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const MultiwayJointPrivateSample& private_deal) const;

private:
    MultiwayRootSnapshot root_;
};

}  // namespace core
