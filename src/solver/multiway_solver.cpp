#include "solver/multiway_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

bool valid_value_units(MultiwayValueUnits units) noexcept {
    return units == MultiwayValueUnits::Chips ||
           units == MultiwayValueUnits::BigBlinds ||
           units == MultiwayValueUnits::PotFraction ||
           units == MultiwayValueUnits::NormalizedStackFraction;
}

bool valid_action(MultiwayAction action) noexcept {
    return action == MultiwayAction::Fold || action == MultiwayAction::Check ||
           action == MultiwayAction::Call || action == MultiwayAction::Bet ||
           action == MultiwayAction::Raise || action == MultiwayAction::AllIn;
}

bool same_board(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) noexcept {
    return left == right;
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
    if (state.id.value == 0 || state.canonical_history_id == 0 || state.legal_actions.empty()) {
        throw std::invalid_argument("multiway public state requires stable identities and an action menu");
    }
    for (std::size_t index = 0; index < state.legal_actions.size(); ++index) {
        const auto& action = state.legal_actions[index];
        if (!valid_action(action.action) || action.action_index != index || action.action_menu_id == 0 ||
            action.target_street_contribution < 0 ||
            (index != 0 && action.action_menu_id != state.legal_actions.front().action_menu_id)) {
            throw std::invalid_argument("multiway public state action menu is not a stable descriptor sequence");
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
    if (seat_order.size() != seat_count || odd_chip_first_seat < 0 ||
        static_cast<std::size_t>(odd_chip_first_seat) >= seat_count) {
        throw std::invalid_argument("multiway root has invalid seat or odd-chip order");
    }
    std::vector<bool> seen(seat_count, false);
    for (const auto seat : seat_order) {
        if (seat < 0 || static_cast<std::size_t>(seat) >= seat_count || seen[static_cast<std::size_t>(seat)]) {
            throw std::invalid_argument("multiway root seat order must be a permutation");
        }
        seen[static_cast<std::size_t>(seat)] = true;
    }
    if (!same_board(public_state.board, private_ranges.board) || !valid_value_units(value_units)) {
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
    if (has_row(shape.infoset)) return;
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
        if (existing->canonical_history_id != state.canonical_history_id ||
            existing->board != state.board || existing->history.size() != state.history.size()) {
            throw std::invalid_argument("multiway public state id was admitted with conflicting data");
        }
        return;
    }
    if (public_states_.size() >= request_.limits().max_public_states) {
        throw std::length_error("multiway public-state admission exceeds configured capacity");
    }
    if (state.parent_id.value != 0 && public_state(state.parent_id) == nullptr) {
        throw std::invalid_argument("multiway public state parent must be admitted first");
    }
    public_states_.push_back(state);
    ++diagnostics_.public_states_admitted;
}

void MultiwaySolverCoordinator::admit_infoset_row(const MultiwaySparseRowShape& shape) {
    if (public_state(shape.infoset.public_state) == nullptr || shape.infoset.seat < 0 ||
        static_cast<std::size_t>(shape.infoset.seat) >= request_.root().seat_order.size()) {
        throw std::invalid_argument("multiway row must belong to an admitted per-seat public infoset");
    }
    const auto existed = storage_.has_row(shape.infoset);
    storage_.admit_row(shape);
    if (!existed) ++diagnostics_.sparse_rows_admitted;
}

void MultiwaySolverCoordinator::merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams) {
    if (streams.size() != request_.limits().worker_count) {
        throw std::invalid_argument("multiway merge requires one stream for every configured worker");
    }
    for (std::size_t worker = 0; worker < streams.size(); ++worker) {
        const auto& stream = streams[worker];
        if (stream.worker_index() != worker || stream.size() > request_.limits().max_worker_delta_entries ||
            !stream.is_fixed_order()) {
            throw std::invalid_argument("multiway worker streams must be bounded and in fixed worker/row/action order");
        }
        for (const auto& delta : stream.deltas()) {
            storage_.apply_delta(
                delta.infoset, delta.bucket, delta.action, delta.regret, delta.strategy_sum);
            ++diagnostics_.worker_delta_entries_merged;
        }
    }
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
        if (probabilities.size() != root.public_state.legal_actions.size()) {
            throw std::logic_error("multiway root row shape does not match the immutable root action menu");
        }
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
