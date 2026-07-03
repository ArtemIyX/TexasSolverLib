#include "solver/hunl_flat_mccfr.hpp"

#include "util/pcs.hpp"
#include "util/simd.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

void validate_mccfr_config(const HUNLFlatMCCFRConfig& config) {
    if (config.mode != HUNLFlatSamplingMode::PublicChance &&
        config.mode != HUNLFlatSamplingMode::Exact &&
        config.mode != HUNLFlatSamplingMode::External) {
        throw std::invalid_argument("HUNLFlatMCCFR prototype currently supports Exact, PublicChance, and External modes only");
    }
    if (config.traversals_per_iteration == 0) {
        throw std::invalid_argument("HUNLFlatMCCFR traversals_per_iteration must be positive");
    }
    if (config.batch_size == 0) {
        throw std::invalid_argument("HUNLFlatMCCFR batch_size must be positive");
    }
    if (config.as_epsilon < 0.0 || config.as_epsilon > 1.0) {
        throw std::invalid_argument("HUNLFlatMCCFR as_epsilon must be in [0, 1]");
    }
}

}  // namespace

HUNLFlatMCCFR::HUNLFlatMCCFR(
    HUNLFlatSolveGraph graph,
    std::array<std::size_t, 2> bucket_count_per_player,
    HUNLFlatMCCFRConfig config,
    HUNLFlatValueLayout layout,
    std::size_t workers,
    HUNLFlatStoragePrecision precision)
    : graph_(std::move(graph)),
      infoset_table_(HUNLFlatInfosetTable::build(graph_, bucket_count_per_player, layout, precision)),
      config_(config) {
    validate_mccfr_config(config_);
    if (workers != 1) {
        throw std::invalid_argument("HUNLFlatMCCFR single-worker prototype only supports workers=1");
    }
}

void HUNLFlatMCCFR::run_iteration() {
    const auto target_iteration = iterations_ + 1U;
    const auto traversal_count = config_.traversals_per_iteration;
    last_iteration_counters_ = {};

    const auto player_begin = 0;
    const auto player_end = config_.update_both_players ? 2 : 1;
    for (int traversing_player = player_begin; traversing_player < player_end; ++traversing_player) {
        for (std::uint32_t traversal_id = 0; traversal_id < traversal_count; ++traversal_id) {
            auto seed = PcsRng::mix_seed(
                config_.seed,
                target_iteration,
                static_cast<std::uint32_t>(traversing_player),
                traversal_id);
            PcsRng rng(seed);
            TraversalContext context;
            context.traversing_player = traversing_player;
            context.rng = &rng;
            context.counters = &last_iteration_counters_;
            (void)traverse(graph_.root, context);
        }
    }

    total_counters_.nodes_visited += last_iteration_counters_.nodes_visited;
    total_counters_.sampled_opponent_actions += last_iteration_counters_.sampled_opponent_actions;
    total_counters_.traversing_player_action_expansions += last_iteration_counters_.traversing_player_action_expansions;
    ++iterations_;
}

void HUNLFlatMCCFR::run_iterations(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        run_iteration();
    }
}

const HUNLFlatSolveGraph& HUNLFlatMCCFR::graph() const noexcept {
    return graph_;
}

const HUNLFlatInfosetTable& HUNLFlatMCCFR::infoset_table() const noexcept {
    return infoset_table_;
}

HUNLFlatInfosetTable& HUNLFlatMCCFR::infoset_table_mut() noexcept {
    return infoset_table_;
}

std::uint32_t HUNLFlatMCCFR::iterations() const noexcept {
    return iterations_;
}

const HUNLFlatMCCFRConfig& HUNLFlatMCCFR::config() const noexcept {
    return config_;
}

