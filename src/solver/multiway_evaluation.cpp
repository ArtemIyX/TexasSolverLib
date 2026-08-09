#include "solver/multiway_evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace core {
namespace {

constexpr double kProbabilityTolerance = 1e-12;

bool finite_non_negative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

double confidence_z_score(double confidence_level) noexcept {
    if (confidence_level == 0.90) return 1.6448536269514722;
    if (confidence_level == 0.95) return 1.959963984540054;
    return 2.5758293035489004;
}

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t duplicate) noexcept {
    auto value = seed + 0x9e3779b97f4a7c15ULL + (duplicate << 6U) + (duplicate >> 2U);
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

void append_failure(
    MultiwayEvaluationResult& result,
    MultiwayEvaluationFailure failure,
    std::uint64_t scenario_id) {
    result.failures.push_back({failure, scenario_id});
}

void add_metrics(MultiwayEvaluationMetrics& metrics, const MultiwayEvaluationSample& sample) {
    ++metrics.samples;
    const auto count = static_cast<double>(metrics.samples);
    metrics.mean_elapsed_nanoseconds +=
        (static_cast<double>(sample.elapsed_nanoseconds) - metrics.mean_elapsed_nanoseconds) / count;
    metrics.peak_resident_memory_bytes = std::max(metrics.peak_resident_memory_bytes, sample.resident_memory_bytes);
    metrics.max_normalization_error = std::max(metrics.max_normalization_error, sample.normalization_error);
    metrics.mean_leaf_variance += (sample.leaf_variance - metrics.mean_leaf_variance) / count;
    metrics.total_pruned_negative_regrets += sample.pruned_negative_regrets;
    metrics.max_worker_imbalance = std::max(metrics.max_worker_imbalance, sample.worker_imbalance);
}

MultiwayQualityDiagnostics make_diagnostics(
    const MultiwayEvaluationConfig& config,
    const MultiwayEvaluationMetrics& metrics,
    std::uint64_t nash_samples) {
    MultiwayQualityDiagnostics diagnostics;
    diagnostics.method = config.metric_method;
    diagnostics.units = config.value_units;
    diagnostics.sample_count = nash_samples;
    diagnostics.seed = config.seed;
    diagnostics.standard_error = metrics.confidence_interval_half_width;
    diagnostics.confidence_level = config.confidence_level;
    diagnostics.confidence_interval_definition = "normal-approximation half-width";
    diagnostics.policy_version = "multiway-evaluation-v1";
    diagnostics.model_version = "multiway-evaluation-v1";
    diagnostics.per_seat_sample_counts.assign(config.seat_count, nash_samples);
    diagnostics.per_seat_standard_errors.assign(config.seat_count, metrics.confidence_interval_half_width);
    diagnostics.per_seat_confidence_intervals.assign(config.seat_count, metrics.confidence_interval_half_width);
    return diagnostics;
}

bool contains_action(
    const std::vector<MultiwayActionDescriptor>& actions,
    const MultiwayActionDescriptor& action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

}  // namespace

void MultiwayEvaluationSeatValues::validate() const {
    if (seat_count < 2U || seat_count > kMultiwayEvaluationMaxSeats) {
        throw std::invalid_argument("multiway evaluation requires two through six seat values");
    }
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
        if (!std::isfinite(values[seat])) {
            throw std::invalid_argument("multiway evaluation seat value must be finite");
        }
    }
}

void MultiwayEvaluationSample::validate() const {
    profile_values.validate();
    best_response_values.validate();
    if (profile_values.seat_count != best_response_values.seat_count ||
        !finite_non_negative(normalization_error) || !finite_non_negative(leaf_variance) ||
        !finite_non_negative(worker_imbalance)) {
        throw std::invalid_argument("multiway evaluation sample is invalid");
    }
}

void MultiwayEvaluationConfig::validate() const {
    if (seat_count < 2U || seat_count > kMultiwayEvaluationMaxSeats || duplicate_deals == 0U ||
        duplicate_deals > kMultiwayEvaluationMaxDuplicateDeals ||
        candidates.empty() || candidates.size() > kMultiwayEvaluationMaxCandidates ||
        local_best_response_scenarios.size() > kMultiwayEvaluationMaxScenarios ||
        off_tree_gauntlets.size() > kMultiwayEvaluationMaxScenarios || evaluate_match == nullptr ||
        (confidence_level != 0.90 && confidence_level != 0.95 && confidence_level != 0.99) ||
        (metric_method != MultiwayMetricMethod::ExactEnumeration &&
         metric_method != MultiwayMetricMethod::SampledEstimate) ||
        (value_units != MultiwayValueUnits::Chips && value_units != MultiwayValueUnits::BigBlinds &&
         value_units != MultiwayValueUnits::PotFraction &&
         value_units != MultiwayValueUnits::NormalizedStackFraction)) {
        throw std::invalid_argument("multiway evaluation configuration is invalid");
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (candidates[index].id == candidates[prior].id) {
                throw std::invalid_argument("multiway evaluation candidate ids must be unique");
            }
        }
    }
    for (const auto& scenario : local_best_response_scenarios) {
        if (scenario.seat < 0 || static_cast<std::uint8_t>(scenario.seat) >= seat_count ||
            !std::isfinite(scenario.minimum_improvement)) {
            throw std::invalid_argument("multiway local best response scenario is invalid");
        }
    }
    for (const auto& gauntlet : off_tree_gauntlets) {
        if (gauntlet.public_state.legal_actions.empty() ||
            !contains_action(gauntlet.public_state.legal_actions, gauntlet.observed_action)) {
            throw std::invalid_argument("multiway off-tree gauntlet must retain its observed legal action");
        }
    }
}

