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
        config.mode != HUNLFlatSamplingMode::External &&
        config.mode != HUNLFlatSamplingMode::AverageStrategy) {
        throw std::invalid_argument("HUNLFlatMCCFR prototype currently supports Exact, PublicChance, External, and AverageStrategy modes only");
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

std::vector<HUNLFlatInfosetTableMeta> build_infoset_meta(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& bucket_count_per_player,
    HUNLFlatValueLayout layout) {
    std::vector<HUNLFlatInfosetTableMeta> meta;
    meta.reserve(graph.infosets.size());

    std::uint32_t value_offset = 0;
    std::uint32_t bucket_offset = 0;
    for (const auto& infoset : graph.infosets) {
        HUNLFlatInfosetTableMeta row_meta;
        row_meta.id = infoset.id;
        row_meta.player = infoset.player;
        row_meta.action_count = infoset.action_count;
        row_meta.bucket_offset = bucket_offset;
        row_meta.bucket_count =
            infoset.player >= 0 && infoset.player < 2
            ? static_cast<std::uint32_t>(bucket_count_per_player[infoset.player])
            : 0U;
        row_meta.hand_count = row_meta.bucket_count;
        row_meta.reach_count = row_meta.bucket_count;
        row_meta.offset = value_offset;
        row_meta.value_count = row_meta.bucket_count * static_cast<std::uint32_t>(row_meta.action_count);
        meta.push_back(row_meta);
        value_offset += row_meta.value_count;
        bucket_offset += row_meta.bucket_count;
    }

    (void)layout;
    return meta;
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
      infoset_table_(),
      sparse_storage_(layout, HUNLFlatStoragePrecision::Float32),
      config_(config),
      infoset_meta_(build_infoset_meta(graph_, bucket_count_per_player, layout)),
      worker_count_(std::max<std::size_t>(1, workers)),
      worker_scratch_(std::max<std::size_t>(1, workers)) {
    validate_mccfr_config(config_);
    if (!config_.use_sparse_storage || config_.keep_dense_validation_backend) {
        infoset_table_ = HUNLFlatInfosetTable::build(graph_, bucket_count_per_player, layout, precision);
    }
    initialize_sparse_infoset_shapes(bucket_count_per_player);
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
    total_counters_.as_actions_considered += last_iteration_counters_.as_actions_considered;
    total_counters_.as_actions_sampled += last_iteration_counters_.as_actions_sampled;
    total_counters_.as_forced_at_least_one_count += last_iteration_counters_.as_forced_at_least_one_count;
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

void HUNLFlatMCCFR::initialize_sparse_infoset_shapes(
    const std::array<std::size_t, 2>& bucket_count_per_player) {
    sparse_infoset_shapes_.assign(graph_.infosets.size(), {});
    for (const auto& infoset : graph_.infosets) {
        auto& shape = sparse_infoset_shapes_[infoset.id.value];
        shape.id = infoset.id;
        shape.player = infoset.player;
        shape.street = infoset.street;
        shape.bucket_count =
            infoset.player >= 0 && infoset.player < 2
            ? static_cast<std::uint32_t>(bucket_count_per_player[infoset.player])
            : 0U;
        shape.action_count = infoset.action_count;
    }
}

const HUNLFlatInfosetTableMeta& HUNLFlatMCCFR::infoset_meta(InfosetId infoset_id) const noexcept {
    return infoset_meta_[infoset_id.value];
}

std::size_t HUNLFlatMCCFR::row_value_index(
    const HUNLFlatInfosetTableMeta& meta,
    std::size_t bucket,
    std::size_t action) const noexcept {
    if (config_.use_sparse_storage) {
        return HUNLSampledStorage::value_index(
            sparse_storage_.layout(),
            meta.bucket_count,
            meta.action_count,
            bucket,
            action);
    }
    return infoset_table_.value_index(meta.id, bucket, action) - meta.offset;
}

HUNLSampledRowView HUNLFlatMCCFR::ensure_sparse_row(InfosetId infoset_id) {
    return sparse_storage_.ensure_row(sparse_infoset_shapes_.at(infoset_id.value));
}

HUNLSampledConstRowView HUNLFlatMCCFR::sparse_row_or_empty(InfosetId infoset_id) const noexcept {
    return sparse_storage_.view(infoset_id);
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

    const auto& row_meta = infoset_meta(meta.infoset_id);
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    std::vector<double> action_values(action_count, 0.0);
    const auto average_strategy = average_action_probabilities(meta.infoset_id);

    if (average_strategy.size() != action_count) {
        throw std::logic_error("HUNLFlatMCCFR action probability size mismatch");
    }

    if (meta.player == context.traversing_player &&
        context.counters != nullptr &&
        config_.mode != HUNLFlatSamplingMode::AverageStrategy) {
        context.counters->traversing_player_action_expansions += static_cast<std::uint64_t>(action_count);
    }

    if ((config_.mode == HUNLFlatSamplingMode::External ||
         config_.mode == HUNLFlatSamplingMode::AverageStrategy) &&
        meta.player != context.traversing_player) {
        const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
        auto& row = context.scratch->ensure_row(meta.infoset_id, row_meta.value_count);
        std::vector<double> bucket_strategy(action_count, 0.0);
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto probability = bucket_strategy[action];
                const auto offset = row_value_index(row_meta, bucket, action);
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

    if (config_.mode == HUNLFlatSamplingMode::AverageStrategy &&
        meta.player == context.traversing_player) {
        const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
        auto& row = context.scratch->ensure_row(meta.infoset_id, row_meta.value_count);
        std::vector<double> bucket_strategy(action_count, 0.0);
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = row_value_index(row_meta, bucket, action);
                row.strategy_delta[offset] += own_reach * bucket_strategy[action];
            }
        }

        const auto rho = average_strategy_sampling_probabilities(meta.infoset_id);
        std::vector<double> inclusion_probabilities = rho;
        std::vector<bool> sampled_actions(action_count, false);
        std::size_t sampled_count = 0;
        if (context.counters != nullptr) {
            context.counters->as_actions_considered += static_cast<std::uint64_t>(action_count);
        }

        double rho_sum = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            rho_sum += rho[action];
            if (context.rng->bernoulli(rho[action])) {
                sampled_actions[action] = true;
                ++sampled_count;
            }
        }

        const auto none_selected_probability = [&]() {
            double probability = 1.0;
            for (const auto sample_probability : rho) {
                probability *= (1.0 - sample_probability);
            }
            return probability;
        }();

        if (sampled_count == 0 && action_count > 0) {
            const auto forced = rho_sum > 0.0
                ? context.rng->sample_weighted(rho).first
                : std::size_t{0};
            sampled_actions[forced] = true;
            sampled_count = 1;
            if (context.counters != nullptr) {
                ++context.counters->as_forced_at_least_one_count;
            }
        }

        for (std::size_t action = 0; action < action_count; ++action) {
            if (sampled_actions[action] && rho_sum > 0.0) {
                inclusion_probabilities[action] =
                    std::min(1.0, rho[action] + none_selected_probability * (rho[action] / rho_sum));
            }
        }

        if (context.counters != nullptr) {
            context.counters->as_actions_sampled += static_cast<std::uint64_t>(sampled_count);
            context.counters->traversing_player_action_expansions += static_cast<std::uint64_t>(sampled_count);
        }

        for (std::size_t action = 0; action < action_count; ++action) {
            if (!sampled_actions[action]) {
                action_values[action] = 0.0;
                continue;
            }

            auto child_context = context;
            if (meta.player == 0) {
                child_context.p0 *= average_strategy[action];
            } else {
                child_context.p1 *= average_strategy[action];
            }
            const auto child_idx = graph_.children.at(meta.child_begin + static_cast<std::uint32_t>(action));
            const auto inclusion_probability = std::max(inclusion_probabilities[action], config_.as_epsilon);
            action_values[action] = traverse(child_idx, child_context) / inclusion_probability;
        }

        std::vector<double> node_values(row_meta.bucket_count, 0.0);
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                node_values[bucket] += bucket_strategy[action] * action_values[action];
            }
        }

        const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = row_value_index(row_meta, bucket, action);
                row.regret_delta[offset] += opponent_reach * (action_values[action] - node_values[bucket]);
            }
        }

        double return_value = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            return_value += average_strategy[action] * action_values[action];
        }
        return return_value;
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
    std::vector<double> bucket_strategy(action_count, 0.0);
    for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
        double node_value = 0.0;
        fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto probability = bucket_strategy[action];
            const auto offset = row_value_index(row_meta, bucket, action);
            row.strategy_delta[offset] += own_reach * probability;
            node_value += probability * action_values[action];
        }

        if (meta.player == context.traversing_player) {
            const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = row_value_index(row_meta, bucket, action);
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
    if (config_.use_sparse_storage) {
        return;
    }

    const auto strategy_start = std::chrono::steady_clock::now();
    std::vector<double> regrets;
    std::vector<double> strategy;

    for (const auto& meta : infoset_meta_) {
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

void HUNLFlatMCCFR::fill_current_strategy_bucket(
    InfosetId infoset_id,
    std::size_t bucket,
    double* out,
    std::size_t action_count) const {
    const auto& meta = infoset_meta(infoset_id);
    if (out == nullptr || action_count == 0) {
        return;
    }

    if (!config_.use_sparse_storage) {
        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
            const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                out[action] = infoset_table_.current_strategy_value(infoset_id, bucket_offset + action);
            }
            return;
        }
        for (std::size_t action = 0; action < action_count; ++action) {
            out[action] = infoset_table_.current_strategy_value(
                infoset_id,
                action * static_cast<std::size_t>(meta.bucket_count) + bucket);
        }
        return;
    }

    const auto row = sparse_row_or_empty(infoset_id);
    if (row.empty() || bucket >= row.bucket_count) {
        const auto uniform = 1.0 / static_cast<double>(action_count);
        for (std::size_t action = 0; action < action_count; ++action) {
            out[action] = uniform;
        }
        return;
    }

    double positive_total = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto regret = row.regret[HUNLSampledStorage::value_index(
            row.layout,
            row.bucket_count,
            row.action_count,
            bucket,
            action)];
        out[action] = std::max(static_cast<double>(regret), 0.0);
        positive_total += out[action];
    }
    if (positive_total > 0.0) {
        for (std::size_t action = 0; action < action_count; ++action) {
            out[action] /= positive_total;
        }
        return;
    }

    const auto uniform = 1.0 / static_cast<double>(action_count);
    for (std::size_t action = 0; action < action_count; ++action) {
        out[action] = uniform;
    }
}

