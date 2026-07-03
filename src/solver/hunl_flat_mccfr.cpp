#include "solver/hunl_flat_mccfr.hpp"

#include "util/pcs.hpp"
#include "util/simd.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

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
      config_(config),
      worker_count_(std::max<std::size_t>(1, workers)),
      worker_scratch_(std::max<std::size_t>(1, workers)) {
    validate_mccfr_config(config_);
    profile_.workers.resize(worker_count_);
}

void HUNLFlatMCCFR::run_iteration() {
    const auto target_iteration = iterations_ + 1U;
    last_iteration_counters_ = {};
    if (profile_.workers.size() != worker_count_) {
        profile_.workers.resize(worker_count_);
    }

    run_player_batch(target_iteration, 0);
    if (config_.update_both_players) {
        run_player_batch(target_iteration, 1);
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

void HUNLFlatMCCFR::WorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    average_strategy.clear();
    rows.clear();
    row_lookup.clear();
    counters = {};
}

HUNLFlatMCCFR::WorkerDeltaRow& HUNLFlatMCCFR::WorkerScratch::ensure_row(
    InfosetId id,
    std::size_t value_count) {
    const auto it = row_lookup.find(id);
    if (it != row_lookup.end()) {
        return rows[it->second];
    }

    const auto index = rows.size();
    row_lookup.emplace(id, index);
    rows.push_back(WorkerDeltaRow{id, std::vector<double>(value_count, 0.0), std::vector<double>(value_count, 0.0)});
    return rows.back();
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

    const auto& row_meta = infoset_table_.meta().at(meta.infoset_id.value);
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    std::vector<double> action_values(action_count, 0.0);
    const auto average_strategy = average_action_probabilities(meta.infoset_id);

    if (average_strategy.size() != action_count) {
        throw std::logic_error("HUNLFlatMCCFR action probability size mismatch");
    }

    if (meta.player == context.traversing_player && context.counters != nullptr) {
        context.counters->traversing_player_action_expansions += static_cast<std::uint64_t>(action_count);
    }

    if (config_.mode == HUNLFlatSamplingMode::External &&
        meta.player != context.traversing_player) {
        const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
        auto& row = context.scratch->ensure_row(meta.infoset_id, row_meta.value_count);
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto probability = current_strategy_probability(meta.infoset_id, bucket, action);
                const auto offset = infoset_table_.value_index(meta.infoset_id, bucket, action) - row_meta.offset;
                row.strategy_delta[offset] += own_reach * probability;
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
    auto& row = context.scratch->ensure_row(meta.infoset_id, row_meta.value_count);
    for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
        double node_value = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto probability = current_strategy_probability(meta.infoset_id, bucket, action);
            const auto offset = infoset_table_.value_index(meta.infoset_id, bucket, action) - row_meta.offset;
            row.strategy_delta[offset] += own_reach * probability;
            node_value += probability * action_values[action];
        }

        if (meta.player == context.traversing_player) {
            const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = infoset_table_.value_index(meta.infoset_id, bucket, action) - row_meta.offset;
                row.regret_delta[offset] += opponent_reach * (action_values[action] - node_value);
            }
        }
    }

    double return_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        return_value += average_strategy[action] * action_values[action];
    }
    return return_value;
}

void HUNLFlatMCCFR::compute_current_strategy_rows() {
    const auto strategy_start = std::chrono::steady_clock::now();
    std::vector<double> regrets;
    std::vector<double> strategy;

    for (const auto& meta : infoset_table_.meta()) {
        regrets.assign(meta.action_count, 0.0);
        strategy.assign(meta.action_count, 0.0);
        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    regrets[action] = infoset_table_.regret_value(meta.id, bucket_offset + action);
                }
                compute_strategy_row(regrets.data(), strategy.data(), meta.action_count);
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    infoset_table_.set_current_strategy_value(meta.id, bucket_offset + action, strategy[action]);
                }
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                const auto row_offset = action * static_cast<std::size_t>(meta.bucket_count) + bucket;
                regrets[action] = infoset_table_.regret_value(meta.id, row_offset);
            }
            compute_strategy_row(regrets.data(), strategy.data(), meta.action_count);
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                const auto row_offset = action * static_cast<std::size_t>(meta.bucket_count) + bucket;
                infoset_table_.set_current_strategy_value(meta.id, row_offset, strategy[action]);
            }
        }
    }
    profile_.strategy_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - strategy_start).count();
}

