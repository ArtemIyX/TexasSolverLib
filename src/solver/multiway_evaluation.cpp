#include "solver/multiway_evaluation.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

constexpr double kProbabilityTolerance = 1e-12;
using texas::core::fingerprint::append_u64;

void append_double(std::uint64_t& hash, double value) noexcept {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    append_u64(hash, bits);
}

void append_action(std::uint64_t& hash, const MultiwayActionDescriptor& action) noexcept {
    append_u64(hash, static_cast<std::uint64_t>(action.action));
    append_u64(hash, action.action_index);
    append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(action.target_street_contribution)));
    append_u64(hash, action.action_menu_id);
}

void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept {
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
        append_u64(hash, field);
    });
}

void append_history(std::uint64_t& hash, const MultiwayHandHistory& history) noexcept {
    append_u64(hash, history.schema_version);
    append_u64(hash, history.hand_seed);
    append_u64(hash, history.initial_config.starting_stacks.size());
    for (const auto value : history.initial_config.starting_stacks) append_u64(hash, value);
    append_u64(hash, history.initial_config.initial_contributions.size());
    for (const auto value : history.initial_config.initial_contributions) append_u64(hash, value);
    append_u64(hash, history.initial_config.initial_street_contributions.size());
    for (const auto value : history.initial_config.initial_street_contributions) append_u64(hash, value);
    append_u64(hash, static_cast<std::uint64_t>(history.initial_config.first_player));
    append_u64(hash, static_cast<std::uint64_t>(history.initial_config.big_blind));
    append_u64(hash, static_cast<std::uint64_t>(history.initial_config.street));
    append_u64(hash, history.initial_config.rake_policy.identity());
    append_u64(hash, history.events.size());
    for (const auto& event : history.events) {
        append_u64(hash, static_cast<std::uint64_t>(event.kind));
        append_u64(hash, static_cast<std::uint64_t>(event.decision.acting_seat));
        append_u64(hash, static_cast<std::uint64_t>(event.decision.action));
        append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(event.decision.target_street_contribution)));
        append_u64(hash, event.decision.decision_seed);
        append_u64(hash, static_cast<std::uint64_t>(event.next_street));
        append_u64(hash, static_cast<std::uint64_t>(event.first_player));
    }
}

std::uint64_t aivat_record_hash(const MultiwayAivatEvaluationRecord& record) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, record.schema_version);
    append_identity(hash, record.identity);
    append_history(hash, record.public_history);
    append_u64(hash, record.raw_chip_outcome.seat_count);
    for (std::size_t seat = 0U; seat < record.raw_chip_outcome.seat_count; ++seat) {
        append_double(hash, record.raw_chip_outcome.values[seat]);
    }
    append_u64(hash, record.decisions.size());
    for (const auto& decision : record.decisions) {
        append_u64(hash, decision.decision_index);
        append_u64(hash, static_cast<std::uint64_t>(decision.acting_seat));
        append_action(hash, decision.sampled_action);
        append_u64(hash, decision.decision_seed);
        append_u64(hash, decision.action_values.size());
        for (const auto& value : decision.action_values) {
            append_action(hash, value.action);
            append_double(hash, value.probability);
            append_double(hash, value.estimated_value);
        }
    }
    return hash;
}

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

void append_failure(MultiwayEvaluationResult& result, MultiwayEvaluationFailure failure) {
    result.failures.push_back(failure);
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

void MultiwayAivatEvaluationRecord::seal() noexcept { integrity_hash = aivat_record_hash(*this); }

void MultiwayAivatEvaluationRecord::validate() const {
    if (schema_version != MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION || integrity_hash == 0U) {
        throw std::invalid_argument("multiway AIVAT record has invalid schema or integrity hash");
    }
    identity.validate();
    public_history.validate();
    raw_chip_outcome.validate();
    if (raw_chip_outcome.seat_count != public_history.initial_config.starting_stacks.size()) {
        throw std::invalid_argument("multiway AIVAT record outcome seat count is invalid");
    }
    std::size_t expected = 0U;
    for (const auto& event : public_history.events) {
        if (event.kind != MultiwayReplayEventKind::Decision) continue;
        if (expected >= decisions.size()) {
            throw std::invalid_argument("multiway AIVAT record is missing a decision");
        }
        const auto& decision = decisions[expected];
        if (decision.decision_index != expected + 1U || decision.acting_seat != event.decision.acting_seat ||
            decision.sampled_action.action != event.decision.action ||
            decision.sampled_action.target_street_contribution != event.decision.target_street_contribution ||
            decision.decision_seed != event.decision.decision_seed || decision.action_values.empty()) {
            throw std::invalid_argument("multiway AIVAT record decision does not match public history");
        }
        double total_probability = 0.0;
        bool found_sampled = false;
        for (std::size_t index = 0U; index < decision.action_values.size(); ++index) {
            const auto& value = decision.action_values[index];
            if (!std::isfinite(value.probability) || value.probability < 0.0 || value.probability > 1.0 ||
                !std::isfinite(value.estimated_value) || value.action.action_menu_id == 0U) {
                throw std::invalid_argument("multiway AIVAT record action value is invalid");
            }
            for (std::size_t prior = 0U; prior < index; ++prior) {
                if (value.action == decision.action_values[prior].action) {
                    throw std::invalid_argument("multiway AIVAT record has duplicate action values");
                }
            }
            total_probability += value.probability;
            found_sampled = found_sampled || value.action == decision.sampled_action;
        }
        if (!found_sampled || std::fabs(total_probability - 1.0) > kProbabilityTolerance) {
            throw std::invalid_argument("multiway AIVAT record policy is invalid");
        }
        ++expected;
    }
    if (expected != decisions.size() || integrity_hash != aivat_record_hash(*this)) {
        throw std::invalid_argument("multiway AIVAT record integrity check failed");
    }
}

void MultiwayAivatEstimate::validate() const {
    if (seat_count < 2U || seat_count > kMultiwayEvaluationMaxSeats ||
        (confidence_level != 0.90 && confidence_level != 0.95 && confidence_level != 0.99)) {
        throw std::invalid_argument("multiway AIVAT estimate is invalid");
    }
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        if (!std::isfinite(means[seat]) || !std::isfinite(standard_errors[seat]) ||
            !std::isfinite(confidence_interval_half_widths[seat]) ||
            standard_errors[seat] < 0.0 || confidence_interval_half_widths[seat] < 0.0) {
            throw std::invalid_argument("multiway AIVAT estimate contains invalid values");
        }
    }
}

