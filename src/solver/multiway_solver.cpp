#include "solver/multiway_solver.hpp"
#include "solver/multiway_public_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {
namespace {

bool valid_action(MultiwayAction action) noexcept {
    return action == MultiwayAction::Fold || action == MultiwayAction::Check ||
           action == MultiwayAction::Call || action == MultiwayAction::Bet ||
           action == MultiwayAction::Raise || action == MultiwayAction::AllIn;
}

bool valid_public_parent_edge_kind(MultiwayPublicParentEdgeKind kind) noexcept {
    return kind == MultiwayPublicParentEdgeKind::None ||
           kind == MultiwayPublicParentEdgeKind::BettingAction ||
           kind == MultiwayPublicParentEdgeKind::BoardChance ||
           kind == MultiwayPublicParentEdgeKind::StreetTransition;
}

bool valid_odd_chip_rule(MultiwayOddChipRule rule) noexcept {
    return rule == MultiwayOddChipRule::AscendingSeatIdFromFirstSeat;
}

std::uint8_t expected_board_card_count(Street street) {
    switch (street) {
        case Street::Preflop: return 0;
        case Street::Flop: return 3;
        case Street::Turn: return 4;
        case Street::River: return 5;
        case Street::Showdown: break;
    }
    throw std::invalid_argument("multiway public state has an invalid board street");
}

bool same_board(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) noexcept {
    return left == right;
}

bool same_card_set(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) noexcept {
    if (left.size() != right.size()) return false;
    for (const auto card : left) {
        if (std::find(right.begin(), right.end(), card) == right.end()) return false;
    }
    return true;
}

bool same_history_action(
    const MultiwayActionDescriptor& left,
    const MultiwayActionDescriptor& right) noexcept {
    return left.action == right.action &&
           left.target_street_contribution == right.target_street_contribution &&
           left.action_menu_id == right.action_menu_id;
}

bool board_contains(
    const std::vector<std::uint8_t>& complete,
    const std::vector<std::uint8_t>& subset) noexcept {
    return std::includes(complete.begin(), complete.end(), subset.begin(), subset.end());
}

bool matches_added_board_cards(
    const std::vector<std::uint8_t>& parent,
    const std::vector<std::uint8_t>& child,
    const std::vector<std::uint8_t>& dealt_cards) noexcept {
    if (child.size() <= parent.size() || dealt_cards.size() > 5U) return false;
    std::array<std::uint8_t, 5U> added = {};
    std::size_t count = 0U;
    for (const auto card : child) {
        if (!std::binary_search(parent.begin(), parent.end(), card)) added[count++] = card;
    }
    if (child.size() != parent.size() + count || count != dealt_cards.size()) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (added[index] != dealt_cards[index]) return false;
    }
    return true;
}

bool same_betting_snapshot(
    const MultiwayBettingSnapshot& left,
    const MultiwayBettingSnapshot& right) noexcept {
    return left.stacks == right.stacks &&
           left.contributions == right.contributions &&
           left.street_contributions == right.street_contributions &&
           left.folded == right.folded &&
           left.all_in == right.all_in &&
           left.may_raise == right.may_raise &&
           left.pending == right.pending &&
           left.has_acted == right.has_acted &&
           left.bet_faced_when_acted == right.bet_faced_when_acted &&
           left.current_player == right.current_player &&
           left.last_aggressor == right.last_aggressor &&
           left.current_bet == right.current_bet &&
           left.last_full_raise_size == right.last_full_raise_size &&
           left.big_blind == right.big_blind &&
           left.street == right.street;
}

bool same_public_state_descriptor(
    const MultiwayPublicStateDescriptor& left,
    const MultiwayPublicStateDescriptor& right) noexcept {
    return left.id == right.id &&
           left.parent_id == right.parent_id &&
           left.incoming_edge.kind == right.incoming_edge.kind &&
           left.incoming_edge.action == right.incoming_edge.action &&
           left.incoming_edge.dealt_card == right.incoming_edge.dealt_card &&
           left.incoming_edge.dealt_cards == right.incoming_edge.dealt_cards &&
           left.incoming_edge.transition_board == right.incoming_edge.transition_board &&
           left.canonical_history_id == right.canonical_history_id &&
           same_betting_snapshot(left.betting, right.betting) &&
           left.board == right.board &&
           left.board_runout == right.board_runout &&
           left.history == right.history &&
           left.legal_actions == right.legal_actions;
}