std::vector<double> HUNLFlatMCCFR::average_action_probabilities(InfosetId infoset_id) const {
    const auto& meta = infoset_meta(infoset_id);
    std::vector<double> averaged(meta.action_count, 0.0);
    if (meta.bucket_count == 0) {
        return averaged;
    }

    std::vector<double> bucket_strategy(meta.action_count, 0.0);
    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        fill_current_strategy_bucket(infoset_id, bucket, bucket_strategy.data(), meta.action_count);
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            averaged[action] += bucket_strategy[action];
        }
    }
    for (auto& probability : averaged) {
        probability /= static_cast<double>(meta.bucket_count);
    }
    return averaged;
}

std::vector<double> HUNLFlatMCCFR::average_strategy_sum_values(InfosetId infoset_id) const {
    const auto& meta = infoset_meta(infoset_id);
    std::vector<double> averaged(meta.action_count, 0.0);
    if (meta.bucket_count == 0) {
        return averaged;
    }

    const auto row = config_.use_sparse_storage ? sparse_row_or_empty(infoset_id) : HUNLSampledConstRowView{};
    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            double value = 0.0;
            if (config_.use_sparse_storage) {
                value = row.empty() ? 0.0 : row.strategy_sum[row_value_index(meta, bucket, action)];
            } else {
                value = infoset_table_.strategy_sum_value(infoset_id, row_value_index(meta, bucket, action));
            }
            averaged[action] += value;
        }
    }
    for (auto& value : averaged) {
        value /= static_cast<double>(meta.bucket_count);
    }
    return averaged;
}

