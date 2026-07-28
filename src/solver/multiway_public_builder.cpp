#include "solver/multiway_public_builder.hpp"

#include <stdexcept>
#include <utility>

namespace core {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

void append_ints(std::uint64_t& hash, const std::vector<int>& values) noexcept {
    append_u64(hash, values.size());
    for (const auto value : values) append_u64(hash, static_cast<std::uint64_t>(value));
}

void append_bools(std::uint64_t& hash, const std::vector<bool>& values) noexcept {
    append_u64(hash, values.size());
    for (const auto value : values) append_u64(hash, value ? 1U : 0U);
}

void append_actions(
    std::uint64_t& hash,
    const std::vector<MultiwayActionDescriptor>& actions) noexcept {
    append_u64(hash, actions.size());
    for (const auto& action : actions) {
        append_u64(hash, static_cast<std::uint64_t>(action.action));
        append_u64(hash, action.action_index);
        append_u64(hash, static_cast<std::uint64_t>(action.target_street_contribution));
        append_u64(hash, action.action_menu_id);
    }
}

void append_history(
    std::uint64_t& hash,
    const std::vector<MultiwayPublicHistoryEntry>& history) noexcept {
    append_u64(hash, history.size());
    for (const auto& entry : history) {
        append_u64(hash, static_cast<std::uint64_t>(entry.actor));
        append_u64(hash, static_cast<std::uint64_t>(entry.action.action));
        append_u64(hash, entry.action.action_index);
        append_u64(hash, static_cast<std::uint64_t>(entry.action.target_street_contribution));
        append_u64(hash, entry.action.action_menu_id);
    }
}

std::uint64_t non_zero(std::uint64_t value) noexcept {
    return value == 0U ? 1U : value;
}

std::uint64_t child_history_id(
    const MultiwayPublicStateDescriptor& parent,
    const MultiwayPublicParentEdge& edge) noexcept {
    auto hash = kFnvOffset;
    append_u64(hash, parent.canonical_history_id);
    append_u64(hash, static_cast<std::uint64_t>(edge.kind));
    append_u64(hash, edge.dealt_card);
    for (const auto card : edge.dealt_cards) append_u64(hash, card);
    for (const auto card : edge.transition_board) append_u64(hash, card);
    return non_zero(hash);
}

MultiwayBoardRunoutState runout_state(const MultiwayState& state, std::size_t board_count) {
    if (board_count > 5U) throw std::invalid_argument("multiway public builder has an oversized board");
    return {
        static_cast<std::uint8_t>(5U - board_count),
        state.requires_board_runout(),
    };
}

}  // namespace