void validate_public_state_child_transition(
    const MultiwayRootSnapshot& root,
    const MultiwayPublicStateDescriptor& parent,
    const MultiwayPublicStateDescriptor& child) {
    const auto parent_state = MultiwayState::from_snapshot(parent.betting);
    switch (child.incoming_edge.kind) {
        case MultiwayPublicParentEdgeKind::BettingAction: {
            if (child.history.size() != parent.history.size() + 1U ||
                !std::equal(parent.history.begin(), parent.history.end(), child.history.begin()) ||
                child.canonical_history_id == parent.canonical_history_id ||
                child.board != parent.board ||
                child.board_runout.remaining_board_cards != parent.board_runout.remaining_board_cards) {
                throw std::invalid_argument("multiway child action state does not preserve parent public data");
            }
            const auto& appended = child.history.back();
            if (parent_state.next_node_kind() != MultiwayNextNodeKind::BettingDecision ||
                appended.actor != parent.betting.current_player ||
                !same_history_action(appended.action, child.incoming_edge.action) ||
                std::none_of(parent.legal_actions.begin(), parent.legal_actions.end(),
                    [&appended](const MultiwayActionDescriptor& action) {
                        return same_history_action(action, appended.action);
                    })) {
                throw std::invalid_argument("multiway child history action is not in the parent decision menu");
            }
            const auto expected_betting = parent_state.apply(
                appended.action.action, appended.action.target_street_contribution).snapshot();
            if (!same_betting_snapshot(expected_betting, child.betting)) {
                throw std::invalid_argument("multiway child betting snapshot does not match its parent action");
            }
            return;
        }
        case MultiwayPublicParentEdgeKind::BoardChance: {
            const auto parent_kind = parent_state.next_node_kind();
            if ((parent_kind != MultiwayNextNodeKind::BoardRunout &&
                 parent_kind != MultiwayNextNodeKind::StreetTransition) ||
                child.history != parent.history || !same_betting_snapshot(child.betting, parent.betting) ||
                child.board.size() <= parent.board.size() ||
                !board_contains(child.board, parent.board) ||
                !are_valid_and_distinct_cards(child.board.data(), child.board.size())) {
                throw std::invalid_argument("multiway child board chance does not match its parent state");
            }
            if (!matches_added_board_cards(parent.board, child.board, child.incoming_edge.dealt_cards) ||
                child.incoming_edge.dealt_card != child.incoming_edge.dealt_cards.front() ||
                !std::is_sorted(child.incoming_edge.dealt_cards.begin(), child.incoming_edge.dealt_cards.end())) {
                throw std::invalid_argument("multiway child board chance has an invalid dealt-card descriptor");
            }
            const auto chance_only = parent_kind == MultiwayNextNodeKind::BoardRunout;
            if (child.board_runout.remaining_board_cards != 5U - child.board.size() ||
                child.board_runout.chance_only_runout != chance_only) {
                throw std::invalid_argument("multiway child board chance has inconsistent runout metadata");
            }
            return;
        }
        case MultiwayPublicParentEdgeKind::StreetTransition: {
            if (!parent_state.requires_street_transition() || child.history != parent.history ||
                child.board != child.incoming_edge.transition_board ||
                child.board.size() != expected_board_card_count(
                    static_cast<Street>(static_cast<std::uint8_t>(parent.betting.street) + 1U)) ||
                child.board.size() < parent.board.size() ||
                !board_contains(child.board, parent.board) ||
                child.board_runout.remaining_board_cards != 5U - child.board.size() ||
                child.board_runout.chance_only_runout) {
                throw std::invalid_argument("multiway child street transition has inconsistent public state");
            }
            const auto next_street = static_cast<Street>(static_cast<std::uint8_t>(parent.betting.street) + 1U);
            const auto expected_betting = parent_state.begin_next_street(
                next_street, root.next_street_first_seat).snapshot();
            if (!same_betting_snapshot(expected_betting, child.betting)) {
                throw std::invalid_argument("multiway child street transition does not match root seat semantics");
            }
            return;
        }
        case MultiwayPublicParentEdgeKind::None:
            break;
    }
    throw std::invalid_argument("multiway child public state requires a typed parent edge");
}

bool delta_less(const MultiwayWorkerDelta& left, const MultiwayWorkerDelta& right) noexcept {
    if (left.infoset.seat != right.infoset.seat) return left.infoset.seat < right.infoset.seat;
    if (left.infoset.public_state != right.infoset.public_state) {
        return left.infoset.public_state < right.infoset.public_state;
    }
    if (left.action != right.action) return left.action < right.action;
    if (left.bucket != right.bucket) return left.bucket < right.bucket;
    return left.trajectory_id < right.trajectory_id;
}

bool delta_is_finite(const MultiwayWorkerDelta& delta) noexcept {
    return std::isfinite(delta.regret) && std::isfinite(delta.strategy_sum);
}

void hash_u64(std::uint64_t value, std::uint64_t& hash) noexcept {
    for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
        hash ^= static_cast<std::uint8_t>(value >> (byte * 8U));
        hash *= 1099511628211ULL;
    }
}

std::uint64_t delta_stream_fingerprint(
    const std::vector<MultiwayWorkerDelta>& deltas) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u64(MULTIWAY_MERGE_ORDER_VERSION, hash);
    hash_u64(deltas.size(), hash);
    for (const auto& delta : deltas) {
        hash_u64(delta.infoset.public_state.value, hash);
        hash_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(delta.infoset.seat)), hash);
        hash_u64(delta.bucket, hash);
        hash_u64(delta.action, hash);
        hash_u64(delta.trajectory_id, hash);
        std::uint64_t regret_bits = 0U;
        std::uint64_t strategy_bits = 0U;
        static_assert(sizeof(regret_bits) == sizeof(delta.regret));
        std::memcpy(&regret_bits, &delta.regret, sizeof(regret_bits));
        std::memcpy(&strategy_bits, &delta.strategy_sum, sizeof(strategy_bits));
        hash_u64(regret_bits, hash);
        hash_u64(strategy_bits, hash);
    }
    return hash == 0U ? 1U : hash;
}