std::vector<double> HUNLFlatMCCFR::average_strategy_sampling_probabilities(InfosetId infoset_id) const {
    const auto strategy_sum = average_strategy_sum_values(infoset_id);
    std::vector<double> rho(strategy_sum.size(), 1.0);
    double row_strategy_sum = 0.0;
    for (const auto value : strategy_sum) {
        row_strategy_sum += std::max(value, 0.0);
    }

    const auto denominator = config_.as_beta + row_strategy_sum;
    for (std::size_t action = 0; action < strategy_sum.size(); ++action) {
        double probability = 1.0;
        if (denominator > 0.0) {
            probability = (config_.as_beta + config_.as_tau * std::max(strategy_sum[action], 0.0)) / denominator;
        }
        probability = std::max(config_.as_epsilon, probability);
        rho[action] = std::min(1.0, probability);
    }
    return rho;
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
        worker_profile.as_actions_considered += scratch.counters.as_actions_considered;
        worker_profile.as_actions_sampled += scratch.counters.as_actions_sampled;
        worker_profile.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
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
    for (const auto& scratch : worker_scratch_) {
        profile_.as_actions_considered += scratch.counters.as_actions_considered;
        profile_.as_actions_sampled += scratch.counters.as_actions_sampled;
        profile_.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
    }

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
        const auto& meta = infoset_meta(row.id);
        if (config_.use_sparse_storage) {
            auto sparse_row = ensure_sparse_row(row.id);
            for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
                sparse_row.regret[offset] =
                    static_cast<float>(static_cast<double>(sparse_row.regret[offset]) + row.regret_delta[offset]);
                sparse_row.strategy_sum[offset] = static_cast<float>(
                    static_cast<double>(sparse_row.strategy_sum[offset]) + row.strategy_delta[offset]);
            }
            continue;
        }

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
    last_iteration_counters_.as_actions_considered += scratch.counters.as_actions_considered;
    last_iteration_counters_.as_actions_sampled += scratch.counters.as_actions_sampled;
    last_iteration_counters_.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
}