MultiwayAivatEstimate estimate_multiway_aivat(
    const std::vector<MultiwayAivatEvaluationRecord>& records,
    double confidence_level) {
    if (records.empty() || (confidence_level != 0.90 && confidence_level != 0.95 && confidence_level != 0.99)) {
        throw std::invalid_argument("multiway AIVAT estimator requires records and supported confidence");
    }
    records.front().validate();
    const auto identity = records.front().identity;
    const auto seats = static_cast<std::size_t>(records.front().raw_chip_outcome.seat_count);
    std::array<Value, kMultiwayEvaluationMaxSeats> mean{};
    std::array<Value, kMultiwayEvaluationMaxSeats> m2{};
    std::uint64_t result_count = 0U;
    for (const auto& record : records) {
        record.validate();
        if (record.identity != identity || record.raw_chip_outcome.seat_count != seats) {
            throw std::invalid_argument("multiway AIVAT records have incompatible identities");
        }
        std::array<Value, kMultiwayEvaluationMaxSeats> adjusted = record.raw_chip_outcome.values;
        for (const auto& decision : record.decisions) {
            Value expected = 0.0;
            Value sampled = 0.0;
            bool found = false;
            for (const auto& value : decision.action_values) {
                expected += value.probability * value.estimated_value;
                if (value.action == decision.sampled_action) {
                    sampled = value.estimated_value;
                    found = true;
                }
            }
            if (!found) throw std::invalid_argument("multiway AIVAT sampled action is missing");
            adjusted[static_cast<std::size_t>(decision.acting_seat)] -= sampled - expected;
        }
        const auto count = static_cast<Value>(++result_count);
        for (std::size_t seat = 0U; seat < seats; ++seat) {
            const auto delta = adjusted[seat] - mean[seat];
            mean[seat] += delta / count;
            m2[seat] += delta * (adjusted[seat] - mean[seat]);
        }
    }
    MultiwayAivatEstimate result;
    result.samples = records.size();
    result.seat_count = static_cast<std::uint8_t>(seats);
    result.confidence_level = confidence_level;
    for (std::size_t seat = 0U; seat < seats; ++seat) {
        result.means[seat] = mean[seat];
        if (result.samples > 1U) result.standard_errors[seat] = std::sqrt(m2[seat] / (result.samples - 1U) / result.samples);
        result.confidence_interval_half_widths[seat] = confidence_z_score(confidence_level) * result.standard_errors[seat];
    }
    result.validate();
    return result;
}

bool publish_multiway_aivat_evaluation_record(
    const MultiwayAivatEvaluationRecord& record,
    MultiwayAivatEvaluationRecordSinkFn sink,
    const void* context) {
    if (sink == nullptr) throw std::invalid_argument("multiway AIVAT record sink is required");
    record.validate();
    return sink(record, context);
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
                        append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected);
                        seat_candidates[focal_seat] = opponent.id;
                        continue;
                    }
                    try {
                        sample.validate();
                        if (sample.profile_values.seat_count != config.seat_count) {
                            throw std::invalid_argument("multiway evaluation callback returned a different seat count");
                        }
                    } catch (const std::exception&) {
                        append_failure(result, MultiwayEvaluationFailure::InvalidSample);
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
                append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected);
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
                append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected);
                continue;
            }
            try {
                sample.validate();
                if (sample.profile_values.seat_count != config.seat_count) {
                    throw std::invalid_argument("multiway evaluation callback returned a different seat count");
                }
            } catch (const std::exception&) {
                append_failure(result, MultiwayEvaluationFailure::InvalidSample);
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
        append_failure(result, MultiwayEvaluationFailure::MatchCallbackRejected);
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
            append_failure(result, MultiwayEvaluationFailure::InvalidConfidence);
        }
    }

    for (const auto& scenario : config.local_best_response_scenarios) {
        MultiwayLocalBestResponseReport report;
        report.scenario_id = scenario.id;
        if (config.evaluate_local_best_response == nullptr) {
            append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRejected);
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
                append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRejected);
                continue;
            }
            total += sample.response_value - sample.profile_value;
            ++report.samples;
        }
        report.mean_improvement = report.samples == 0U ? 0.0 : total / static_cast<Value>(report.samples);
        report.passed = report.samples == config.duplicate_deals &&
            report.mean_improvement + kProbabilityTolerance >= scenario.minimum_improvement;
        if (!report.passed) {
            append_failure(result, MultiwayEvaluationFailure::LocalBestResponseRegression);
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
        if (!report.passed) append_failure(result, report.failure);
        result.off_tree_gauntlets.push_back(report);
    }
    return result;
}

}  // namespace texas::solver::multiway