void validate_public_state_descriptor(const MultiwayPublicStateDescriptor& state) {
    state.betting.validate();
    if (state.id.value == 0 || state.canonical_history_id == 0 ||
        !valid_public_parent_edge_kind(state.incoming_edge.kind)) {
        throw std::invalid_argument("multiway public state requires stable identities");
    }
    const auto known_board_cards = expected_board_card_count(state.betting.street);
    const auto betting_state = MultiwayState::from_snapshot(state.betting);
    const auto valid_board_size = state.board_runout.chance_only_runout
        ? state.board.size() >= known_board_cards && state.board.size() <= 5U
        : betting_state.requires_street_transition()
            ? state.board.size() >= known_board_cards &&
                  state.board.size() <= expected_board_card_count(
                      static_cast<Street>(static_cast<std::uint8_t>(state.betting.street) + 1U))
        : state.board.size() == known_board_cards;
    if (!valid_board_size || !std::is_sorted(state.board.begin(), state.board.end()) ||
        state.board_runout.remaining_board_cards != 5U - state.board.size() ||
        !are_valid_and_distinct_cards(state.board.data(), state.board.size())) {
        throw std::invalid_argument("multiway public state has an inconsistent board/runout state");
    }

    if (state.board_runout.chance_only_runout != betting_state.requires_board_runout()) {
        throw std::invalid_argument("multiway public state has an inconsistent chance-only runout flag");
    }
    const auto available_actions = betting_state.legal_actions();
    std::vector<bool> covered_actions(available_actions.size(), false);
    const auto expected_menu_id = MultiwayPublicBuilder::stable_action_menu_id(state.legal_actions);
    for (std::size_t index = 0; index < state.legal_actions.size(); ++index) {
        const auto& action = state.legal_actions[index];
        if (!valid_action(action.action) || action.action_index != index || action.action_menu_id == 0 ||
            action.target_street_contribution < 0 ||
            action.action_menu_id != expected_menu_id ||
            (index != 0 &&
             (static_cast<std::uint8_t>(action.action) <
                  static_cast<std::uint8_t>(state.legal_actions[index - 1U].action) ||
              (action.action == state.legal_actions[index - 1U].action &&
               action.target_street_contribution <=
                   state.legal_actions[index - 1U].target_street_contribution)))) {
            throw std::invalid_argument("multiway public state action menu is not a stable descriptor sequence");
        }
        const auto available = std::find(available_actions.begin(), available_actions.end(), action.action);
        if (available == available_actions.end()) {
            throw std::invalid_argument("multiway public state action is unavailable in its betting snapshot");
        }
        covered_actions[static_cast<std::size_t>(available - available_actions.begin())] = true;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.legal_actions[prior].action == action.action &&
                state.legal_actions[prior].target_street_contribution == action.target_street_contribution) {
                throw std::invalid_argument("multiway public state action menu contains a duplicate target");
            }
        }
        try {
            const auto successor = betting_state.apply(action.action, action.target_street_contribution);
            const auto acting_seat = static_cast<std::size_t>(betting_state.current_player());
            if (successor.street_contributions()[acting_seat] != action.target_street_contribution) {
                throw std::invalid_argument("action descriptor does not encode its exact resulting contribution");
            }
        } catch (const std::exception&) {
            throw std::invalid_argument("multiway public state action descriptor is not executable");
        }
    }
    for (const auto covered : covered_actions) {
        if (!covered) throw std::invalid_argument("multiway public state action menu omits a legal action kind");
    }
    const auto seat_count = state.betting.stacks.size();
    for (const auto& entry : state.history) {
        if (entry.actor < 0 || static_cast<std::size_t>(entry.actor) >= seat_count ||
            !valid_action(entry.action.action) || entry.action.action_index != 0U || entry.action.action_menu_id == 0 ||
            entry.action.target_street_contribution < 0) {
            throw std::invalid_argument("multiway public state history contains an invalid action descriptor");
        }
    }
    if (state.canonical_history_id != MultiwayPublicBuilder::stable_history_id(state.history) ||
        state.id.value != MultiwayPublicBuilder::stable_public_state_id(
            state.betting, state.board, state.history, state.legal_actions)) {
        throw std::invalid_argument("multiway public state descriptor does not match its schema-v2 fingerprint");
    }
}

std::size_t checked_value_count(const MultiwaySparseRowShape& shape) {
    if (shape.bucket_count == 0U || shape.action_count == 0U) {
        throw std::invalid_argument("multiway sparse row requires non-zero buckets and actions");
    }
    const auto actions = static_cast<std::size_t>(shape.action_count);
    if (static_cast<std::size_t>(shape.bucket_count) > std::numeric_limits<std::size_t>::max() / actions) {
        throw std::overflow_error("multiway sparse row value count overflows size_t");
    }
    return static_cast<std::size_t>(shape.bucket_count) * actions;
}

}  // namespace

