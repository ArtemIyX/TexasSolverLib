#include "solver/multiway_cfr.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace core {

namespace {

constexpr double kExactNashConvTolerance = 1e-12;

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

void validate_strategy_and_values(
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values) {
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
}

void validate_quality_diagnostics(const MultiwayQualityDiagnostics& diagnostics, std::size_t seat_count) {
    if (diagnostics.units != MultiwayValueUnits::Chips &&
        diagnostics.units != MultiwayValueUnits::BigBlinds &&
        diagnostics.units != MultiwayValueUnits::PotFraction &&
        diagnostics.units != MultiwayValueUnits::NormalizedStackFraction) {
        throw std::invalid_argument("NashConv has unsupported value units");
    }
    if (!std::isfinite(diagnostics.standard_error) || diagnostics.standard_error < 0.0) {
        throw std::invalid_argument("NashConv standard error must be finite and non-negative");
    }
    if (diagnostics.method == MultiwayMetricMethod::SampledEstimate && diagnostics.sample_count == 0U) {
        throw std::invalid_argument("sampled NashConv requires a non-zero sample count");
    }
    if (diagnostics.method == MultiwayMetricMethod::SampledEstimate && diagnostics.policy_version.empty()) {
        throw std::invalid_argument("sampled NashConv requires a policy version");
    }
    if (diagnostics.method == MultiwayMetricMethod::SampledEstimate &&
        (!std::isfinite(diagnostics.confidence_level) || diagnostics.confidence_level <= 0.0 ||
         diagnostics.confidence_level >= 1.0 || diagnostics.confidence_interval_definition.empty())) {
        throw std::invalid_argument("sampled NashConv requires a finite confidence level and interval definition");
    }
    if (diagnostics.per_seat_sample_counts.size() != diagnostics.per_seat_standard_errors.size() ||
        diagnostics.per_seat_sample_counts.size() != diagnostics.per_seat_confidence_intervals.size()) {
        throw std::invalid_argument("per-seat NashConv diagnostics must have matching sizes");
    }
    if (diagnostics.method == MultiwayMetricMethod::SampledEstimate &&
        diagnostics.per_seat_sample_counts.size() != seat_count) {
        throw std::invalid_argument("sampled NashConv requires per-seat uncertainty for every seat");
    }
    for (std::size_t seat = 0; seat < diagnostics.per_seat_sample_counts.size(); ++seat) {
        if (diagnostics.method == MultiwayMetricMethod::SampledEstimate && diagnostics.per_seat_sample_counts[seat] == 0U) {
            throw std::invalid_argument("sampled per-seat NashConv requires a non-zero sample count");
        }
        if (!std::isfinite(diagnostics.per_seat_standard_errors[seat]) || diagnostics.per_seat_standard_errors[seat] < 0.0 ||
            !std::isfinite(diagnostics.per_seat_confidence_intervals[seat]) || diagnostics.per_seat_confidence_intervals[seat] < 0.0) {
            throw std::invalid_argument("per-seat NashConv uncertainty must be finite and non-negative");
        }
    }
    if (diagnostics.method != MultiwayMetricMethod::ExactEnumeration &&
        diagnostics.method != MultiwayMetricMethod::SampledEstimate) {
        throw std::invalid_argument("NashConv has an unsupported metric method");
    }
}

}  // namespace