double HUNLFlatMCCFR::current_strategy_probability(
    InfosetId infoset_id,
    std::size_t bucket,
    std::size_t action) const {
    const auto& meta = infoset_table_.meta().at(infoset_id.value);
    if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
        return infoset_table_.current_strategy_value(
            infoset_id,
            bucket * static_cast<std::size_t>(meta.action_count) + action);
    }
    return infoset_table_.current_strategy_value(
        infoset_id,
        action * static_cast<std::size_t>(meta.bucket_count) + bucket);
}

std::vector<double> HUNLFlatMCCFR::average_action_probabilities(InfosetId infoset_id) const {
    const auto& meta = infoset_table_.meta().at(infoset_id.value);
    std::vector<double> averaged(meta.action_count, 0.0);
    if (meta.bucket_count == 0) {
        return averaged;
    }

    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            averaged[action] += current_strategy_probability(infoset_id, bucket, action);
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

void HUNLFlatMCCFR::run_player_batch(std::uint32_t target_iteration, PlayerId traversing_player) {
    compute_current_strategy_rows();
    const auto batches = HUNLSampledScheduler::partition_deterministic(
        config_.traversals_per_iteration,
        worker_count_);
    const auto traverse_start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(worker_count_ > 1 ? worker_count_ - 1 : 0);

    auto worker_fn = [&](std::size_t worker_index) {
        auto& scratch = worker_scratch_[worker_index];
        scratch.clear_keep_capacity();
        const auto range = batches[worker_index].trajectories;
        const auto worker_start = std::chrono::steady_clock::now();
        for (std::uint64_t trajectory_id = range.begin; trajectory_id < range.end; ++trajectory_id) {
            const auto seed = PcsRng::mix_seed(
                config_.seed,
                target_iteration,
                static_cast<std::uint32_t>(traversing_player),
                trajectory_id);
            PcsRng rng(seed);
            TraversalContext context;
            context.traversing_player = traversing_player;
            context.rng = &rng;
            context.counters = &scratch.counters;
            context.scratch = &scratch;
            (void)traverse(graph_.root, context);
        }
        auto& worker_profile = profile_.workers[worker_index];
        worker_profile.traverse_seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count();
        worker_profile.traversals += range.size();
        worker_profile.nodes_visited += scratch.counters.nodes_visited;
        worker_profile.sampled_opponent_actions += scratch.counters.sampled_opponent_actions;
        worker_profile.traversing_player_action_expansions += scratch.counters.traversing_player_action_expansions;
        worker_profile.active_infosets += scratch.rows.size();
    };

    for (std::size_t worker_index = 1; worker_index < worker_count_; ++worker_index) {
        threads.emplace_back(worker_fn, worker_index);
    }
    worker_fn(0);
    for (auto& thread : threads) {
        thread.join();
    }

    profile_.traverse_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - traverse_start).count();
    profile_.traversals += config_.traversals_per_iteration;

    for (std::size_t worker_index = 0; worker_index < worker_count_; ++worker_index) {
        merge_worker_rows(worker_index);
    }
}

void HUNLFlatMCCFR::merge_worker_rows(std::size_t worker_index) {
    auto merge_start = std::chrono::steady_clock::now();
    auto& scratch = worker_scratch_[worker_index];
    std::sort(scratch.rows.begin(), scratch.rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id.value < rhs.id.value;
    });

    for (const auto& row : scratch.rows) {
        const auto& meta = infoset_table_.meta().at(row.id.value);
        for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
            infoset_table_.set_regret_value(
                row.id,
                offset,
                infoset_table_.regret_value(row.id, offset) + row.regret_delta[offset]);
            infoset_table_.set_strategy_sum_value(
                row.id,
                offset,
                infoset_table_.strategy_sum_value(row.id, offset) + row.strategy_delta[offset]);
        }
    }

    const auto merge_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - merge_start).count();
    profile_.merge_seconds += merge_seconds;
    profile_.workers[worker_index].merge_seconds += merge_seconds;
    last_iteration_counters_.nodes_visited += scratch.counters.nodes_visited;
    last_iteration_counters_.sampled_opponent_actions += scratch.counters.sampled_opponent_actions;
    last_iteration_counters_.traversing_player_action_expansions +=
        scratch.counters.traversing_player_action_expansions;
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

const HUNLFlatMCCFR::Profile& HUNLFlatMCCFR::profile() const noexcept {
    return profile_;
}

std::size_t HUNLFlatMCCFR::worker_count() const noexcept {
    return worker_count_;
}

}  // namespace core