void MultiwayRootSnapshot::validate() const {
    validate_public_state_descriptor(public_state);
    private_ranges.validate();
    rake_policy.validate();

    if (public_state.parent_id.value != 0 ||
        public_state.incoming_edge.kind != MultiwayPublicParentEdgeKind::None) {
        throw std::invalid_argument("multiway root must not carry a parent edge");
    }

    const auto seat_count = public_state.betting.stacks.size();
    if (seat_count < 2U || seat_count > 6U || private_ranges.ranges.size() != seat_count) {
        throw std::invalid_argument("multiway root requires matching two-through-six seat state and ranges");
    }
    if (action_abstraction_version == 0 || leaf_model_version == 0) {
        throw std::invalid_argument("multiway root requires stable non-zero public and version identities");
    }
    if (root_infoset.public_state != public_state.id || root_infoset.seat < 0 ||
        static_cast<std::size_t>(root_infoset.seat) >= seat_count ||
        root_infoset.seat != public_state.betting.current_player) {
        throw std::invalid_argument("multiway root infoset must identify the acting root seat");
    }
    if (seat_order.size() != seat_count || next_street_first_seat < 0 ||
        static_cast<std::size_t>(next_street_first_seat) >= seat_count || odd_chip_first_seat < 0 ||
        static_cast<std::size_t>(odd_chip_first_seat) >= seat_count || !valid_odd_chip_rule(odd_chip_rule)) {
        throw std::invalid_argument("multiway root has invalid seat or odd-chip order");
    }
    std::vector<bool> seen(seat_count, false);
    for (const auto seat : seat_order) {
        if (seat < 0 || static_cast<std::size_t>(seat) >= seat_count || seen[static_cast<std::size_t>(seat)]) {
            throw std::invalid_argument("multiway root seat order must be a permutation");
        }
        seen[static_cast<std::size_t>(seat)] = true;
    }
    if (next_street_first_seat != seat_order.front()) {
        throw std::invalid_argument("multiway root next-street seat must lead the canonical seat order");
    }
    for (std::size_t index = 1; index < seat_order.size(); ++index) {
        const auto expected = static_cast<PlayerId>((seat_order[index - 1U] + 1) % seat_count);
        if (seat_order[index] != expected) {
            throw std::invalid_argument("multiway root seat order must follow the canonical seat cycle");
        }
    }
    if (!same_card_set(public_state.board, private_ranges.board) ||
        (value_units != MultiwayValueUnits::Chips && value_units != MultiwayValueUnits::BigBlinds)) {
        throw std::invalid_argument("multiway root has inconsistent board or value units");
    }
}

void MultiwaySolverLimits::validate() const {
    if (worker_count == 0U || trajectories_per_batch == 0U || max_public_states == 0U ||
        max_sparse_rows == 0U || max_sparse_values == 0U || max_worker_delta_entries == 0U) {
        throw std::invalid_argument("multiway solver limits require non-zero bounded capacities");
    }
    if (max_worker_delta_entries >
        std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(worker_count)) {
        throw std::overflow_error("multiway aggregate worker delta capacity overflows size_t");
    }
    if (run_mode != MultiwayRunMode::Deterministic) {
        throw std::invalid_argument("multiway solver supports only deterministic run mode");
    }
}

MultiwaySolveRequest::MultiwaySolveRequest(
    MultiwayRootSnapshot root,
    MultiwayCFRConfig cfr_config,
    MultiwaySolverLimits limits)
    : root_(std::move(root)),
      cfr_config_(cfr_config),
      limits_(limits),
      private_range_feasibility_(preflight_multiway_private_range_feasibility(root_.private_ranges)) {
    root_.validate();
    if (private_range_feasibility_.status == MultiwayPrivateRangeFeasibilityStatus::Infeasible) {
        throw std::invalid_argument("multiway solve request has no compatible private deal");
    }
    if (private_range_feasibility_.status == MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted) {
        throw std::runtime_error("multiway solve request private-range feasibility preflight exhausted its budget");
    }
    cfr_config_.validate();
    limits_.validate();
    if (cfr_config_.algorithm != MultiwayCFRAlgorithm::ExternalSamplingMCCFR ||
        cfr_config_.player_count != root_.seat_order.size()) {
        throw std::invalid_argument("multiway boundary requires external sampling with the root seat count");
    }
    compiled_private_ranges_.emplace(root_.private_ranges);
}

std::size_t MultiwaySparseRowMetadata::value_count() const noexcept {
    return static_cast<std::size_t>(shape.bucket_count) * static_cast<std::size_t>(shape.action_count);
}

MultiwaySparseRowStorage::MultiwaySparseRowStorage(std::size_t max_rows, std::size_t max_values)
    : max_rows_(max_rows), max_values_(max_values) {}

bool MultiwaySparseRowStorage::has_row(MultiwayInfosetId infoset) const noexcept {
    return metadata(infoset) != nullptr;
}

const MultiwaySparseRowMetadata* MultiwaySparseRowStorage::metadata(MultiwayInfosetId infoset) const noexcept {
    const auto found = std::lower_bound(
        metadata_.begin(), metadata_.end(), infoset,
        [](const MultiwaySparseRowMetadata& row, MultiwayInfosetId key) {
            return row.shape.infoset < key;
        });
    if (found == metadata_.end() || !(found->shape.infoset == infoset)) return nullptr;
    return &*found;
}