double HUNLFlatMCCFR::traverse(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = graph_.node_meta.at(node_idx);
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];

        case HUNLFlatNodeType::Chance: {
            if (config_.mode == HUNLFlatSamplingMode::Exact) {
                double value = 0.0;
                for (std::size_t i = 0; i < meta.chance_count; ++i) {
                    const auto& outcome = graph_.chance_outcomes.at(meta.chance_begin + i);
                    value += outcome.probability * traverse(outcome.child, context);
                }
                return value;
            }

            const auto child = sample_chance_child(meta, *context.rng);
            return traverse(child, context);
        }

        case HUNLFlatNodeType::Decision:
            break;
    }

    if (!meta.has_infoset) {
        throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
    }

    update_current_strategy_row(meta.infoset_id);
    const auto& row_meta = infoset_table_.meta().at(meta.infoset_id.value);
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    std::vector<double> action_values(action_count, 0.0);
    std::vector<double> average_strategy = average_action_probabilities(meta.infoset_id);

    if (average_strategy.size() != action_count) {
        throw std::logic_error("HUNLFlatMCCFR action probability size mismatch");
    }

    if (meta.player == context.traversing_player && context.counters != nullptr) {
        context.counters->traversing_player_action_expansions += static_cast<std::uint64_t>(action_count);
    }

    if (config_.mode == HUNLFlatSamplingMode::External &&
        meta.player != context.traversing_player) {
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto probability = bucket_strategy_probability(meta.infoset_id, bucket, action);
                const auto offset = infoset_table_.value_index(meta.infoset_id, bucket, action);
                infoset_table_.set_strategy_sum_value(
                    meta.infoset_id,
                    offset,
                    infoset_table_.strategy_sum_value(meta.infoset_id, offset) + probability);
            }
        }

        const auto sampled = context.rng->sample_weighted(average_strategy);
        if (context.counters != nullptr) {
            ++context.counters->sampled_opponent_actions;
        }

        auto child_context = context;
        if (meta.player == 0) {
            child_context.p0 *= average_strategy[sampled.first];
        } else {
            child_context.p1 *= average_strategy[sampled.first];
        }
        const auto child_idx = graph_.children.at(meta.child_begin + static_cast<std::uint32_t>(sampled.first));
        return traverse(child_idx, child_context);
    }

    for (std::size_t action = 0; action < action_count; ++action) {
        auto child_context = context;
        if (meta.player == 0) {
            child_context.p0 *= average_strategy[action];
        } else {
            child_context.p1 *= average_strategy[action];
        }
        const auto child_idx = graph_.children.at(meta.child_begin + static_cast<std::uint32_t>(action));
        action_values[action] = traverse(child_idx, child_context);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
        double node_value = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto probability = bucket_strategy_probability(meta.infoset_id, bucket, action);
            infoset_table_.set_strategy_sum_value(
                meta.infoset_id,
                infoset_table_.value_index(meta.infoset_id, bucket, action),
                infoset_table_.strategy_sum_value(
                    meta.infoset_id,
                    infoset_table_.value_index(meta.infoset_id, bucket, action)) + own_reach * probability);
            node_value += probability * action_values[action];
        }

        if (meta.player == context.traversing_player) {
            const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = infoset_table_.value_index(meta.infoset_id, bucket, action);
                const auto regret = infoset_table_.regret_value(meta.infoset_id, offset) +
                    opponent_reach * (action_values[action] - node_value);
                infoset_table_.set_regret_value(meta.infoset_id, offset, regret);
            }
        }
    }

    double return_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        return_value += average_strategy[action] * action_values[action];
    }
    return return_value;
}

void HUNLFlatMCCFR::update_current_strategy_row(InfosetId infoset_id) {
    const auto& meta = infoset_table_.meta().at(infoset_id.value);
    std::vector<double> regrets(meta.action_count, 0.0);
    std::vector<double> strategy(meta.action_count, 0.0);

    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            regrets[action] = infoset_table_.regret_value(
                infoset_id,
                infoset_table_.value_index(infoset_id, bucket, action));
        }
        compute_strategy_row(regrets.data(), strategy.data(), meta.action_count);
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            infoset_table_.set_current_strategy_value(
                infoset_id,
                infoset_table_.value_index(infoset_id, bucket, action),
                strategy[action]);
        }
    }
}

double HUNLFlatMCCFR::bucket_strategy_probability(
    InfosetId infoset_id,
    std::size_t bucket,
    std::size_t action) const {
    return infoset_table_.current_strategy_value(
        infoset_id,
        infoset_table_.value_index(infoset_id, bucket, action));
}