std::vector<MultiwayActionDescriptor> MultiwayPublicBuilder::make_legal_actions(
    const MultiwayBettingSnapshot& betting,
    std::uint64_t action_menu_id,
    const std::vector<int>& target_street_contributions) {
    if (action_menu_id == 0U) {
        throw std::invalid_argument("multiway public builder requires a non-zero action menu id");
    }
    const auto state = MultiwayState::from_snapshot(betting);
    const auto actions = state.legal_actions();
    if (actions.size() != target_street_contributions.size()) {
        throw std::invalid_argument("multiway public builder targets must match legal action kinds");
    }

    std::vector<MultiwayActionDescriptor> result;
    result.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto successor = state.apply(actions[index], target_street_contributions[index]);
        const auto acting_seat = static_cast<std::size_t>(state.current_player());
        result.push_back({
            actions[index],
            static_cast<std::uint32_t>(index),
            successor.street_contributions()[acting_seat],
            action_menu_id,
        });
    }
    return result;
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_root(
    const MultiwayBettingSnapshot& betting,
    std::vector<std::uint8_t> board,
    std::vector<MultiwayActionDescriptor> legal_actions) {
    const auto state = MultiwayState::from_snapshot(betting);
    if (state.current_player() < 0) {
        throw std::invalid_argument("multiway public builder root requires an acting seat");
    }

    MultiwayPublicStateDescriptor root;
    root.betting = betting;
    root.board = std::move(board);
    root.board_runout = runout_state(state, root.board.size());
    root.legal_actions = std::move(legal_actions);
    root.canonical_history_id = stable_history_id(root.history);
    root.id = {stable_public_state_id(root.betting, root.board, root.history, root.legal_actions)};
    return root;
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_action_child(
    const MultiwayPublicStateDescriptor& parent,
    std::uint32_t action_index,
    std::vector<MultiwayActionDescriptor> child_legal_actions) {
    const auto parent_state = MultiwayState::from_snapshot(parent.betting);
    if (parent_state.current_player() < 0 || action_index >= parent.legal_actions.size()) {
        throw std::invalid_argument("multiway public builder action child has no selected legal action");
    }
    const auto& action = parent.legal_actions[action_index];
    if (action.action_index != action_index) {
        throw std::invalid_argument("multiway public builder action indices must be contiguous");
    }

    MultiwayPublicStateDescriptor child;
    child.parent_id = parent.id;
    child.incoming_edge.kind = MultiwayPublicParentEdgeKind::BettingAction;
    child.incoming_edge.action = action;
    child.betting = parent_state.apply(action.action, action.target_street_contribution).snapshot();
    child.board = parent.board;
    child.history = parent.history;
    child.history.push_back({parent_state.current_player(), action});
    const auto child_state = MultiwayState::from_snapshot(child.betting);
    child.board_runout = runout_state(child_state, child.board.size());
    child.legal_actions = std::move(child_legal_actions);
    child.canonical_history_id = stable_history_id(child.history);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate parent and child identity");
    }
    return child;
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_board_chance_child(
    const MultiwayPublicStateDescriptor& parent,
    const MultiwayPublicBoardChanceEdge& edge,
    std::vector<MultiwayActionDescriptor> child_legal_actions) {
    if (edge.parent_id != parent.id || edge.incoming_edge.kind != MultiwayPublicParentEdgeKind::BoardChance) {
        throw std::invalid_argument("multiway public builder board chance edge has the wrong parent");
    }
    MultiwayPublicStateDescriptor child;
    child.parent_id = parent.id;
    child.incoming_edge = edge.incoming_edge;
    child.betting = parent.betting;
    child.board = edge.chance.board;
    child.board_runout = edge.chance.board_runout;
    child.history = parent.history;
    child.legal_actions = std::move(child_legal_actions);
    child.canonical_history_id = child_history_id(parent, child.incoming_edge);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate chance identity");
    }
    return child;
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_street_transition_child(
    const MultiwayPublicStateDescriptor& parent,
    const MultiwayPublicStreetTransition& transition,
    std::vector<MultiwayActionDescriptor> child_legal_actions) {
    if (transition.parent_id != parent.id ||
        transition.incoming_edge.kind != MultiwayPublicParentEdgeKind::StreetTransition) {
        throw std::invalid_argument("multiway public builder street transition has the wrong parent");
    }
    MultiwayPublicStateDescriptor child;
    child.parent_id = parent.id;
    child.incoming_edge = transition.incoming_edge;
    child.betting = transition.transition.betting;
    child.board = transition.transition.board;
    child.board_runout = transition.transition.board_runout;
    child.history = parent.history;
    child.legal_actions = std::move(child_legal_actions);
    child.canonical_history_id = child_history_id(parent, child.incoming_edge);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate street-transition identity");
    }
    return child;
}

std::uint64_t MultiwayPublicBuilder::stable_history_id(
    const std::vector<MultiwayPublicHistoryEntry>& history) noexcept {
    auto hash = kFnvOffset;
    append_history(hash, history);
    return non_zero(hash);
}

std::uint64_t MultiwayPublicBuilder::stable_public_state_id(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayPublicHistoryEntry>& history,
    const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept {
    auto hash = kFnvOffset;
    append_ints(hash, betting.stacks);
    append_ints(hash, betting.contributions);
    append_ints(hash, betting.street_contributions);
    append_bools(hash, betting.folded);
    append_bools(hash, betting.all_in);
    append_bools(hash, betting.may_raise);
    append_bools(hash, betting.pending);
    append_bools(hash, betting.has_acted);
    append_ints(hash, betting.bet_faced_when_acted);
    append_u64(hash, static_cast<std::uint64_t>(betting.current_player));
    append_u64(hash, static_cast<std::uint64_t>(betting.last_aggressor));
    append_u64(hash, static_cast<std::uint64_t>(betting.current_bet));
    append_u64(hash, static_cast<std::uint64_t>(betting.last_full_raise_size));
    append_u64(hash, static_cast<std::uint64_t>(betting.big_blind));
    append_u64(hash, static_cast<std::uint64_t>(betting.street));
    append_u64(hash, board.size());
    for (const auto card : board) append_u64(hash, card);
    append_history(hash, history);
    append_actions(hash, legal_actions);
    return non_zero(hash);
}

}  // namespace core