std::vector<Probability> MultiwaySparseRowStorage::average_strategy(
    MultiwayInfosetId infoset,
    std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) {
        throw std::out_of_range("multiway average strategy row or bucket is unavailable");
    }

    std::vector<Probability> result(row->shape.action_count, 0.0);
    double total = 0.0;
    for (std::size_t action = 0; action < result.size(); ++action) {
        const auto index = row->strategy_sum_offset + action * row->shape.bucket_count + bucket;
        result[action] = strategy_sum_[index] > 0.0 ? strategy_sum_[index] : 0.0;
        total += result[action];
    }
    if (total == 0.0) {
        const auto uniform = 1.0 / static_cast<double>(result.size());
        std::fill(result.begin(), result.end(), uniform);
        return result;
    }
    for (auto& probability : result) probability /= total;
    return result;
}

std::vector<Probability> MultiwaySparseRowStorage::regret_matched_strategy(
    MultiwayInfosetId infoset,
    std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) {
        throw std::out_of_range("multiway regret row or bucket is unavailable");
    }
    std::vector<Probability> strategy(row->shape.action_count, 0.0);
    regret_matched_strategy_into(infoset, bucket, strategy.data(), strategy.size());
    return strategy;
}

void MultiwaySparseRowStorage::regret_matched_strategy_into(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    Probability* output,
    std::size_t output_size) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count || output == nullptr ||
        output_size != row->shape.action_count) {
        throw std::out_of_range("multiway regret strategy output does not match its row");
    }
    multiway_regret_matching_action_major_into(
        regret_.data() + row->regret_offset + bucket,
        (output_size - 1U) * row->shape.bucket_count + 1U,
        output_size,
        row->shape.bucket_count,
        output);
}

std::vector<double> MultiwaySparseRowStorage::strategy_sums(
    MultiwayInfosetId infoset,
    std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) {
        throw std::out_of_range("multiway strategy sum row or bucket is unavailable");
    }
    std::vector<double> result(row->shape.action_count, 0.0);
    for (std::size_t action = 0; action < result.size(); ++action) {
        result[action] = strategy_sum_[row->strategy_sum_offset + action * row->shape.bucket_count + bucket];
    }
    return result;
}

void MultiwaySparseRowStorage::scale_regrets(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0) {
        throw std::invalid_argument("multiway regret discount must be finite and in (0, 1]");
    }
    for (auto& regret : regret_) regret *= factor;
}

std::size_t MultiwaySparseRowStorage::prune_negative_regrets() noexcept {
    std::size_t pruned = 0;
    for (auto& regret : regret_) {
        if (regret < 0.0) {
            regret = 0.0;
            ++pruned;
        }
    }
    return pruned;
}

void MultiwaySparseRowStorage::admit_row(const MultiwaySparseRowShape& shape) {
    if (shape.infoset.public_state.value == 0 || shape.infoset.seat < 0) {
        throw std::invalid_argument("multiway sparse row requires a stable per-seat infoset id");
    }
    if (const auto* existing = metadata(shape.infoset)) {
        if (existing->shape.bucket_count != shape.bucket_count ||
            existing->shape.action_count != shape.action_count) {
            throw std::invalid_argument("multiway sparse row infoset was admitted with a conflicting shape");
        }
        return;
    }
    const auto values = checked_value_count(shape);
    if (metadata_.size() >= max_rows_ || values > max_values_ - regret_.size()) {
        throw std::length_error("multiway sparse row admission exceeds configured capacity");
    }

    MultiwaySparseRowMetadata row;
    row.shape = shape;
    row.regret_offset = regret_.size();
    row.strategy_sum_offset = strategy_sum_.size();
    const auto insertion = std::lower_bound(
        metadata_.begin(), metadata_.end(), shape.infoset,
        [](const MultiwaySparseRowMetadata& existing, MultiwayInfosetId key) {
            return existing.shape.infoset < key;
        });
    regret_.resize(regret_.size() + values, 0.0);
    strategy_sum_.resize(strategy_sum_.size() + values, 0.0);
    metadata_.insert(insertion, row);
}

void MultiwaySparseRowStorage::apply_delta(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint8_t action,
    double regret,
    double strategy_sum) {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count ||
        !std::isfinite(regret) || !std::isfinite(strategy_sum)) {
        throw std::invalid_argument("multiway delta does not match an admitted finite row cell");
    }
    const auto index = row->regret_offset + static_cast<std::size_t>(action) * row->shape.bucket_count + bucket;
    const auto updated_regret = static_cast<double>(regret_[index]) + regret;
    const auto updated_strategy = static_cast<double>(strategy_sum_[index]) + strategy_sum;
    if (!std::isfinite(updated_regret) || !std::isfinite(updated_strategy) ||
        (regret != 0.0 && updated_regret == regret_[index]) ||
        (strategy_sum != 0.0 && updated_strategy == strategy_sum_[index])) {
        throw std::overflow_error("multiway sparse delta would lose a nonzero Float64 update");
    }
    regret_[index] = updated_regret;
    strategy_sum_[index] = updated_strategy;
}

MultiwayWorkerDeltaStream::MultiwayWorkerDeltaStream(std::size_t worker_index, std::size_t capacity)
    : worker_index_(worker_index), capacity_(capacity) {
    deltas_.reserve(capacity_);
}

bool MultiwayWorkerDeltaStream::try_append(const MultiwayWorkerDelta& delta) noexcept {
    if (deltas_.size() >= capacity_ || !delta_is_finite(delta)) return false;
    deltas_.push_back(delta);
    return true;
}

void MultiwayWorkerDeltaStream::rewind(std::size_t size) noexcept {
    if (size < deltas_.size()) deltas_.resize(size);
}