std::vector<double> HUNLFlatMCCFR::average_action_probabilities(InfosetId infoset_id) const {
    const auto& meta = infoset_table_.meta().at(infoset_id.value);
    std::vector<double> averaged(meta.action_count, 0.0);
    if (meta.bucket_count == 0) {
        return averaged;
    }

    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            averaged[action] += bucket_strategy_probability(infoset_id, bucket, action);
        }
    }
    for (auto& probability : averaged) {
        probability /= static_cast<double>(meta.bucket_count);
    }
    return averaged;
}

std::uint32_t HUNLFlatMCCFR::sample_chance_child(const HUNLFlatNodeMeta& meta, PcsRng& rng) const {
    double total = 0.0;
    for (std::size_t i = 0; i < meta.chance_count; ++i) {
        total += graph_.chance_outcomes.at(meta.chance_begin + i).probability;
    }
    if (total <= 0.0) {
        throw std::logic_error("HUNLFlatMCCFR chance node must have positive probability mass");
    }

    double draw = rng.next_unit_f64() * total;
    for (std::size_t i = 0; i < meta.chance_count; ++i) {
        const auto& outcome = graph_.chance_outcomes.at(meta.chance_begin + i);
        if (draw < outcome.probability) {
            return outcome.child;
        }
        draw -= outcome.probability;
    }

    return graph_.chance_outcomes.at(meta.chance_begin + meta.chance_count - 1U).child;
}

std::unordered_map<std::string, std::vector<double>> HUNLFlatMCCFR::export_average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    out.reserve(graph_.infosets.size());

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = infoset_table_.meta().at(infoset.id.value);
        std::vector<double> average(meta.value_count, 0.0);

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                infoset_table_.copy_strategy_sum_values(
                    infoset.id,
                    bucket_offset,
                    average.data() + bucket_offset,
                    meta.action_count);
                normalize(
                    average.data() + bucket_offset,
                    meta.action_count,
                    reduce_action_values(average.data() + bucket_offset, meta.action_count));
            }
        } else {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    average[bucket_offset + action] = infoset_table_.strategy_sum_value(
                        infoset.id,
                        action * static_cast<std::size_t>(meta.bucket_count) + bucket);
                }
                normalize(
                    average.data() + bucket_offset,
                    meta.action_count,
                    reduce_action_values(average.data() + bucket_offset, meta.action_count));
            }
        }

        out.emplace(std::string(graph_.infoset_key(infoset)), std::move(average));
    }

    return out;
}

HUNLFlatAverageStrategyTable HUNLFlatMCCFR::export_average_strategy_table() const {
    HUNLFlatAverageStrategyTable out;
    out.layout = infoset_table_.layout();
    out.meta = infoset_table_.meta();
    out.values.assign(infoset_table_.total_value_count(), 0.0);

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = out.meta.at(infoset.id.value);
        auto* average = out.values.data() + meta.offset;

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                double total = 0.0;
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    total += infoset_table_.strategy_sum_value(
                        infoset.id,
                        action * static_cast<std::size_t>(meta.bucket_count) + bucket);
                }
                if (total > 0.0) {
                    for (std::size_t action = 0; action < meta.action_count; ++action) {
                        average[action * static_cast<std::size_t>(meta.bucket_count) + bucket] =
                            infoset_table_.strategy_sum_value(
                                infoset.id,
                                action * static_cast<std::size_t>(meta.bucket_count) + bucket) / total;
                    }
                    continue;
                }
                const auto uniform = 1.0 / static_cast<double>(meta.action_count);
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    average[action * static_cast<std::size_t>(meta.bucket_count) + bucket] = uniform;
                }
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
            infoset_table_.copy_strategy_sum_values(
                infoset.id,
                bucket_offset,
                average + bucket_offset,
                meta.action_count);
            normalize(
                average + bucket_offset,
                meta.action_count,
                reduce_action_values(average + bucket_offset, meta.action_count));
        }
    }

    return out;
}

const HUNLFlatMCCFR::Counters& HUNLFlatMCCFR::last_iteration_counters() const noexcept {
    return last_iteration_counters_;
}

const HUNLFlatMCCFR::Counters& HUNLFlatMCCFR::total_counters() const noexcept {
    return total_counters_;
}

}  // namespace core