MultiwayEvaluationResult evaluate_multiway_candidates(const MultiwayEvaluationConfig& config) {
    config.validate();
    MultiwayEvaluationResult result;
    result.metrics.confidence_level = config.confidence_level;
    const auto candidate_count = config.candidates.size();
    const auto seat_count = static_cast<std::size_t>(config.seat_count);
    const auto expected_samples = config.duplicate_deals * config.seat_count;
    std::vector<Value> nash_profile_sum(seat_count, 0.0);
    std::vector<Value> nash_response_sum(seat_count, 0.0);
    Value nash_improvement_mean = 0.0;
    Value nash_improvement_m2 = 0.0;

    result.cross_play.reserve(candidate_count * candidate_count);
    for (const auto& focal : config.candidates) {
        for (const auto& opponent : config.candidates) {
            MultiwayCrossPlayCell cell;
            cell.focal_candidate_id = focal.id;
            cell.opponent_candidate_id = opponent.id;
            std::vector<std::uint64_t> seat_candidates(seat_count, opponent.id);
            Value total = 0.0;
            for (std::uint64_t duplicate = 0; duplicate < config.duplicate_deals; ++duplicate) {
                const MultiwayEvaluationDeal deal = {duplicate, mix_seed(config.seed, duplicate)};
                for (std::uint8_t rotation = 0; rotation < config.seat_count; ++rotation) {
                    const auto focal_seat = static_cast<std::size_t>(rotation);
                    seat_candidates[focal_seat] = focal.id;
                    MultiwayEvaluationSample sample;
                    const MultiwayEvaluationMatchRequest request = {
                        &deal, seat_candidates.data(), config.seat_count, rotation};
                    if (!config.evaluate_match(request, &sample, config.callback_context)) {
                        append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected, deal.duplicate_index);
                        seat_candidates[focal_seat] = opponent.id;
                        continue;
                    }
                    try {
                        sample.validate();
                        if (sample.profile_values.seat_count != config.seat_count) {
                            throw std::invalid_argument("multiway evaluation callback returned a different seat count");
                        }
                    } catch (const std::exception&) {
                        append_failure(result, MultiwayEvaluationFailure::InvalidSample, deal.duplicate_index);
                        seat_candidates[focal_seat] = opponent.id;
                        continue;
                    }
                    total += sample.profile_values.values[focal_seat];
                    ++cell.samples;
                    add_metrics(result.metrics, sample);
                    seat_candidates[focal_seat] = opponent.id;
                }
            }
            if (cell.samples != expected_samples) {
                append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected, focal.id ^ opponent.id);
            }
            cell.mean_focal_value = cell.samples == 0U ? 0.0 : total / static_cast<Value>(cell.samples);
            result.cross_play.push_back(cell);
        }
    }

    // The reduced-game NashConv profile evaluates each candidate against itself
    // under the same duplicate/rotation schedule, then uses existing NashConv
    // aggregation to preserve its unit and uncertainty validation contract.
    const auto& reference = config.candidates.front();
    std::vector<std::uint64_t> reference_seats(seat_count, reference.id);
    std::uint64_t nash_samples = 0;
    for (std::uint64_t duplicate = 0; duplicate < config.duplicate_deals; ++duplicate) {
        const MultiwayEvaluationDeal deal = {duplicate, mix_seed(config.seed, duplicate)};
        for (std::uint8_t rotation = 0; rotation < config.seat_count; ++rotation) {
            MultiwayEvaluationSample sample;
            const MultiwayEvaluationMatchRequest request = {
                &deal, reference_seats.data(), config.seat_count, rotation};
            if (!config.evaluate_match(request, &sample, config.callback_context)) {
                append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected, deal.duplicate_index);
                continue;
            }
            try {
                sample.validate();
                if (sample.profile_values.seat_count != config.seat_count) {
                    throw std::invalid_argument("multiway evaluation callback returned a different seat count");
                }
            } catch (const std::exception&) {
                append_failure(result, MultiwayEvaluationFailure::InvalidSample, deal.duplicate_index);
                continue;
            }
            add_metrics(result.metrics, sample);
            for (std::size_t seat = 0; seat < seat_count; ++seat) {
                nash_profile_sum[seat] += sample.profile_values.values[seat];
                nash_response_sum[seat] += sample.best_response_values.values[seat];
            }
            Value total_improvement = 0.0;
            for (std::size_t seat = 0; seat < seat_count; ++seat) {
                total_improvement += sample.best_response_values.values[seat] - sample.profile_values.values[seat];
            }
            ++nash_samples;
            const auto difference = total_improvement - nash_improvement_mean;
            nash_improvement_mean += difference / static_cast<Value>(nash_samples);
            nash_improvement_m2 += difference * (total_improvement - nash_improvement_mean);
        }
    }
    if (nash_samples == 0U) {
        append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected, reference.id);
    } else {
        for (std::size_t seat = 0; seat < seat_count; ++seat) {
            nash_profile_sum[seat] /= static_cast<Value>(nash_samples);
            nash_response_sum[seat] /= static_cast<Value>(nash_samples);
        }
        if (nash_samples > 1U) {
            const auto standard_error = std::sqrt(
                nash_improvement_m2 / static_cast<Value>(nash_samples - 1U) /
                static_cast<Value>(nash_samples));
            result.metrics.confidence_interval_half_width = confidence_z_score(config.confidence_level) * standard_error;
        }
        try {
            result.reduced_game_nash_conv = compute_multiway_nash_conv(
                nash_profile_sum, nash_response_sum, make_diagnostics(config, result.metrics, nash_samples));
        } catch (const std::exception&) {
            append_failure(result, MultiwayEvaluationFailure::InvalidConfidence, reference.id);
        }
    }

    for (const auto& scenario : config.local_best_response_scenarios) {
        MultiwayLocalBestResponseReport report;
        report.scenario_id = scenario.id;
        if (config.evaluate_local_best_response == nullptr) {
            append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRejected, scenario.id);
            result.local_best_response.push_back(report);
            continue;
        }
        Value total = 0.0;
        for (std::uint64_t duplicate = 0; duplicate < config.duplicate_deals; ++duplicate) {
            MultiwayLocalBestResponseResult sample;
            const MultiwayLocalBestResponseRequest request = {
                {duplicate, mix_seed(config.seed, duplicate)}, scenario};
            if (!config.evaluate_local_best_response(request, &sample, config.callback_context) ||
                !std::isfinite(sample.profile_value) || !std::isfinite(sample.response_value)) {
                append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRejected, scenario.id);
                continue;
            }
            total += sample.response_value - sample.profile_value;
            ++report.samples;
        }
        report.mean_improvement = report.samples == 0U ? 0.0 : total / static_cast<Value>(report.samples);
        report.passed = report.samples == config.duplicate_deals &&
            report.mean_improvement + kProbabilityTolerance >= scenario.minimum_improvement;
        if (!report.passed) {
            append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRegression, scenario.id);
        }
        result.local_best_response.push_back(report);
    }

    for (const auto& gauntlet : config.off_tree_gauntlets) {
        MultiwayOffTreeGauntletReport report;
        report.scenario_id = gauntlet.id;
        if (config.evaluate_off_tree == nullptr) {
            report.failure = MultiwayEvaluationFailure::OffTreeCallbackRejected;
        } else {
            MultiwayOffTreeGauntletResult output;
            if (!config.evaluate_off_tree(gauntlet, &output, config.callback_context)) {
                report.failure = MultiwayEvaluationFailure::OffTreeCallbackRejected;
            } else if (!contains_action(gauntlet.public_state.legal_actions, gauntlet.observed_action)) {
                report.failure = MultiwayEvaluationFailure::OffTreeObservedActionMissing;
            } else if (!output.has_sampled_action ||
                       !contains_action(gauntlet.public_state.legal_actions, output.sampled_action)) {
                report.failure = MultiwayEvaluationFailure::OffTreeIllegalAction;
            } else if (!std::isfinite(output.policy_total) ||
                       std::fabs(output.policy_total - 1.0) > kProbabilityTolerance) {
                report.failure = MultiwayEvaluationFailure::OffTreeNonNormalizedPolicy;
            }
        }
        report.passed = report.failure == MultiwayEvaluationFailure::None;
        if (!report.passed) append_failure(result, report.failure, gauntlet.id);
        result.off_tree_gauntlets.push_back(report);
    }
    return result;
}

std::vector<MultiwayEvaluationFailureFixture> multiway_evaluation_failure_fixtures() {
    return {
        {MultiwayEvaluationFailure::InvalidConfiguration, 1U},
        {MultiwayEvaluationFailure::MatchCallbackRejected, 2U},
        {MultiwayEvaluationFailure::InvalidSample, 3U},
        {MultiwayEvaluationFailure::InvalidConfidence, 4U},
        {MultiwayEvaluationFailure::LocalBestResponseRejected, 5U},
        {MultiwayEvaluationFailure::LocalBestResponseRegression, 6U},
        {MultiwayEvaluationFailure::OffTreeCallbackRejected, 7U},
        {MultiwayEvaluationFailure::OffTreeObservedActionMissing, 8U},
        {MultiwayEvaluationFailure::OffTreeIllegalAction, 9U},
        {MultiwayEvaluationFailure::OffTreeNonNormalizedPolicy, 10U},
    };
}

}  // namespace core
