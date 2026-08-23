#include "solver/multiway_public_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kPublicFingerprintSchemaVersion = 2U;

enum class FingerprintField : std::uint8_t {
    Schema = 1U,
    ActionMenu = 2U,
    History = 3U,
    BettingStacks = 4U,
    BettingContributions = 5U,
    BettingStreetContributions = 6U,
    BettingFolded = 7U,
    BettingAllIn = 8U,
    BettingMayRaise = 9U,
    BettingPending = 10U,
    BettingHasActed = 11U,
    BettingFaced = 12U,
    CurrentPlayer = 13U,
    LastAggressor = 14U,
    CurrentBet = 15U,
    LastFullRaise = 16U,
    BigBlind = 17U,
    Street = 18U,
    Board = 19U,
    Action = 20U,
    ActionTarget = 21U,
    ActionMenuId = 22U,
    HistoryActor = 23U,
};

void append_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnvPrime;
}

void append_u64_bytes(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0U; byte < 8U; ++byte) {
        append_byte(hash, static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
    }
}

void append_field_prefix(std::uint64_t& hash, FingerprintField field, std::size_t bytes) noexcept {
    append_byte(hash, static_cast<std::uint8_t>(field));
    append_u64_bytes(hash, static_cast<std::uint64_t>(bytes));
}

void append_u64_field(std::uint64_t& hash, FingerprintField field, std::uint64_t value) noexcept {
    append_field_prefix(hash, field, sizeof(value));
    append_u64_bytes(hash, value);
}

void append_i64_field(std::uint64_t& hash, FingerprintField field, std::int64_t value) noexcept {
    append_u64_field(hash, field, static_cast<std::uint64_t>(value));
}

void append_cards_field(
    std::uint64_t& hash,
    FingerprintField field,
    const std::vector<std::uint8_t>& cards) noexcept {
    append_field_prefix(hash, field, cards.size());
    for (const auto card : cards) append_byte(hash, card);
}

void append_ints_field(
    std::uint64_t& hash,
    FingerprintField field,
    const std::vector<int>& values) noexcept {
    append_field_prefix(hash, field, values.size() * sizeof(std::uint64_t));
    for (const auto value : values) append_u64_bytes(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void append_bools_field(
    std::uint64_t& hash,
    FingerprintField field,
    const std::vector<bool>& values) noexcept {
    append_field_prefix(hash, field, values.size());
    for (const auto value : values) append_byte(hash, value ? 1U : 0U);
}

std::uint64_t non_zero(std::uint64_t value) noexcept {
    return value == 0U ? 1U : value;
}

void begin_fingerprint(std::uint64_t& hash) noexcept {
    hash = kFnvOffset;
    append_u64_field(hash, FingerprintField::Schema, kPublicFingerprintSchemaVersion);
}

bool action_less(const MultiwayActionDescriptor& left, const MultiwayActionDescriptor& right) noexcept {
    if (left.action != right.action) {
        return static_cast<std::uint8_t>(left.action) < static_cast<std::uint8_t>(right.action);
    }
    return left.target_street_contribution < right.target_street_contribution;
}

bool same_action_target(const MultiwayActionDescriptor& left, const MultiwayActionDescriptor& right) noexcept {
    return left.action == right.action && left.target_street_contribution == right.target_street_contribution;
}

void canonicalize_board(std::vector<std::uint8_t>& board) {
    std::sort(board.begin(), board.end());
    if (board.size() > 5U || !are_valid_and_distinct_cards(board.data(), board.size())) {
        throw std::invalid_argument("multiway public builder has an invalid board");
    }
}

std::size_t added_board_cards(
    const std::vector<std::uint8_t>& parent,
    const std::vector<std::uint8_t>& child,
    std::array<std::uint8_t, 5U>& added) {
    std::size_t count = 0U;
    for (const auto card : child) {
        if (!std::binary_search(parent.begin(), parent.end(), card)) added[count++] = card;
    }
    if (child.size() != parent.size() + count) {
        throw std::invalid_argument("multiway public builder child board does not contain its parent board");
    }
    return count;
}

void canonicalize_history(std::vector<MultiwayPublicHistoryEntry>& history) noexcept {
    for (auto& entry : history) entry.action.action_index = 0U;
}

void canonicalize_board_chance_edge(
    MultiwayPublicParentEdge& edge,
    const std::vector<std::uint8_t>& parent_board,
    const std::vector<std::uint8_t>& child_board) {
    std::array<std::uint8_t, 5U> added = {};
    const auto count = added_board_cards(parent_board, child_board, added);
    if (count == 0U) {
        throw std::invalid_argument("multiway public builder chance child must add board cards");
    }
    edge.dealt_cards.assign(added.begin(), added.begin() + static_cast<std::ptrdiff_t>(count));
    edge.dealt_card = added[0];
}

MultiwayBoardRunoutState runout_state(const MultiwayState& state, std::size_t board_count) {
    if (board_count > 5U) throw std::invalid_argument("multiway public builder has an oversized board");
    return {
        static_cast<std::uint8_t>(5U - board_count),
        state.requires_board_runout(),
    };
}

void append_action_menu(std::uint64_t& hash, const std::vector<MultiwayActionDescriptor>& menu) noexcept {
    append_field_prefix(hash, FingerprintField::ActionMenu, menu.size() * 2U * sizeof(std::uint64_t));
    for (const auto& action : menu) {
        append_u64_bytes(hash, static_cast<std::uint64_t>(action.action));
        append_u64_bytes(hash, static_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.target_street_contribution)));
    }
}

