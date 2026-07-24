#pragma once

#include "core/types.hpp"
#include "games/multiway_private.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

// Multiway solving is evaluated with NashConv. Unlike heads-up exploitability,
// it is the sum of each seat's unilateral best-response improvement.
enum class MultiwayQualityMetric : std::uint8_t {
    NashConv,
};

enum class MultiwayCFRAlgorithm : std::uint8_t {
    FullTreeCFR,
    ExternalSamplingMCCFR,
};

struct MultiwayCFRConfig {
    std::uint8_t player_count = 2;
    MultiwayCFRAlgorithm algorithm = MultiwayCFRAlgorithm::ExternalSamplingMCCFR;
    MultiwayQualityMetric quality_metric = MultiwayQualityMetric::NashConv;
    bool deterministic_trajectory_merges = true;

    void validate() const;
};

struct MultiwayCFRUpdate {
    double node_value = 0.0;
    double counterfactual_reach = 0.0;
    double average_strategy_weight = 0.0;
    std::vector<double> regret_deltas;
    std::vector<double> strategy_deltas;
};

// The importance ratio is explicit: values supplied to this update are the
// continuation estimator for one sampled opponent/chance trajectory, not
// full-tree action values.  `sampling_reach` is the probability of sampling
// that trajectory under the proposal distribution.
struct MultiwayExternalSamplingRequest {
    std::vector<Probability> player_reaches;
    PlayerId traverser = -1;
    Probability chance_reach = 1.0;
    Probability sampling_reach = 1.0;
    Probability traverser_reach = 1.0;
    std::vector<Probability> strategy;
    std::vector<Value> sampled_action_values;
};

// Constructs an update request from the compiled private-deal proposal
// contract. This prevents traversal code from reconstructing conditioned
// sampling reach from hole cards or rejection attempts.
MultiwayExternalSamplingRequest make_multiway_external_sampling_request(
    std::vector<Probability> player_reaches,
    PlayerId traverser,
    const MultiwayJointPrivateSample& private_sample,
    std::vector<Probability> strategy,
    std::vector<Value> sampled_action_values);

enum class MultiwayMetricMethod : std::uint8_t {
    ExactEnumeration,
    SampledEstimate,
};

enum class MultiwayValueUnits : std::uint8_t {
    Chips,
    BigBlinds,
    PotFraction,
    NormalizedStackFraction,
};

struct MultiwayQualityDiagnostics {
    MultiwayMetricMethod method = MultiwayMetricMethod::ExactEnumeration;
    MultiwayValueUnits units = MultiwayValueUnits::Chips;
    std::uint64_t sample_count = 0;
    std::uint64_t seed = 0;
    double standard_error = 0.0;
    std::vector<std::uint64_t> per_seat_sample_counts;
    std::vector<double> per_seat_standard_errors;
    std::vector<double> per_seat_confidence_intervals;
    double confidence_level = 0.0;
    std::string confidence_interval_definition;
    std::string policy_version;
    std::string model_version;
};

struct MultiwayNashConv {
    std::vector<Value> profile_values;
    std::vector<Value> best_response_values;
    std::vector<Value> unilateral_improvements;
    Value value = 0.0;
    MultiwayQualityDiagnostics diagnostics;
};

// Product of chance reach and every seat except traverser. This deliberately
// has no "other player" shortcut and is valid for two through six seats.
double multiway_counterfactual_reach(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach = 1.0);

std::vector<Probability> multiway_regret_matching(const std::vector<double>& regrets);

// Produces the full-tree CFR update for one traverser's infoset. The strategy
// sum uses only the traverser's own reach; regret uses all other
// seats' reach and chance reach.
MultiwayCFRUpdate make_multiway_full_tree_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values);

// Compatibility spelling for callers that already use this standalone
// full-tree helper.  New traversal code must choose the named full-tree or
// external-sampling API explicitly.
MultiwayCFRUpdate make_multiway_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values);

MultiwayCFRUpdate make_multiway_external_sampling_cfr_update(
    const MultiwayExternalSamplingRequest& request);

void apply_multiway_cfr_update(
    std::vector<double>& regret_sum,
    std::vector<double>& strategy_sum,
    const MultiwayCFRUpdate& update);

MultiwayNashConv compute_multiway_nash_conv(
    const std::vector<Value>& profile_values,
    const std::vector<Value>& best_response_values,
    const MultiwayQualityDiagnostics& diagnostics = {});

}  // namespace core