void MultiwayCFRConfig::validate() const {
    if (player_count < 2U || player_count > 6U) {
        throw std::invalid_argument("MultiwayCFRConfig supports two through six players");
    }
    if (algorithm != MultiwayCFRAlgorithm::FullTreeCFR &&
        algorithm != MultiwayCFRAlgorithm::ExternalSamplingMCCFR) {
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
    double positive_scale = 0.0;
    for (const auto regret : regrets) {
        if (!std::isfinite(regret)) throw std::invalid_argument("regrets must be finite");
        if (regret > positive_scale) positive_scale = regret;
    }
    std::vector<Probability> strategy(regrets.size(), 0.0);
    if (positive_scale == 0.0) {
        std::fill(strategy.begin(), strategy.end(), 1.0 / static_cast<double>(strategy.size()));
        return strategy;
    }
    double scaled_sum = 0.0;
    for (const auto regret : regrets) {
        if (regret > 0.0) scaled_sum += regret / positive_scale;
    }
    if (!std::isfinite(scaled_sum) || scaled_sum <= 0.0) {
        throw std::overflow_error("regret matching produced a non-finite normalization");
    }
    double assigned = 0.0;
    std::size_t last_positive = 0;
    for (std::size_t action = 0; action < regrets.size(); ++action) {
        if (regrets[action] > 0.0) last_positive = action;
    }
    for (std::size_t action = 0; action < regrets.size(); ++action) {
        if (regrets[action] > 0.0 && action != last_positive) {
            strategy[action] = (regrets[action] / positive_scale) / scaled_sum;
            assigned += strategy[action];
        }
    }
    strategy[last_positive] = 1.0 - assigned;
    return strategy;
}

MultiwayCFRUpdate make_multiway_full_tree_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values) {
    validate_reaches(player_reaches, traverser);
    validate_probability(chance_reach, "chance reach");
    validate_strategy_and_values(strategy, action_values);

    MultiwayCFRUpdate update;
    update.counterfactual_reach = multiway_counterfactual_reach(player_reaches, traverser, chance_reach);
    update.average_strategy_weight = player_reaches[static_cast<std::size_t>(traverser)];
    update.regret_deltas.resize(strategy.size());
    update.strategy_deltas.resize(strategy.size());
    for (std::size_t action = 0; action < strategy.size(); ++action) {
        update.node_value += strategy[action] * action_values[action];
    }
    if (!std::isfinite(update.node_value)) {
        throw std::overflow_error("full-tree CFR update has a non-finite node value");
    }
    for (std::size_t action = 0; action < strategy.size(); ++action) {
        update.regret_deltas[action] =
            update.counterfactual_reach * (action_values[action] - update.node_value);
        update.strategy_deltas[action] = update.average_strategy_weight * strategy[action];
        if (!std::isfinite(update.regret_deltas[action]) || !std::isfinite(update.strategy_deltas[action])) {
            throw std::overflow_error("full-tree CFR update contains a non-finite delta");
        }
    }
    return update;
}

MultiwayCFRUpdate make_multiway_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values) {
    return make_multiway_full_tree_cfr_update(
        player_reaches, traverser, chance_reach, strategy, action_values);
}

MultiwayCFRUpdate make_multiway_external_sampling_cfr_update(
    const MultiwayExternalSamplingRequest& request) {
    validate_reaches(request.player_reaches, request.traverser);
    validate_probability(request.chance_reach, "chance reach");
    validate_probability(request.sampling_reach, "sampling reach");
    validate_probability(request.traverser_reach, "traverser reach");
    if (request.sampling_reach == 0.0) {
        throw std::invalid_argument("external-sampling update requires non-zero sampling reach");
    }
    if (std::fabs(request.traverser_reach -
                  request.player_reaches[static_cast<std::size_t>(request.traverser)]) > 1e-12) {
        throw std::invalid_argument("external-sampling traverser reach must match player reaches");
    }
    validate_strategy_and_values(request.strategy, request.sampled_action_values);

    MultiwayCFRUpdate update;
    update.counterfactual_reach = multiway_counterfactual_reach(
        request.player_reaches, request.traverser, request.chance_reach);
    const auto importance_weight = update.counterfactual_reach / request.sampling_reach;
    update.average_strategy_weight = request.traverser_reach / request.sampling_reach;
    if (!std::isfinite(importance_weight) || !std::isfinite(update.average_strategy_weight)) {
        throw std::overflow_error("external-sampling importance weight is non-finite");
    }
    update.regret_deltas.resize(request.strategy.size());
    update.strategy_deltas.resize(request.strategy.size());
    for (std::size_t action = 0; action < request.strategy.size(); ++action) {
        update.node_value += request.strategy[action] * request.sampled_action_values[action];
    }
    if (!std::isfinite(update.node_value)) {
        throw std::overflow_error("external-sampling CFR update has a non-finite node value");
    }
    for (std::size_t action = 0; action < request.strategy.size(); ++action) {
        update.regret_deltas[action] = importance_weight *
            (request.sampled_action_values[action] - update.node_value);
        update.strategy_deltas[action] = update.average_strategy_weight * request.strategy[action];
        if (!std::isfinite(update.regret_deltas[action]) ||
            !std::isfinite(update.strategy_deltas[action])) {
            throw std::overflow_error("external-sampling update contains a non-finite delta");
        }
    }
    return update;
}

