#pragma once

#include "core/namespaces.hpp"

#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

class MultiwayResolver;

// Cold-path factory for coordinator-admitted public descriptors. It owns no
// tree storage and never selects strategic bet sizes; Phase 2 supplies target
// contributions for Bet and Raise actions.
class MultiwayPublicBuilder {
public:
    // Schema-v2 canonical action identity. The returned menu is sorted by
    // (action, target contribution), has contiguous indices, and has one
    // derived non-zero menu id on every entry.
    [[nodiscard]] static std::vector<MultiwayActionDescriptor> canonicalize_action_menu(
        const MultiwayBettingSnapshot& betting,
        std::vector<MultiwayActionDescriptor> menu);

    [[nodiscard]] static std::uint64_t stable_action_menu_id(
        const std::vector<MultiwayActionDescriptor>& canonical_menu) noexcept;

    [[nodiscard]] static std::vector<MultiwayActionDescriptor> make_legal_actions(
        const MultiwayBettingSnapshot& betting,
        const std::vector<int>& target_street_contributions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_root(
        const MultiwayBettingSnapshot& betting,
        std::vector<std::uint8_t> board,
        std::vector<MultiwayActionDescriptor> legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
        const MultiwayPublicStateDescriptor& parent,
        std::uint32_t action_index,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
        const MultiwayPublicStateDescriptor& parent,
        std::uint32_t action_index,
        MultiwayBettingSnapshot child_betting,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
        const MultiwayPublicStateDescriptor& parent,
        std::uint32_t action_index,
        MultiwayBettingSnapshot child_betting,
        const MultiwayActionDescriptor* child_legal_actions,
        std::size_t child_action_count);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_board_chance_child(
        const MultiwayPublicStateDescriptor& parent,
        const MultiwayPublicBoardChanceEdge& edge,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_board_chance_child(
        const MultiwayPublicStateDescriptor& parent,
        const MultiwaySampledPublicBoardChance& edge,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    [[nodiscard]] static MultiwayPublicStateDescriptor make_street_transition_child(
        const MultiwayPublicStateDescriptor& parent,
        const MultiwayPublicStreetTransition& transition,
        std::vector<MultiwayActionDescriptor> child_legal_actions);

    // Schema-v2 uses tagged, length-prefixed bytes. History contains ordered
    // actor/action/target/menu-id records. Public state contains the complete
    // betting snapshot, canonical sorted board, ordered history, and canonical
    // action menu. Parent links and diagnostic fields are deliberately absent.
    [[nodiscard]] static std::uint64_t stable_history_id(
        const std::vector<MultiwayPublicHistoryEntry>& history) noexcept;

    [[nodiscard]] static std::uint64_t stable_public_state_id(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const std::vector<MultiwayPublicHistoryEntry>& history,
        const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;

    // Lossless live-round key. This aliases the schema-v2 public-state
    // encoding, which already includes the full current history, exact target
    // contributions, active seats in the betting snapshot, board, and menu.
    [[nodiscard]] static std::uint64_t stable_lossless_current_round_key(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const std::vector<MultiwayPublicHistoryEntry>& history,
        const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;

private:
    friend class MultiwayResolver;
};

}  // namespace texas::solver::multiway
