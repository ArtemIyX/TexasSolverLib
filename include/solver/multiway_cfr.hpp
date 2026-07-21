#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

// Multiway solving is evaluated with NashConv. Unlike heads-up exploitability,
// it is the sum of each seat's unilateral best-response improvement.
enum class MultiwayQualityMetric : std::uint8_t {
    NashConv,
};

enum class MultiwayCFRAlgorithm : std::uint8_t {
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

struct MultiwayNashConv {
    std::vector<Value> profile_values;
    std::vector<Value> best_response_values;
    std::vector<Value> unilateral_improvements;
    Value value = 0.0;
};

// Product of chance reach and every seat except traverser. This deliberately
// has no "other player" shortcut and is valid for two through six seats.
double multiway_counterfactual_reach(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach = 1.0);

std::vector<Probability> multiway_regret_matching(const std::vector<double>& regrets);

// Produces the full-tree CFR update for one traverser's infoset. The strategy
// sum uses the traverser's own reach and chance reach; regret uses all other
// seats' reach and chance reach.
MultiwayCFRUpdate make_multiway_cfr_update(
    const std::vector<Probability>& player_reaches,
    PlayerId traverser,
    Probability chance_reach,
    const std::vector<Probability>& strategy,
    const std::vector<Value>& action_values);

void apply_multiway_cfr_update(
    std::vector<double>& regret_sum,
    std::vector<double>& strategy_sum,
    const MultiwayCFRUpdate& update);

MultiwayNashConv compute_multiway_nash_conv(
    const std::vector<Value>& profile_values,
    const std::vector<Value>& best_response_values);

}  // namespace core