MultiwayExternalSamplingRequest make_multiway_external_sampling_request(
    std::vector<Probability> player_reaches,
    PlayerId traverser,
    const MultiwayJointPrivateSample& private_sample,
    std::vector<Probability> strategy,
    std::vector<Value> sampled_action_values) {
    if (private_sample.holes.empty() || private_sample.accepted_trajectories != 1U ||
        private_sample.discarded_trajectories != 0U ||
        !std::isfinite(private_sample.chance_reach) ||
        !std::isfinite(private_sample.conditional_deal_probability) ||
        !std::isfinite(private_sample.proposal_reach) ||
        !std::isfinite(private_sample.inclusion_reach) ||
        private_sample.chance_reach <= 0.0 || private_sample.conditional_deal_probability <= 0.0 ||
        private_sample.proposal_reach <= 0.0 ||
        private_sample.inclusion_reach <= 0.0 || private_sample.inclusion_reach > 1.0) {
        throw std::invalid_argument("external-sampling request requires one accepted compiled private sample");
    }
    const auto expected_proposal =
        private_sample.conditional_deal_probability * private_sample.inclusion_reach;
    if (!std::isfinite(expected_proposal) ||
        std::fabs(private_sample.proposal_reach - expected_proposal) > 1e-12) {
        throw std::invalid_argument("compiled private sample has inconsistent proposal and inclusion reach");
    }
    MultiwayExternalSamplingRequest request;
    request.player_reaches = std::move(player_reaches);
    request.traverser = traverser;
    request.chance_reach = private_sample.chance_reach;
    request.sampling_reach = private_sample.proposal_reach;
    request.strategy = std::move(strategy);
    request.sampled_action_values = std::move(sampled_action_values);
    if (traverser >= 0 && static_cast<std::size_t>(traverser) < request.player_reaches.size()) {
        request.traverser_reach = request.player_reaches[static_cast<std::size_t>(traverser)];
    }
    return request;
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
        if (!std::isfinite(regret_sum[action]) || !std::isfinite(strategy_sum[action]) ||
            !std::isfinite(update.regret_deltas[action]) || !std::isfinite(update.strategy_deltas[action]) ||
            !std::isfinite(regret_sum[action] + update.regret_deltas[action]) ||
            !std::isfinite(strategy_sum[action] + update.strategy_deltas[action])) {
            throw std::overflow_error("multiway CFR update would produce a non-finite row");
        }
    }
    for (std::size_t action = 0; action < regret_sum.size(); ++action) {
        regret_sum[action] += update.regret_deltas[action];
        strategy_sum[action] += update.strategy_deltas[action];
    }
}

MultiwayNashConv compute_multiway_nash_conv(
    const std::vector<Value>& profile_values,
    const std::vector<Value>& best_response_values,
    const MultiwayQualityDiagnostics& diagnostics) {
    if (profile_values.size() < 2U || profile_values.size() > 6U ||
        best_response_values.size() != profile_values.size()) {
        throw std::invalid_argument("NashConv requires equal two-through-six seat value vectors");
    }
    validate_quality_diagnostics(diagnostics, profile_values.size());
    if (!diagnostics.per_seat_sample_counts.empty() &&
        diagnostics.per_seat_sample_counts.size() != profile_values.size()) {
        throw std::invalid_argument("per-seat NashConv diagnostics must match the seat count");
    }
    MultiwayNashConv result;
    result.profile_values = profile_values;
    result.best_response_values = best_response_values;
    result.unilateral_improvements.resize(profile_values.size(), 0.0);
    for (std::size_t player = 0; player < profile_values.size(); ++player) {
        if (!std::isfinite(profile_values[player]) || !std::isfinite(best_response_values[player])) {
            throw std::invalid_argument("NashConv values must be finite");
        }
        result.unilateral_improvements[player] = best_response_values[player] - profile_values[player];
        if (!std::isfinite(result.unilateral_improvements[player])) {
            throw std::overflow_error("NashConv unilateral improvement is non-finite");
        }
        if (diagnostics.method == MultiwayMetricMethod::ExactEnumeration &&
            result.unilateral_improvements[player] < -kExactNashConvTolerance) {
            throw std::invalid_argument("exact NashConv best response is below profile value");
        }
        result.value += result.unilateral_improvements[player];
        if (!std::isfinite(result.value)) {
            throw std::overflow_error("NashConv accumulation is non-finite");
        }
    }
    result.diagnostics = diagnostics;
    return result;
}

}  // namespace core
