#include "solver/multiway_continuation_selector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {

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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Row& row) {
        return row.key.public_state == key.public_state && row.key.actor == key.actor &&
            row.key.street == key.street && row.key.future_bucket == key.future_bucket &&
            row.key.action_abstraction_version == key.action_abstraction_version &&
            row.key.leaf_model_version == key.leaf_model_version;
    });
    if (found != rows_.end()) {
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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Row& row) {
        return row.key.public_state == key.public_state && row.key.actor == key.actor &&
            row.key.street == key.street && row.key.future_bucket == key.future_bucket &&
            row.key.action_abstraction_version == key.action_abstraction_version &&
            row.key.leaf_model_version == key.leaf_model_version;
    });
    auto& row = found == rows_.end() ? rows_.emplace_back(Row{key, {}}) : *found;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto next = row.regrets[index] + values[index] - node_value;
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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Row& row) {
        return row.key.public_state == key.public_state && row.key.actor == key.actor &&
            row.key.street == key.street && row.key.future_bucket == key.future_bucket &&
            row.key.action_abstraction_version == key.action_abstraction_version &&
            row.key.leaf_model_version == key.leaf_model_version;
    });
    if (found == rows_.end()) rows_.push_back({key, regrets});
    else found->regrets = regrets;
}

}  // namespace texas::solver::multiway
