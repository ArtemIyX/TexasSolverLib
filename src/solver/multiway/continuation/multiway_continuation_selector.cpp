#include "solver/multiway/continuation/multiway_continuation_selector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

bool same_key(
    const MultiwayContinuationSelectionKey& left,
    const MultiwayContinuationSelectionKey& right) noexcept {
    return left.public_state == right.public_state && left.actor == right.actor &&
        left.street == right.street && left.future_bucket == right.future_bucket &&
        left.action_abstraction_version == right.action_abstraction_version &&
        left.leaf_model_version == right.leaf_model_version;
}

bool key_less(const MultiwayContinuationSelectionKey& left,
    const MultiwayContinuationSelectionKey& right) noexcept {
    if (left.public_state != right.public_state) return left.public_state < right.public_state;
    if (left.actor != right.actor) return left.actor < right.actor;
    if (left.street != right.street) return left.street < right.street;
    if (left.future_bucket != right.future_bucket) return left.future_bucket < right.future_bucket;
    if (left.action_abstraction_version != right.action_abstraction_version) {
        return left.action_abstraction_version < right.action_abstraction_version;
    }
    return left.leaf_model_version < right.leaf_model_version;
}

bool delta_less(const MultiwayContinuationDelta& left, const MultiwayContinuationDelta& right) noexcept {
    if (left.trajectory_id != right.trajectory_id) return left.trajectory_id < right.trajectory_id;
    return left.sequence < right.sequence;
}

bool delta_valid(const MultiwayContinuationDelta& delta) noexcept {
    if (!delta.key.valid()) return false;
    double total = 0.0;
    for (const auto probability : delta.mixture) {
        if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) return false;
        total += probability;
    }
    if (std::fabs(total - 1.0) > 1e-12) return false;
    return std::all_of(delta.values.begin(), delta.values.end(), [](double value) {
        return std::isfinite(value);
    });
}

}  // namespace

MultiwayContinuationDeltaStream::MultiwayContinuationDeltaStream(
    std::size_t worker_index,
    std::size_t capacity)
    : worker_index_(worker_index), capacity_(capacity) {
    deltas_.reserve(capacity_);
}

bool MultiwayContinuationDeltaStream::try_append(const MultiwayContinuationDelta& delta) noexcept {
    if (deltas_.size() >= capacity_ || !delta_valid(delta)) return false;
    deltas_.push_back(delta);
    return true;
}

void MultiwayContinuationDeltaStream::rewind(std::size_t size) noexcept {
    if (size < deltas_.size()) deltas_.resize(size);
}

void MultiwayContinuationDeltaStream::sort_fixed_order() noexcept {
    std::sort(deltas_.begin(), deltas_.end(), delta_less);
}

bool MultiwayContinuationDeltaStream::is_fixed_order() const noexcept {
    return std::is_sorted(deltas_.begin(), deltas_.end(), delta_less);
}

bool MultiwayContinuationSelectionKey::valid() const noexcept {
    return public_state.value != 0U && actor >= 0 && street >= Street::Flop &&
           street <= Street::River && action_abstraction_version != 0U &&
           leaf_model_version != 0U;
}

bool MultiwayFixedContinuationSelector::valid() const noexcept {
    return is_valid_multiway_continuation_policy(policy_);
}