void MultiwayWorkerDeltaStream::sort_fixed_order() noexcept {
    std::sort(deltas_.begin(), deltas_.end(), delta_less);
}

bool MultiwayWorkerDeltaStream::is_fixed_order() const noexcept {
    return std::is_sorted(deltas_.begin(), deltas_.end(), delta_less);
}

MultiwaySolveResult::MultiwaySolveResult(
    MultiwayRootPolicy root_policy,
    std::vector<Value> root_values,
    MultiwaySolveDiagnostics diagnostics)
    : root_policy_(std::move(root_policy)),
      root_values_(std::move(root_values)),
      diagnostics_(std::move(diagnostics)) {}

MultiwaySolverCoordinator::MultiwaySolverCoordinator(const MultiwaySolveRequest& request)
    : request_(request),
      storage_(request.limits().max_sparse_rows, request.limits().max_sparse_values) {
    public_states_.reserve(request.limits().max_public_states);
    merge_stream_views_.reserve(request.limits().worker_count);
    const auto merge_capacity = static_cast<std::size_t>(request.limits().worker_count) *
        request.limits().max_worker_delta_entries;
    merge_deltas_.reserve(merge_capacity);
    pending_merge_cells_.reserve(merge_capacity);
    admit_public_state(request_.root().public_state);
}

void MultiwaySolverCoordinator::admit_public_state(const MultiwayPublicStateDescriptor& state) {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    validate_public_state_descriptor(state);
    const auto existing = public_state(state.id);
    if (existing != nullptr) {
        if (!same_public_state_descriptor(*existing, state)) {
            throw std::invalid_argument("multiway public state id was admitted with conflicting data");
        }
        return;
    }
    if (public_states_.size() >= request_.limits().max_public_states) {
        throw std::length_error("multiway public-state admission exceeds configured capacity");
    }
    if (state.parent_id.value != 0) {
        const auto* parent = public_state(state.parent_id);
        if (parent == nullptr) {
            throw std::invalid_argument("multiway public state parent must be admitted first");
        }
        validate_public_state_child_transition(request_.root(), *parent, state);
    } else if (state.id != request_.root().public_state.id) {
        throw std::invalid_argument("multiway coordinator admits only its immutable root without a parent edge");
    }
    public_states_.push_back(state);
    ++diagnostics_.public_states_admitted;
}

void MultiwaySolverCoordinator::admit_infoset_row(const MultiwaySparseRowShape& shape) {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    if (!storage_.has_row(shape.infoset) &&
        storage_.row_count() >= request_.limits().max_sparse_rows) {
        throw std::length_error("multiway sparse row admission exceeds configured capacity");
    }
    const auto* state = public_state(shape.infoset.public_state);
    if (state == nullptr || shape.infoset.seat < 0 ||
        static_cast<std::size_t>(shape.infoset.seat) >= request_.root().seat_order.size()) {
        throw std::invalid_argument("multiway row must belong to an admitted per-seat public infoset");
    }
    const auto betting_state = MultiwayState::from_snapshot(state->betting);
    if (betting_state.next_node_kind() != MultiwayNextNodeKind::BettingDecision ||
        shape.infoset.seat != state->betting.current_player) {
        throw std::invalid_argument("multiway row must belong to its public decision owner");
    }
    if (shape.action_count != state->legal_actions.size()) {
        throw std::invalid_argument("multiway row action count must match its public action menu");
    }
    if (shape.infoset == request_.root().root_infoset &&
        request_.root().root_bucket >= shape.bucket_count) {
        throw std::invalid_argument("multiway root bucket must fit its admitted sparse row");
    }
    const auto existed = storage_.has_row(shape.infoset);
    storage_.admit_row(shape);
    if (!existed) ++diagnostics_.sparse_rows_admitted;
}

void MultiwaySolverCoordinator::regret_matched_strategy_into(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    Probability* output,
    std::size_t output_size) const {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    storage_.regret_matched_strategy_into(infoset, bucket, output, output_size);
}

void MultiwaySolverCoordinator::merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams) {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    if (streams.size() != request_.limits().worker_count) {
        throw std::invalid_argument("multiway merge requires one stream for every configured worker");
    }
    merge_stream_views_.clear();
    for (const auto& stream : streams) merge_stream_views_.push_back(&stream);
    merge_worker_streams_locked(merge_stream_views_);
}

void MultiwaySolverCoordinator::merge_worker_streams(
    const std::vector<const MultiwayWorkerDeltaStream*>& streams) {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    merge_worker_streams_locked(streams);
}