void append_history(std::uint64_t& hash, const std::vector<MultiwayPublicHistoryEntry>& history) noexcept {
    append_field_prefix(hash, FingerprintField::History, history.size() * 4U * sizeof(std::uint64_t));
    for (const auto& entry : history) {
        append_u64_bytes(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(entry.actor)));
        append_u64_bytes(hash, static_cast<std::uint64_t>(entry.action.action));
        append_u64_bytes(hash, static_cast<std::uint64_t>(
            static_cast<std::int64_t>(entry.action.target_street_contribution)));
        append_u64_bytes(hash, entry.action.action_menu_id);
    }
}

}  // namespace

std::vector<MultiwayActionDescriptor> MultiwayPublicBuilder::canonicalize_action_menu(
    const MultiwayBettingSnapshot& betting,
    std::vector<MultiwayActionDescriptor> menu) {
    const auto state = MultiwayState::from_snapshot(betting);
    if (state.current_player() < 0) {
        if (!menu.empty()) throw std::invalid_argument("multiway public builder has actions without an actor");
        return menu;
    }
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto legal = state.legal_actions();
    for (auto& candidate : menu) {
        if (candidate.target_street_contribution < 0 ||
            std::find(legal.begin(), legal.end(), candidate.action) == legal.end()) {
            throw std::invalid_argument("multiway public builder menu contains an illegal action");
        }
        const auto successor = state.apply(candidate.action, candidate.target_street_contribution);
        candidate.action_index = 0U;
        candidate.target_street_contribution = successor.street_contributions()[actor];
        candidate.action_menu_id = 0U;
    }
    std::sort(menu.begin(), menu.end(), action_less);
    menu.erase(std::unique(menu.begin(), menu.end(), same_action_target), menu.end());
    const auto menu_id = stable_action_menu_id(menu);
    for (std::size_t index = 0U; index < menu.size(); ++index) {
        menu[index].action_index = static_cast<std::uint32_t>(index);
        menu[index].action_menu_id = menu_id;
    }
    return menu;
}

std::uint64_t MultiwayPublicBuilder::stable_action_menu_id(
    const std::vector<MultiwayActionDescriptor>& canonical_menu) noexcept {
    std::uint64_t hash = 0U;
    begin_fingerprint(hash);
    append_action_menu(hash, canonical_menu);
    return non_zero(hash);
}

std::vector<MultiwayActionDescriptor> MultiwayPublicBuilder::make_legal_actions(
    const MultiwayBettingSnapshot& betting,
    const std::vector<int>& target_street_contributions) {
    const auto state = MultiwayState::from_snapshot(betting);
    const auto actions = state.legal_actions();
    if (actions.size() != target_street_contributions.size()) {
        throw std::invalid_argument("multiway public builder targets must match legal action kinds");
    }

    std::vector<MultiwayActionDescriptor> result;
    result.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        result.push_back({actions[index], 0U, target_street_contributions[index], 0U});
    }
    return canonicalize_action_menu(betting, std::move(result));
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_root(
    const MultiwayBettingSnapshot& betting,
    std::vector<std::uint8_t> board,
    std::vector<MultiwayActionDescriptor> legal_actions) {
    const auto state = MultiwayState::from_snapshot(betting);
    if (state.current_player() < 0) {
        throw std::invalid_argument("multiway public builder root requires an acting seat");
    }
    canonicalize_board(board);

    MultiwayPublicStateDescriptor root;
    root.betting = betting;
    root.board = std::move(board);
    root.board_runout = runout_state(state, root.board.size());
    root.legal_actions = canonicalize_action_menu(root.betting, std::move(legal_actions));
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
    canonicalize_board(child.board);
    child.history = parent.history;
    canonicalize_history(child.history);
    child.history.push_back({parent_state.current_player(), action});
    child.history.back().action.action_index = 0U;
    const auto child_state = MultiwayState::from_snapshot(child.betting);
    child.board_runout = runout_state(child_state, child.board.size());
    child.legal_actions = canonicalize_action_menu(child.betting, std::move(child_legal_actions));
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
    canonicalize_board(child.board);
    canonicalize_board_chance_edge(child.incoming_edge, parent.board, child.board);
    child.board_runout = edge.chance.board_runout;
    child.history = parent.history;
    canonicalize_history(child.history);
    child.legal_actions = canonicalize_action_menu(child.betting, std::move(child_legal_actions));
    child.canonical_history_id = stable_history_id(child.history);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate chance identity");
    }
    return child;
}

