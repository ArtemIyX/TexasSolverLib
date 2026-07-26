#include "solver/multiway_solver.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace core {
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
    if (child.canonical_history_id == parent.canonical_history_id) {
        throw std::invalid_argument("multiway child public state must have a distinct history identity");
    }
    switch (child.incoming_edge.kind) {
        case MultiwayPublicParentEdgeKind::BettingAction: {
            if (child.history.size() != parent.history.size() + 1U ||
                !std::equal(parent.history.begin(), parent.history.end(), child.history.begin()) ||
                child.board != parent.board || child.board_runout != parent.board_runout) {
                throw std::invalid_argument("multiway child action state does not preserve parent public data");
            }
            const auto& appended = child.history.back();
            if (parent_state.next_node_kind() != MultiwayNextNodeKind::BettingDecision ||
                appended.actor != parent.betting.current_player ||
                appended.action != child.incoming_edge.action ||
                std::find(parent.legal_actions.begin(), parent.legal_actions.end(), appended.action) ==
                    parent.legal_actions.end()) {
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
                child.board.size() != parent.board.size() + 1U ||
                !std::equal(parent.board.begin(), parent.board.end(), child.board.begin()) ||
                child.board.back() != child.incoming_edge.dealt_card ||
                !are_valid_and_distinct_cards(child.board.data(), child.board.size())) {
                throw std::invalid_argument("multiway child board chance does not match its parent state");
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
                !std::equal(parent.board.begin(), parent.board.end(), child.board.begin()) ||
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
    if (!valid_board_size ||
        state.board_runout.remaining_board_cards != 5U - state.board.size() ||
        !are_valid_and_distinct_cards(state.board.data(), state.board.size())) {
        throw std::invalid_argument("multiway public state has an inconsistent board/runout state");
    }

    if (state.board_runout.chance_only_runout != betting_state.requires_board_runout()) {
        throw std::invalid_argument("multiway public state has an inconsistent chance-only runout flag");
    }
    const auto available_actions = betting_state.legal_actions();
    if (state.legal_actions.size() != available_actions.size()) {
        throw std::invalid_argument("multiway public state action menu does not match its betting snapshot");
    }
    for (std::size_t index = 0; index < state.legal_actions.size(); ++index) {
        const auto& action = state.legal_actions[index];
        if (!valid_action(action.action) || action.action_index != index || action.action_menu_id == 0 ||
            action.target_street_contribution < 0 ||
            action.action != available_actions[index] ||
            (index != 0 && action.action_menu_id != state.legal_actions.front().action_menu_id)) {
            throw std::invalid_argument("multiway public state action menu is not a stable descriptor sequence");
        }
        try {
            static_cast<void>(betting_state.apply(action.action, action.target_street_contribution));
        } catch (const std::exception&) {
            throw std::invalid_argument("multiway public state action descriptor is not executable");
        }
    }
    const auto seat_count = state.betting.stacks.size();
    for (const auto& entry : state.history) {
        if (entry.actor < 0 || static_cast<std::size_t>(entry.actor) >= seat_count ||
            !valid_action(entry.action.action) || entry.action.action_menu_id == 0 ||
            entry.action.target_street_contribution < 0) {
            throw std::invalid_argument("multiway public state history contains an invalid action descriptor");
        }
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
    if (!same_board(public_state.board, private_ranges.board) ||
        (value_units != MultiwayValueUnits::Chips && value_units != MultiwayValueUnits::BigBlinds)) {
        throw std::invalid_argument("multiway root has inconsistent board or value units");
    }
}

void MultiwaySolverLimits::validate() const {
    if (worker_count == 0U || trajectories_per_batch == 0U || max_public_states == 0U ||
        max_sparse_rows == 0U || max_sparse_values == 0U || max_worker_delta_entries == 0U) {
        throw std::invalid_argument("multiway solver limits require non-zero bounded capacities");
    }
}

MultiwaySolveRequest::MultiwaySolveRequest(
    MultiwayRootSnapshot root,
    MultiwayCFRConfig cfr_config,
    MultiwaySolverLimits limits)
    : root_(std::move(root)), cfr_config_(cfr_config), limits_(limits) {
    root_.validate();
    cfr_config_.validate();
    limits_.validate();
    if (cfr_config_.algorithm != MultiwayCFRAlgorithm::ExternalSamplingMCCFR ||
        cfr_config_.player_count != root_.seat_order.size()) {
        throw std::invalid_argument("multiway boundary requires external sampling with the root seat count");
    }
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
        result[action] = strategy_sum_[index] > 0.0F ? strategy_sum_[index] : 0.0;
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
    regret_.resize(regret_.size() + values, 0.0F);
    strategy_sum_.resize(strategy_sum_.size() + values, 0.0F);
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
        updated_regret > std::numeric_limits<float>::max() ||
        updated_regret < -std::numeric_limits<float>::max() ||
        updated_strategy > std::numeric_limits<float>::max() ||
        updated_strategy < -std::numeric_limits<float>::max()) {
        throw std::overflow_error("multiway sparse delta would overflow row storage");
    }
    regret_[index] = static_cast<float>(updated_regret);
    strategy_sum_[index] = static_cast<float>(updated_strategy);
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
    admit_public_state(request_.root().public_state);
}

void MultiwaySolverCoordinator::admit_public_state(const MultiwayPublicStateDescriptor& state) {
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

void MultiwaySolverCoordinator::merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams) {
    if (streams.size() != request_.limits().worker_count) {
        throw std::invalid_argument("multiway merge requires one stream for every configured worker");
    }

    std::size_t delta_count = 0;
    for (std::size_t worker = 0; worker < streams.size(); ++worker) {
        const auto& stream = streams[worker];
        if (stream.worker_index() != worker || stream.size() > request_.limits().max_worker_delta_entries ||
            !stream.is_fixed_order()) {
            throw std::invalid_argument("multiway worker streams must be bounded and in fixed worker/row/action order");
        }
        if (stream.size() > std::numeric_limits<std::size_t>::max() - delta_count) {
            throw std::overflow_error("multiway worker delta count overflows size_t");
        }
        delta_count += stream.size();
    }

    std::vector<MultiwayWorkerDelta> deltas;
    deltas.reserve(delta_count);
    for (const auto& stream : streams) {
        deltas.insert(deltas.end(), stream.deltas().begin(), stream.deltas().end());
    }
    std::sort(deltas.begin(), deltas.end(), delta_less);

    struct PendingCell {
        std::size_t index = 0;
        float regret = 0.0F;
        float strategy_sum = 0.0F;
    };
    std::vector<PendingCell> pending;
    pending.reserve(deltas.size());
    for (std::size_t begin = 0; begin < deltas.size();) {
        const auto& first = deltas[begin];
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
        for (; end < deltas.size(); ++end) {
            const auto& delta = deltas[end];
            if (!(delta.infoset == first.infoset) || delta.bucket != first.bucket || delta.action != first.action) break;
            if (!delta_is_finite(delta)) {
                throw std::invalid_argument("multiway delta must be finite");
            }
            if (have_trajectory && delta.trajectory_id == previous_trajectory) {
                throw std::invalid_argument("multiway merge received duplicate trajectory updates for one cell");
            }
            previous_trajectory = delta.trajectory_id;
            have_trajectory = true;
            regret += delta.regret;
            strategy_sum += delta.strategy_sum;
            if (!std::isfinite(regret) || !std::isfinite(strategy_sum) ||
                regret > std::numeric_limits<float>::max() || regret < -std::numeric_limits<float>::max() ||
                strategy_sum > std::numeric_limits<float>::max() || strategy_sum < -std::numeric_limits<float>::max()) {
                throw std::overflow_error("multiway sparse batch delta would overflow row storage");
            }
        }
        pending.push_back({index, static_cast<float>(regret), static_cast<float>(strategy_sum)});
        begin = end;
    }

    for (const auto& cell : pending) {
        storage_.regret_[cell.index] = cell.regret;
        storage_.strategy_sum_[cell.index] = cell.strategy_sum;
    }
    diagnostics_.worker_delta_entries_merged += delta_count;
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

const MultiwayPublicStateDescriptor* MultiwaySolverCoordinator::public_state(
    MultiwayPublicStateId id) const noexcept {
    const auto found = std::find_if(
        public_states_.begin(), public_states_.end(),
        [id](const MultiwayPublicStateDescriptor& state) { return state.id == id; });
    return found == public_states_.end() ? nullptr : &*found;
}

}  // namespace core