void MultiwaySolverCoordinator::merge_worker_streams_locked(
    const std::vector<const MultiwayWorkerDeltaStream*>& streams) {
    if (streams.size() != request_.limits().worker_count) {
        throw std::invalid_argument("multiway merge requires one stream for every configured worker");
    }

    std::size_t delta_count = 0;
    for (std::size_t worker = 0; worker < streams.size(); ++worker) {
        const auto* stream = streams[worker];
        if (stream == nullptr || stream->worker_index() != worker ||
            stream->size() > request_.limits().max_worker_delta_entries ||
            !stream->is_fixed_order()) {
            throw std::invalid_argument("multiway worker streams must be bounded and in fixed worker/row/action order");
        }
        if (stream->size() > std::numeric_limits<std::size_t>::max() - delta_count) {
            throw std::overflow_error("multiway worker delta count overflows size_t");
        }
        delta_count += stream->size();
    }

    merge_deltas_.clear();
    pending_merge_cells_.clear();
    if (delta_count > merge_deltas_.capacity() || delta_count > pending_merge_cells_.capacity()) {
        throw std::logic_error("multiway merge scratch capacity changed after coordinator construction");
    }
    for (const auto* stream : streams) {
        merge_deltas_.insert(
            merge_deltas_.end(), stream->deltas().begin(), stream->deltas().end());
    }
    std::sort(merge_deltas_.begin(), merge_deltas_.end(), delta_less);
    const auto stream_fingerprint = delta_stream_fingerprint(merge_deltas_);

    for (std::size_t begin = 0; begin < merge_deltas_.size();) {
        const auto& first = merge_deltas_[begin];
        const auto* row = storage_.metadata(first.infoset);
        if (row == nullptr || first.bucket >= row->shape.bucket_count || first.action >= row->shape.action_count) {
            throw std::invalid_argument("multiway delta does not match an admitted row cell");
        }
        const auto index = row->regret_offset +
            static_cast<std::size_t>(first.action) * row->shape.bucket_count + first.bucket;
        double regret = storage_.regret_[index];
        double strategy_sum = storage_.strategy_sum_[index];
        std::uint64_t previous_trajectory = 0;
        bool have_trajectory = false;
        std::size_t end = begin;
        for (; end < merge_deltas_.size(); ++end) {
            const auto& delta = merge_deltas_[end];
            if (!(delta.infoset == first.infoset) || delta.bucket != first.bucket || delta.action != first.action) break;
            if (!delta_is_finite(delta)) {
                throw std::invalid_argument("multiway delta must be finite");
            }
            if (have_trajectory && delta.trajectory_id == previous_trajectory) {
                throw std::invalid_argument("multiway merge received duplicate trajectory updates for one cell");
            }
            previous_trajectory = delta.trajectory_id;
            have_trajectory = true;
            const auto next_regret = regret + delta.regret;
            const auto next_strategy_sum = strategy_sum + delta.strategy_sum;
            if (!std::isfinite(next_regret) || !std::isfinite(next_strategy_sum) ||
                (delta.regret != 0.0 && next_regret == regret) ||
                (delta.strategy_sum != 0.0 && next_strategy_sum == strategy_sum)) {
                throw std::overflow_error("multiway sparse merge would lose a nonzero Float64 update");
            }
            regret = next_regret;
            strategy_sum = next_strategy_sum;
        }
        pending_merge_cells_.push_back({index, regret, strategy_sum});
        begin = end;
    }

    for (const auto& cell : pending_merge_cells_) {
        storage_.regret_[cell.index] = cell.regret;
        storage_.strategy_sum_[cell.index] = cell.strategy_sum;
    }
    diagnostics_.worker_delta_entries_merged += delta_count;
    diagnostics_.last_merged_stream_fingerprint = stream_fingerprint;
}

MultiwayRootPolicy MultiwaySolverCoordinator::export_root_policy() const {
    const auto& root = request_.root();
    MultiwayRootPolicy result;
    result.public_state = root.public_state.id;
    result.infoset = root.root_infoset;
    result.bucket = root.root_bucket;

    std::vector<Probability> probabilities;
    if (storage_.has_row(root.root_infoset)) {
        probabilities = storage_.average_strategy(root.root_infoset, root.root_bucket);
    } else {
        const auto uniform = 1.0 / static_cast<double>(root.public_state.legal_actions.size());
        probabilities.assign(root.public_state.legal_actions.size(), uniform);
    }
    result.actions.reserve(probabilities.size());
    for (std::size_t action = 0; action < probabilities.size(); ++action) {
        result.actions.push_back({root.public_state.legal_actions[action], probabilities[action]});
    }
    return result;
}

MultiwayRootPolicy MultiwaySolverCoordinator::export_root_current_policy() const {
    const auto& root = request_.root();
    MultiwayRootPolicy result;
    result.public_state = root.public_state.id;
    result.infoset = root.root_infoset;
    result.bucket = root.root_bucket;
    std::vector<Probability> probabilities;
    if (storage_.has_row(root.root_infoset)) {
        probabilities = storage_.regret_matched_strategy(root.root_infoset, root.root_bucket);
    } else {
        probabilities.assign(root.public_state.legal_actions.size(),
            1.0 / static_cast<double>(root.public_state.legal_actions.size()));
    }
    result.actions.reserve(probabilities.size());
    for (std::size_t action = 0; action < probabilities.size(); ++action) {
        result.actions.push_back({root.public_state.legal_actions[action], probabilities[action]});
    }
    return result;
}

std::vector<double> MultiwaySolverCoordinator::export_root_strategy_sums() const {
    const auto& root = request_.root();
    if (!storage_.has_row(root.root_infoset)) {
        return std::vector<double>(root.public_state.legal_actions.size(), 0.0);
    }
    return storage_.strategy_sums(root.root_infoset, root.root_bucket);
}