std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>
MultiwayFixedContinuationSelector::strategy(const MultiwayContinuationSelectionKey& key) const {
    std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> result{};
    if (!valid() || !key.valid()) {
        result[0] = 1.0;
        return result;
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = std::lower_bound(rows_.begin(), rows_.end(), key,
        [](const Row& row, const MultiwayContinuationSelectionKey& value) {
            return key_less(row.key, value);
        });
    if (found != rows_.end() && same_key(found->key, key)) {
        double total = 0.0;
        for (std::size_t index = 0U; index < found->regrets.size(); ++index) {
            const auto regret = std::max(0.0, found->regrets[index]);
            total += regret;
        }
        if (total > 0.0 && std::isfinite(total)) {
            for (std::size_t index = 0U; index < result.size(); ++index) {
                result[index] = std::max(0.0, found->regrets[index]) / total;
            }
            return result;
        }
    }
    result[static_cast<std::size_t>(policy_)] = 1.0;
    return result;
}

MultiwayContinuationPolicyKind MultiwayFixedContinuationSelector::select(
    const MultiwayContinuationSelectionKey& key) const {
    const auto mixture = strategy(key);
    std::size_t best = 0U;
    for (std::size_t index = 1U; index < mixture.size(); ++index) {
        if (mixture[index] > mixture[best]) best = index;
    }
    return MULTIWAY_FIXED_CONTINUATION_POLICIES[best];
}

void MultiwayFixedContinuationSelector::update_regrets(
    const MultiwayContinuationSelectionKey& key,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values) const {
    update_regrets_weighted(key, mixture, values, 1.0);
}

void MultiwayFixedContinuationSelector::update_regrets_weighted(
    const MultiwayContinuationSelectionKey& key,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values,
    const double importance_weight) const {
    if (!std::isfinite(importance_weight) || importance_weight < 0.0)
        throw std::invalid_argument("multiway continuation importance weight is invalid");
    if (!key.valid()) throw std::invalid_argument("multiway continuation row key is invalid");
    double mixture_total = 0.0;
    for (const auto probability : mixture) {
        if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
            throw std::invalid_argument("multiway continuation mixture is invalid");
        }
        mixture_total += probability;
    }
    if (std::fabs(mixture_total - 1.0) > 1e-12) {
        throw std::invalid_argument("multiway continuation mixture is not normalized");
    }
    for (const auto value : values) {
        if (!std::isfinite(value)) throw std::invalid_argument("multiway continuation value is non-finite");
    }
    double node_value = 0.0;
    for (std::size_t index = 0U; index < values.size(); ++index) node_value += mixture[index] * values[index];
    if (!std::isfinite(node_value)) throw std::overflow_error("multiway continuation value is non-finite");
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto found = std::lower_bound(rows_.begin(), rows_.end(), key,
        [](const Row& row, const MultiwayContinuationSelectionKey& value) {
            return key_less(row.key, value);
        });
    auto found_index = static_cast<std::size_t>(found - rows_.begin());
    if (found == rows_.end() || !same_key(found->key, key)) {
        rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(found_index), Row{key, {}});
    }
    auto& row = rows_[found_index];
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto next = row.regrets[index] + (values[index] - node_value) * importance_weight;
        if (!std::isfinite(next)) throw std::overflow_error("multiway continuation regret is non-finite");
        row.regrets[index] = next;
    }
}

void MultiwayFixedContinuationSelector::set_regrets(
    const MultiwayContinuationSelectionKey& key,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& regrets) {
    if (!key.valid()) throw std::invalid_argument("multiway continuation row key is invalid");
    for (const auto regret : regrets) {
        if (!std::isfinite(regret)) throw std::invalid_argument("multiway continuation regret is non-finite");
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto found = std::lower_bound(rows_.begin(), rows_.end(), key,
        [](const Row& row, const MultiwayContinuationSelectionKey& value) {
            return key_less(row.key, value);
        });
    if (found == rows_.end() || !same_key(found->key, key)) {
        rows_.insert(found, {key, regrets});
    } else {
        found->regrets = regrets;
    }
}

void MultiwayFixedContinuationSelector::merge_worker_streams(
    const std::vector<const MultiwayContinuationDeltaStream*>& streams) const {
    std::size_t count = 0U;
    for (std::size_t worker = 0U; worker < streams.size(); ++worker) {
        const auto* stream = streams[worker];
        if (stream == nullptr || stream->worker_index() != worker || !stream->is_fixed_order()) {
            throw std::invalid_argument("multiway continuation streams must be in fixed worker order");
        }
        count += stream->size();
    }
    std::vector<std::size_t> cursors(streams.size(), 0U);
    for (std::size_t merged = 0U; merged < count; ++merged) {
        const MultiwayContinuationDelta* next = nullptr;
        std::size_t next_worker = 0U;
        for (std::size_t worker = 0U; worker < streams.size(); ++worker) {
            const auto cursor = cursors[worker];
            const auto& deltas = streams[worker]->deltas();
            if (cursor >= deltas.size()) continue;
            const auto* candidate = &deltas[cursor];
            if (next == nullptr || delta_less(*candidate, *next)) {
                next = candidate;
                next_worker = worker;
            }
        }
        if (next == nullptr) throw std::logic_error("multiway continuation merge lost a delta");
        update_regrets_weighted(next->key, next->mixture, next->values, next->importance_weight);
        ++cursors[next_worker];
    }
}

}  // namespace texas::solver::multiway
