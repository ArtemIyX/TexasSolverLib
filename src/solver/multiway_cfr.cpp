#include "solver/multiway_cfr.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace core {

namespace {

void validate_probability(Probability value, const char* name) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument(std::string(name) + " must be finite and in [0, 1]");
    }
}

void validate_reaches(const std::vector<Probability>& reaches, PlayerId traverser) {
    if (reaches.size() < 2U || reaches.size() > 6U || traverser < 0 ||
        static_cast<std::size_t>(traverser) >= reaches.size()) {
        throw std::invalid_argument("multiway reach input must contain the traverser and two through six seats");
    }
    for (const auto reach : reaches) validate_probability(reach, "player reach");
}

}  // namespace

void MultiwayCFRConfig::validate() const {
    if (player_count < 2U || player_count > 6U) {
        throw std::invalid_argument("MultiwayCFRConfig supports two through six players");
    }
    if (algorithm != MultiwayCFRAlgorithm::ExternalSamplingMCCFR) {
        throw std::invalid_argument("MultiwayCFRConfig has an unsupported algorithm");
    }
    if (!deterministic_trajectory_merges) {
        throw std::invalid_argument("MultiwayCFRConfig requires deterministic trajectory merges");
    }
    if (quality_metric != MultiwayQualityMetric::NashConv) {
        throw std::invalid_argument("MultiwayCFRConfig has an unsupported quality metric");
    }
}

double multiway_counterfactual_reach(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach) {
    validate_reaches(player_reaches, traverser);
    validate_probability(chance_reach, "chance reach");
    auto reach = chance_reach;
    for (std::size_t player = 0; player < player_reaches.size(); ++player) {
        if (static_cast<PlayerId>(player) != traverser) reach *= player_reaches[player];
    }
    return reach;
}

std::vector<Probability> multiway_regret_matching(const std::vector<double>& regrets) {
    if (regrets.empty()) throw std::invalid_argument("regret matching requires at least one action");
    double positive_sum = 0.0;
    for (const auto regret : regrets) {
        if (!std::isfinite(regret)) throw std::invalid_argument("regrets must be finite");
        if (regret > 0.0) positive_sum += regret;
    }
    std::vector<Probability> strategy(regrets.size(), 0.0);
    if (positive_sum == 0.0) {
        std::fill(strategy.begin(), strategy.end(), 1.0 / static_cast<double>(strategy.size()));
        return strategy;
    }
    for (std::size_t action = 0; action < regrets.size(); ++action) {
        strategy[action] = regrets[action] > 0.0 ? regrets[action] / positive_sum : 0.0;
    }
    return strategy;
}

MultiwayCFRUpdate make_multiway_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values) {
    validate_reaches(player_reaches, traverser);
    validate_probability(chance_reach, "chance reach");
    if (strategy.empty() || strategy.size() != action_values.size()) {
        throw std::invalid_argument("strategy and action values must have the same non-zero size");
    }
    double strategy_total = 0.0;
    for (const auto probability : strategy) {
        validate_probability(probability, "strategy probability");
        strategy_total += probability;
    }
    if (std::fabs(strategy_total - 1.0) > 1e-12) {
        throw std::invalid_argument("strategy probabilities must sum to one");
    }
    for (const auto value : action_values) {
        if (!std::isfinite(value)) throw std::invalid_argument("action values must be finite");
    }

    MultiwayCFRUpdate update;
    update.counterfactual_reach = multiway_counterfactual_reach(player_reaches, traverser, chance_reach);
    update.average_strategy_weight = chance_reach * player_reaches[static_cast<std::size_t>(traverser)];
    update.regret_deltas.resize(strategy.size());
    update.strategy_deltas.resize(strategy.size());
    for (std::size_t action = 0; action < strategy.size(); ++action) {
        update.node_value += strategy[action] * action_values[action];
    }
    for (std::size_t action = 0; action < strategy.size(); ++action) {
        update.regret_deltas[action] =
            update.counterfactual_reach * (action_values[action] - update.node_value);
        update.strategy_deltas[action] = update.average_strategy_weight * strategy[action];
    }
    return update;
}

void apply_multiway_cfr_update(
    std::vector<double>& regret_sum,
    std::vector<double>& strategy_sum,
    const MultiwayCFRUpdate& update) {
    if (regret_sum.size() != update.regret_deltas.size() ||
        strategy_sum.size() != update.strategy_deltas.size() ||
        regret_sum.size() != strategy_sum.size()) {
        throw std::invalid_argument("multiway CFR row shape does not match update");
    }
    for (std::size_t action = 0; action < regret_sum.size(); ++action) {
        regret_sum[action] += update.regret_deltas[action];
        strategy_sum[action] += update.strategy_deltas[action];
    }
}

MultiwayNashConv compute_multiway_nash_conv(
    const std::vector<Value>& profile_values,
    const std::vector<Value>& best_response_values) {
    if (profile_values.size() < 2U || profile_values.size() > 6U ||
        best_response_values.size() != profile_values.size()) {
        throw std::invalid_argument("NashConv requires equal two-through-six seat value vectors");
    }
    MultiwayNashConv result;
    result.profile_values = profile_values;
    result.best_response_values = best_response_values;
    result.unilateral_improvements.resize(profile_values.size(), 0.0);
    for (std::size_t player = 0; player < profile_values.size(); ++player) {
        if (!std::isfinite(profile_values[player]) || !std::isfinite(best_response_values[player])) {
            throw std::invalid_argument("NashConv values must be finite");
        }
        result.unilateral_improvements[player] =
            std::max<Value>(0.0, best_response_values[player] - profile_values[player]);
        result.value += result.unilateral_improvements[player];
    }
    return result;
}

}  // namespace core