MultiwayPublicStateDescriptor MultiwayPublicBuilder::make_board_chance_child(
    const MultiwayPublicStateDescriptor& parent,
    const MultiwaySampledPublicBoardChance& edge,
    std::vector<MultiwayActionDescriptor> child_legal_actions) {
    if (edge.parent_id != parent.id ||
        (edge.dealt_card_count != 1U && edge.dealt_card_count != 3U) ||
        edge.board_count == 0U || edge.board_count > edge.board.size() ||
        edge.dealt_card_count > edge.board_count ||
        !std::isfinite(edge.probability) || edge.probability <= 0.0 || edge.probability > 1.0) {
        throw std::invalid_argument("multiway public builder sampled chance edge is invalid");
    }
    MultiwayPublicStateDescriptor child;
    child.parent_id = parent.id;
    child.incoming_edge.kind = MultiwayPublicParentEdgeKind::BoardChance;
    child.betting = parent.betting;
    child.board.assign(edge.board.begin(), edge.board.begin() + edge.board_count);
    canonicalize_board(child.board);
    canonicalize_board_chance_edge(child.incoming_edge, parent.board, child.board);
    child.board_runout = edge.board_runout;
    child.history = parent.history;
    canonicalize_history(child.history);
    child.legal_actions = canonicalize_action_menu(child.betting, std::move(child_legal_actions));
    child.canonical_history_id = stable_history_id(child.history);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate sampled chance identity");
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
    canonicalize_board(child.board);
    child.incoming_edge.transition_board = child.board;
    child.board_runout = transition.transition.board_runout;
    child.history = parent.history;
    canonicalize_history(child.history);
    child.legal_actions = canonicalize_action_menu(child.betting, std::move(child_legal_actions));
    child.canonical_history_id = stable_history_id(child.history);
    child.id = {stable_public_state_id(child.betting, child.board, child.history, child.legal_actions)};
    if (child.id == parent.id) {
        throw std::logic_error("multiway public builder produced a duplicate street-transition identity");
    }
    return child;
}

std::uint64_t MultiwayPublicBuilder::stable_history_id(
    const std::vector<MultiwayPublicHistoryEntry>& history) noexcept {
    std::uint64_t hash = 0U;
    begin_fingerprint(hash);
    append_history(hash, history);
    return non_zero(hash);
}

std::uint64_t MultiwayPublicBuilder::stable_public_state_id(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayPublicHistoryEntry>& history,
    const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept {
    std::uint64_t hash = 0U;
    begin_fingerprint(hash);
    append_ints_field(hash, FingerprintField::BettingStacks, betting.stacks);
    append_ints_field(hash, FingerprintField::BettingContributions, betting.contributions);
    append_ints_field(hash, FingerprintField::BettingStreetContributions, betting.street_contributions);
    append_bools_field(hash, FingerprintField::BettingFolded, betting.folded);
    append_bools_field(hash, FingerprintField::BettingAllIn, betting.all_in);
    append_bools_field(hash, FingerprintField::BettingMayRaise, betting.may_raise);
    append_bools_field(hash, FingerprintField::BettingPending, betting.pending);
    append_bools_field(hash, FingerprintField::BettingHasActed, betting.has_acted);
    append_ints_field(hash, FingerprintField::BettingFaced, betting.bet_faced_when_acted);
    append_i64_field(hash, FingerprintField::CurrentPlayer, betting.current_player);
    append_i64_field(hash, FingerprintField::LastAggressor, betting.last_aggressor);
    append_i64_field(hash, FingerprintField::CurrentBet, betting.current_bet);
    append_i64_field(hash, FingerprintField::LastFullRaise, betting.last_full_raise_size);
    append_i64_field(hash, FingerprintField::BigBlind, betting.big_blind);
    append_u64_field(hash, FingerprintField::Street, static_cast<std::uint64_t>(betting.street));
    append_cards_field(hash, FingerprintField::Board, board);
    append_history(hash, history);
    append_action_menu(hash, legal_actions);
    return non_zero(hash);
}

std::uint64_t MultiwayPublicBuilder::stable_lossless_current_round_key(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayPublicHistoryEntry>& history,
    const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept {
    return stable_public_state_id(betting, board, history, legal_actions);
}

}  // namespace texas::solver::multiway