MultiwayRootPolicy MultiwaySolverCoordinator::export_root_policy_since(
    const std::vector<double>& baseline_strategy_sums) const {
    const auto& root = request_.root();
    const auto sums = export_root_strategy_sums();
    if (baseline_strategy_sums.size() != sums.size()) {
        throw std::invalid_argument("multiway root policy baseline has an incompatible action count");
    }
    MultiwayRootPolicy result;
    result.public_state = root.public_state.id;
    result.infoset = root.root_infoset;
    result.bucket = root.root_bucket;
    double total = 0.0;
    std::vector<double> values(sums.size(), 0.0);
    for (std::size_t action = 0; action < sums.size(); ++action) {
        values[action] = sums[action] - baseline_strategy_sums[action];
        if (!std::isfinite(values[action]) || values[action] < -1e-12) {
            throw std::logic_error("multiway root policy baseline exceeds accumulated strategy mass");
        }
        if (values[action] < 0.0) values[action] = 0.0;
        total += values[action];
    }
    if (total == 0.0) {
        values.assign(values.size(), 1.0 / static_cast<double>(values.size()));
    } else {
        for (auto& value : values) value /= total;
    }
    result.actions.reserve(values.size());
    for (std::size_t action = 0; action < values.size(); ++action) {
        result.actions.push_back({root.public_state.legal_actions[action], values[action]});
    }
    return result;
}

void MultiwaySolverCoordinator::scale_regrets(double factor) {
    storage_.scale_regrets(factor);
}

std::size_t MultiwaySolverCoordinator::prune_negative_regrets() noexcept {
    return storage_.prune_negative_regrets();
}

const MultiwayPublicStateDescriptor* MultiwaySolverCoordinator::public_state(
    MultiwayPublicStateId id) const noexcept {
    const auto found = std::find_if(
        public_states_.begin(), public_states_.end(),
        [id](const MultiwayPublicStateDescriptor& state) { return state.id == id; });
    return found == public_states_.end() ? nullptr : &*found;
}

const MultiwayPublicStateDescriptor* MultiwaySolverCoordinator::find_public_state(
    MultiwayPublicStateId id) const noexcept {
    return public_state(id);
}

MultiwayCoordinatorCheckpoint MultiwaySolverCoordinator::checkpoint() const {
    std::lock_guard<std::mutex> lock(traversal_mutex_);
    MultiwayCoordinatorCheckpoint result;
    result.public_states = public_states_;
    result.storage.shapes.reserve(storage_.metadata_.size());
    result.storage.regrets.reserve(storage_.regret_.size());
    result.storage.strategy_sums.reserve(storage_.strategy_sum_.size());
    for (const auto& row : storage_.metadata_) {
        result.storage.shapes.push_back(row.shape);
        const auto values = row.value_count();
        result.storage.regrets.insert(
            result.storage.regrets.end(),
            storage_.regret_.begin() + static_cast<std::ptrdiff_t>(row.regret_offset),
            storage_.regret_.begin() + static_cast<std::ptrdiff_t>(row.regret_offset + values));
        result.storage.strategy_sums.insert(
            result.storage.strategy_sums.end(),
            storage_.strategy_sum_.begin() + static_cast<std::ptrdiff_t>(row.strategy_sum_offset),
            storage_.strategy_sum_.begin() + static_cast<std::ptrdiff_t>(row.strategy_sum_offset + values));
    }
    return result;
}

void MultiwaySolverCoordinator::restore_checkpoint(const MultiwayCoordinatorCheckpoint& checkpoint) {
    if (checkpoint.public_states.empty() ||
        !same_public_state_descriptor(checkpoint.public_states.front(), request_.root().public_state) ||
        checkpoint.storage.regrets.size() != checkpoint.storage.strategy_sums.size()) {
        throw std::invalid_argument("multiway coordinator checkpoint is incomplete");
    }
    std::size_t expected_values = 0U;
    for (const auto& shape : checkpoint.storage.shapes) {
        if (shape.bucket_count == 0U || shape.action_count == 0U ||
            expected_values > std::numeric_limits<std::size_t>::max() -
                static_cast<std::size_t>(shape.bucket_count) * shape.action_count) {
            throw std::invalid_argument("multiway coordinator checkpoint has invalid row shapes");
        }
        expected_values += static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
    }
    if (expected_values != checkpoint.storage.regrets.size()) {
        throw std::invalid_argument("multiway coordinator checkpoint row values do not match shapes");
    }
    for (const auto value : checkpoint.storage.regrets) {
        if (!std::isfinite(value)) throw std::invalid_argument("multiway coordinator checkpoint has non-finite regrets");
    }
    for (const auto value : checkpoint.storage.strategy_sums) {
        if (!std::isfinite(value) || value < 0.0) {
            throw std::invalid_argument("multiway coordinator checkpoint has invalid strategy sums");
        }
    }
    {
        std::lock_guard<std::mutex> lock(traversal_mutex_);
        public_states_.clear();
        storage_.metadata_.clear();
        storage_.regret_.clear();
        storage_.strategy_sum_.clear();
        diagnostics_ = {};
    }
    for (const auto& state : checkpoint.public_states) admit_public_state(state);
    for (const auto& shape : checkpoint.storage.shapes) admit_infoset_row(shape);
    {
        std::lock_guard<std::mutex> lock(traversal_mutex_);
        storage_.regret_ = checkpoint.storage.regrets;
        storage_.strategy_sum_ = checkpoint.storage.strategy_sums;
    }
}

}  // namespace texas::solver::multiway
