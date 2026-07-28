#pragma once

#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"

#include <cstdint>
#include <vector>

namespace core {

// Cold-path factory for coordinator-admitted public descriptors. It owns no
// tree storage and never selects strategic bet sizes; Phase 2 supplies target
// contributions for Bet and Raise actions.
class MultiwayPublicBuilder {
public:
    [[nodiscard]] static std::vector<MultiwayActionDescriptor> make_legal_actions(
        const MultiwayBettingSnapshot& betting,
        std::uint64_t action_menu_id,
        const std::vector<int>& target_street_contributions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_root(
        const MultiwayBettingSnapshot& betting,
        std::vector<std::uint8_t> board,
        std::vector<MultiwayActionDescriptor> legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
        const MultiwayPublicStateDescriptor& parent,
        std::uint32_t action_index,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_board_chance_child(
        const MultiwayPublicStateDescriptor& parent,
        const MultiwayPublicBoardChanceEdge& edge,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_street_transition_child(
        const MultiwayPublicStateDescriptor& parent,
        const MultiwayPublicStreetTransition& transition,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static std::uint64_t stable_history_id(
        const std::vector<MultiwayPublicHistoryEntry>& history) noexcept;

private:
    [[nodiscard]] static std::uint64_t stable_public_state_id(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const std::vector<MultiwayPublicHistoryEntry>& history,
        const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;
};

}  // namespace core