std::unordered_map<std::string, std::vector<double>> HUNLFlatMCCFR::export_average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    out.reserve(graph_.infosets.size());

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = infoset_meta(infoset.id);
        std::vector<double> average(meta.value_count, 0.0);
        const auto row = config_.use_sparse_storage ? sparse_row_or_empty(infoset.id) : HUNLSampledConstRowView{};

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
            double total = 0.0;
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                double value = 0.0;
                if (config_.use_sparse_storage) {
                    value = row.empty()
                        ? 0.0
                        : row.strategy_sum[row_value_index(meta, bucket, action)];
                } else if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                    value = infoset_table_.strategy_sum_value(
                        infoset.id,
                        action * static_cast<std::size_t>(meta.bucket_count) + bucket);
                } else {
                    value = infoset_table_.strategy_sum_value(infoset.id, bucket_offset + action);
                }
                average[bucket_offset + action] = value;
                total += value;
            }

            if (total > 0.0) {
                normalize(
                    average.data() + bucket_offset,
                    meta.action_count,
                    total);
                continue;
            }

            const auto uniform = meta.action_count == 0 ? 0.0 : 1.0 / static_cast<double>(meta.action_count);
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                average[bucket_offset + action] = uniform;
            }
        }

        out.emplace(std::string(graph_.infoset_key(infoset)), std::move(average));
    }

    return out;
}

HUNLFlatAverageStrategyTable HUNLFlatMCCFR::export_average_strategy_table() const {
    HUNLFlatAverageStrategyTable out;
    out.layout = config_.use_sparse_storage ? sparse_storage_.layout() : infoset_table_.layout();
    out.meta = infoset_meta_;
    const auto total_value_count = out.meta.empty()
        ? 0U
        : out.meta.back().offset + out.meta.back().value_count;
    out.values.assign(total_value_count, 0.0);

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = out.meta.at(infoset.id.value);
        auto* average = out.values.data() + meta.offset;
        const auto row = config_.use_sparse_storage ? sparse_row_or_empty(infoset.id) : HUNLSampledConstRowView{};
        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            double total = 0.0;
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                double value = 0.0;
                if (config_.use_sparse_storage) {
                    value = row.empty() ? 0.0 : row.strategy_sum[row_value_index(meta, bucket, action)];
                } else {
                    value = infoset_table_.strategy_sum_value(
                        infoset.id,
                        row_value_index(meta, bucket, action));
                }
                average[row_value_index(meta, bucket, action)] = value;
                total += value;
            }

            if (total > 0.0) {
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    average[row_value_index(meta, bucket, action)] /= total;
                }
                continue;
            }

            const auto uniform = meta.action_count == 0 ? 0.0 : 1.0 / static_cast<double>(meta.action_count);
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                average[row_value_index(meta, bucket, action)] = uniform;
            }
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

bool HUNLFlatMCCFR::using_sparse_storage() const noexcept {
    return config_.use_sparse_storage;
}

const HUNLSampledStorage& HUNLFlatMCCFR::sparse_storage() const noexcept {
    return sparse_storage_;
}

double HUNLFlatMCCFR::average_strategy_sampling_ratio() const noexcept {
    if (total_counters_.as_actions_considered == 0U) {
        return 0.0;
    }
    return static_cast<double>(total_counters_.as_actions_sampled) /
        static_cast<double>(total_counters_.as_actions_considered);
}

}  // namespace core
