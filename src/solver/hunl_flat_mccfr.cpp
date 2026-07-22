#include "solver/hunl_flat_mccfr.hpp"

#include "util/pcs.hpp"
#include "util/simd.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <limits>

namespace core {

namespace {

template <class T, class Allocator>
std::uint64_t vector_storage_bytes(const std::vector<T, Allocator>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

std::uint64_t graph_storage_bytes(const HUNLFlatSolveGraph& graph) {
    std::uint64_t bytes = 0;
    bytes += vector_storage_bytes(graph.node_meta);
    bytes += vector_storage_bytes(graph.children);
    bytes += vector_storage_bytes(graph.actions);
    bytes += vector_storage_bytes(graph.chance_outcomes);
    bytes += vector_storage_bytes(graph.infosets);
    bytes += vector_storage_bytes(graph.infoset_debug_keys);
    bytes += vector_storage_bytes(graph.infoset_nodes);
    bytes += vector_storage_bytes(graph.forward_order);
    bytes += vector_storage_bytes(graph.reverse_order);
    bytes += vector_storage_bytes(graph.node_depths);
    bytes += vector_storage_bytes(graph.street_order);
    bytes += vector_storage_bytes(graph.depth_slices);
    bytes += vector_storage_bytes(graph.depth_order);
    bytes += vector_storage_bytes(graph.terminal_nodes);
    bytes += vector_storage_bytes(graph.terminal_node_values);
    bytes += vector_storage_bytes(graph.fold_terminal_nodes);
    bytes += vector_storage_bytes(graph.fold_terminal_values);
    bytes += vector_storage_bytes(graph.showdown_terminal_nodes);
    bytes += vector_storage_bytes(graph.showdown_terminal_values);
    for (const auto& key : graph.infoset_debug_keys) {
        bytes += static_cast<std::uint64_t>(key.capacity());
    }
    for (const auto& ranges : graph.depth_worker_ranges) {
        bytes += vector_storage_bytes(ranges);
    }
    return bytes;
}

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
    if (config.dcfr_alpha < 0.0 || config.dcfr_beta < 0.0 || config.dcfr_gamma < 0.0) {
        throw std::invalid_argument("HUNLFlatMCCFR DCFR exponents must be non-negative");
    }
    if (config.baseline_mode != HUNLFlatBaselineMode::None &&
        config.baseline_mode != HUNLFlatBaselineMode::MovingAverage) {
        throw std::invalid_argument(
            "HUNLFlatMCCFR baseline_mode currently supports None and MovingAverage only");
    }
}

std::vector<HUNLFlatInfosetTableMeta> build_infoset_meta(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& bucket_count_per_player,
    HUNLFlatValueLayout layout) {
    std::vector<HUNLFlatInfosetTableMeta> meta;
    meta.reserve(graph.infosets.size());

    std::size_t value_offset = 0;
    std::size_t bucket_offset = 0;
    for (const auto& infoset : graph.infosets) {
        HUNLFlatInfosetTableMeta row_meta;
        row_meta.id = infoset.id;
        row_meta.player = infoset.player;
        row_meta.action_count = infoset.action_count;
        row_meta.bucket_offset = bucket_offset;
        row_meta.bucket_count =
            infoset.player >= 0 && infoset.player < 2
            ? bucket_count_per_player[infoset.player]
            : 0U;
        row_meta.hand_count = row_meta.bucket_count;
        row_meta.reach_count = row_meta.bucket_count;
        row_meta.offset = value_offset;
        if (row_meta.bucket_count != 0U &&
            row_meta.bucket_count > std::numeric_limits<std::size_t>::max() / row_meta.action_count) {
            throw std::length_error("HUNLFlatMCCFR infoset value count overflow");
        }
        row_meta.value_count = row_meta.bucket_count * static_cast<std::size_t>(row_meta.action_count);
        if (value_offset > std::numeric_limits<std::size_t>::max() - row_meta.value_count ||
            bucket_offset > std::numeric_limits<std::size_t>::max() - row_meta.bucket_count) {
            throw std::length_error("HUNLFlatMCCFR infoset offset overflow");
        }
        meta.push_back(row_meta);
        value_offset += row_meta.value_count;
        bucket_offset += row_meta.bucket_count;
    }

    (void)layout;
    return meta;
}

std::pair<std::size_t, double> sample_weighted_prefix(
    const double* weights,
    std::size_t count,
    PcsRng& rng) {
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        total += std::max(0.0, weights[i]);
    }
    if (total <= 0.0) {
        return {0U, count == 0 ? 0.0 : 1.0 / static_cast<double>(count)};
    }

    double draw = rng.next_unit_f64() * total;
    for (std::size_t i = 0; i < count; ++i) {
        const auto weight = std::max(0.0, weights[i]);
        if (draw < weight) {
            return {i, weight / total};
        }
        draw -= weight;
    }

    const auto fallback_weight = std::max(0.0, weights[count - 1U]);
    return {count - 1U, fallback_weight / total};
}

double elapsed_seconds_since(std::chrono::steady_clock::time_point start) noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct DenseTraversalRowView {
    const double* current_strategy = nullptr;
    const double* regret = nullptr;
    const double* strategy_sum = nullptr;
    double* regret_delta = nullptr;
    double* strategy_delta = nullptr;
    std::size_t bucket_count = 0;
    std::size_t action_count = 0;
    std::size_t action_stride = 0;

    [[nodiscard]] bool valid() const noexcept {
        return current_strategy != nullptr &&
            regret_delta != nullptr &&
            strategy_delta != nullptr &&
            action_stride == bucket_count;
    }
};

DenseTraversalRowView build_dense_traversal_row_view(
    const HUNLFlatInfosetTable& infoset_table,
    InfosetId infoset_id,
    std::size_t bucket_count,
    std::size_t action_count,
    double* regret_delta,
    double* strategy_delta) noexcept {
    DenseTraversalRowView row;
    row.current_strategy = infoset_table.current_strategy(infoset_id);
    row.regret = infoset_table.regret(infoset_id);
    row.strategy_sum = infoset_table.strategy_sum(infoset_id);
    row.regret_delta = regret_delta;
    row.strategy_delta = strategy_delta;
    row.bucket_count = bucket_count;
    row.action_count = action_count;
    row.action_stride = bucket_count;
    return row;
}

void fill_current_strategy_bucket_dense(
    const DenseTraversalRowView& row,
    std::size_t bucket,
    double* out) noexcept {
    if (out == nullptr || row.current_strategy == nullptr || bucket >= row.bucket_count) {
        return;
    }
    const auto* bucket_base = row.current_strategy + bucket;
    for (std::size_t action = 0; action < row.action_count; ++action) {
        out[action] = bucket_base[action * row.action_stride];
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
      infoset_table_(),
      sparse_storage_(layout, HUNLFlatStoragePrecision::Float32),
      config_(config),
      infoset_meta_(build_infoset_meta(graph_, bucket_count_per_player, layout)),
      average_policy_cache_(graph_.infosets.size()),
      average_strategy_sampling_cache_(graph_.infosets.size()),
      traversal_node_meta_(graph_.node_meta.size()),
      chance_total_probability_(graph_.node_meta.size(), 0.0),
      worker_count_(std::max<std::size_t>(1, workers)),
      worker_scratch_(std::max<std::size_t>(1, workers)),
      current_worker_batches_(std::max<std::size_t>(1, workers)),
      touched_infosets_(graph_.infosets.size(), 0U),
      graph_memory_bytes_(graph_storage_bytes(graph_)),
      infoset_action_baselines_(graph_.infosets.size()),
      infoset_action_baseline_counts_(graph_.infosets.size()),
      node_action_baselines_(graph_.node_meta.size()),
      node_action_baseline_counts_(graph_.node_meta.size()) {
    validate_mccfr_config(config_);
    if (!config_.use_sparse_storage || config_.keep_dense_validation_backend) {
        infoset_table_ = HUNLFlatInfosetTable::build(graph_, bucket_count_per_player, layout, precision);
    }
    initialize_sparse_infoset_shapes(bucket_count_per_player);
    for (const auto& meta : infoset_meta_) {
        average_policy_cache_[meta.id.value].assign(meta.action_count, 0.0);
        average_strategy_sampling_cache_[meta.id.value].assign(meta.action_count, 1.0);
    }
    initialize_traversal_node_metadata();
    initialize_chance_sampling_metadata();
    profile_.workers.resize(worker_count_);
    profile_.sampled_simd_backend = hunl_sampled_simd_backend();
    const auto max_bucket_count = std::max(bucket_count_per_player[0], bucket_count_per_player[1]);
    for (auto& scratch : worker_scratch_) {
        scratch.prepare(graph_.max_actions, max_bucket_count, graph_.max_depth + 1U);
        scratch.prepare_delta_rows(infoset_meta_);
    }
    initialize_worker_threads();
}

HUNLFlatMCCFR::~HUNLFlatMCCFR() {
    shutdown_worker_threads();
}

void HUNLFlatMCCFR::run_iteration() {
    while (!run_next_player_subbatch()) {
    }
}

void HUNLFlatMCCFR::run_iterations(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        run_iteration();
    }
}

std::uint64_t HUNLFlatMCCFR::run_batches(std::uint64_t batch_count) {
    std::uint64_t completed = 0;
    while (completed < batch_count) {
        run_next_player_subbatch();
        ++completed;
    }
    return completed;
}

HUNLFlatMCCFR::DeadlineSolveResult HUNLFlatMCCFR::run_until(
    std::chrono::steady_clock::time_point deadline,
    std::size_t snapshot_stride_batches) {
    DeadlineSolveResult result;
    const auto stride = std::max<std::size_t>(1, snapshot_stride_batches);
    HUNLSampledRootStrategy previous_root = export_root_average_strategy();
    bool started_any_batch = false;

    while (std::chrono::steady_clock::now() < deadline) {
        run_next_player_subbatch();
        started_any_batch = true;
        ++result.batches_completed;

        if ((result.batches_completed % stride) == 0U) {
            const auto current_root = export_root_average_strategy();
            const auto delta = action_probability_delta(previous_root, current_root);
            auto snapshot = export_root_snapshot(0, delta, result.batches_completed, false);
            snapshot.sampled_nodes_visited =
                total_counters_.nodes_visited + last_iteration_counters_.nodes_visited;
            previous_root = current_root;
            result.latest_snapshot = snapshot;
            result.snapshots.push_back(snapshot);
        }
    }

    result.timed_out = std::chrono::steady_clock::now() >= deadline;

    if (!started_any_batch || result.snapshots.empty()) {
        const auto current_root = export_root_average_strategy();
        result.latest_snapshot = export_root_snapshot(
            0,
            action_probability_delta(previous_root, current_root),
            result.batches_completed,
            std::chrono::steady_clock::now() >= deadline);
        result.snapshots.push_back(result.latest_snapshot);
    } else if (result.timed_out) {
        result.latest_snapshot.timed_out = true;
        result.snapshots.back().timed_out = true;
    }

    result.iterations_completed = iterations_;
    return result;
}

HUNLFlatMCCFR::DeadlineSolveResult HUNLFlatMCCFR::solve_for(
    std::chrono::milliseconds budget,
    std::size_t snapshot_stride_batches) {
    const auto start = std::chrono::steady_clock::now();
    auto result = run_until(start + std::max<std::chrono::milliseconds>(budget, std::chrono::milliseconds{0}), snapshot_stride_batches);
    return result;
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

void HUNLFlatMCCFR::WorkerScratch::prepare(
    std::size_t max_actions,
    std::size_t max_bucket_count,
    std::size_t max_depth) {
    action_values.resize(max_actions, 0.0);
    average_strategy.resize(max_actions, 0.0);
    sampled_action_indices.resize(max_actions, 0U);
    bucket_strategy.resize(max_actions, 0.0);
    inclusion_probabilities.resize(max_actions, 0.0);
    strategy_sum_values.resize(max_actions, 0.0);
    sampled_actions.resize(max_actions, 0U);
    node_values.resize(max_bucket_count, 0.0);
    external_frames.resize(max_depth);
    external_frame_action_values.resize(max_depth * max_actions, 0.0);
}

void HUNLFlatMCCFR::WorkerScratch::prepare_delta_rows(
    const std::vector<HUNLFlatInfosetTableMeta>& infoset_meta) {
    delta_rows.resize(infoset_meta.size());
    row_active.assign(infoset_meta.size(), 0U);
    dirty_row_ids.clear();
    dirty_row_ids.reserve(infoset_meta.size());
    for (const auto& meta : infoset_meta) {
        auto& row = delta_rows[meta.id.value];
        row.id = meta.id;
        row.regret_delta.assign(meta.value_count, 0.0);
        row.strategy_delta.assign(meta.value_count, 0.0);
    }
}

void HUNLFlatMCCFR::initialize_traversal_node_metadata() {
    traversal_node_meta_.assign(graph_.node_meta.size(), {});
    for (std::size_t node_idx = 0; node_idx < graph_.node_meta.size(); ++node_idx) {
        const auto& source = graph_.node_meta[node_idx];
        auto& meta = traversal_node_meta_[node_idx];
        meta.child_begin = source.child_begin;
        meta.chance_begin = source.chance_begin;
        meta.infoset_id = source.infoset_id;
        meta.terminal_utility = source.terminal_utility;
        meta.infoset_meta_index = source.infoset_id.value;
        meta.player = source.player;
        meta.type = source.type;
        meta.action_count = source.action_count;
        meta.chance_count = source.chance_count;
        meta.has_infoset = source.has_infoset;
    }
}

void HUNLFlatMCCFR::initialize_chance_sampling_metadata() {
    chance_total_probability_.assign(graph_.node_meta.size(), 0.0);
    for (std::size_t node_idx = 0; node_idx < graph_.node_meta.size(); ++node_idx) {
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type != HUNLFlatNodeType::Chance || meta.chance_count == 0U) {
            continue;
        }

        double total_probability = 0.0;
        const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
        for (std::size_t i = 0; i < meta.chance_count; ++i) {
            total_probability += outcomes[i].probability;
        }
        chance_total_probability_[node_idx] = total_probability;
    }
}

void HUNLFlatMCCFR::WorkerScratch::clear_keep_capacity() noexcept {
    for (const auto infoset_id : dirty_row_ids) {
        auto& row = delta_rows[infoset_id.value];
        std::fill(row.regret_delta.begin(), row.regret_delta.end(), 0.0);
        std::fill(row.strategy_delta.begin(), row.strategy_delta.end(), 0.0);
        row_active[infoset_id.value] = 0U;
    }
    dirty_row_ids.clear();
    infoset_baseline_rows.clear();
    infoset_baseline_lookup.clear();
    node_baseline_rows.clear();
    node_baseline_lookup.clear();
    counters = {};
    audit = {};
    last_trajectory_seconds = 0.0;
}

HUNLFlatMCCFR::WorkerDeltaRow& HUNLFlatMCCFR::WorkerScratch::ensure_row(InfosetId id) {
    if (id.value >= delta_rows.size()) {
        throw std::out_of_range("HUNLFlatMCCFR worker delta row id out of range");
    }
    if (row_active[id.value] == 0U) {
        row_active[id.value] = 1U;
        dirty_row_ids.push_back(id);
    }
    return delta_rows[id.value];
}

HUNLFlatMCCFR::WorkerBaselineRow& HUNLFlatMCCFR::WorkerScratch::ensure_infoset_baseline_row(
    InfosetId id,
    std::size_t action_count) {
    const auto it = infoset_baseline_lookup.find(id);
    if (it != infoset_baseline_lookup.end()) {
        return infoset_baseline_rows[it->second];
    }

    const auto index = infoset_baseline_rows.size();
    infoset_baseline_lookup.emplace(id, index);
    infoset_baseline_rows.push_back(
        WorkerBaselineRow{static_cast<std::uint32_t>(id.value), std::vector<double>(action_count, 0.0), std::vector<std::uint32_t>(action_count, 0U)});
    return infoset_baseline_rows.back();
}

HUNLFlatMCCFR::WorkerBaselineRow& HUNLFlatMCCFR::WorkerScratch::ensure_node_baseline_row(
    std::uint32_t node_idx,
    std::size_t action_count) {
    const auto it = node_baseline_lookup.find(node_idx);
    if (it != node_baseline_lookup.end()) {
        return node_baseline_rows[it->second];
    }

    const auto index = node_baseline_rows.size();
    node_baseline_lookup.emplace(node_idx, index);
    node_baseline_rows.push_back(
        WorkerBaselineRow{node_idx, std::vector<double>(action_count, 0.0), std::vector<std::uint32_t>(action_count, 0U)});
    return node_baseline_rows.back();
}

bool HUNLFlatMCCFR::variance_reduction_enabled() const noexcept {
    return config_.baseline_mode == HUNLFlatBaselineMode::MovingAverage;
}

double HUNLFlatMCCFR::infoset_action_baseline(InfosetId infoset_id, std::size_t action) const noexcept {
    if (!variance_reduction_enabled() || infoset_id.value >= infoset_action_baselines_.size()) {
        return 0.0;
    }
    const auto& row = infoset_action_baselines_[infoset_id.value];
    return action < row.size() ? static_cast<double>(row[action]) : 0.0;
}

double HUNLFlatMCCFR::node_action_baseline(std::uint32_t node_idx, std::size_t action) const noexcept {
    if (!variance_reduction_enabled() || node_idx >= node_action_baselines_.size()) {
        return 0.0;
    }
    const auto& row = node_action_baselines_[node_idx];
    return action < row.size() ? static_cast<double>(row[action]) : 0.0;
}

void HUNLFlatMCCFR::observe_infoset_action_baseline(
    WorkerScratch& scratch,
    InfosetId infoset_id,
    std::size_t action_count,
    std::size_t action,
    double sample) {
    if (!variance_reduction_enabled() || action >= action_count) {
        return;
    }
    auto& row = scratch.ensure_infoset_baseline_row(infoset_id, action_count);
    row.value_sum[action] += sample;
    ++row.sample_count[action];
}

void HUNLFlatMCCFR::observe_node_action_baseline(
    WorkerScratch& scratch,
    std::uint32_t node_idx,
    std::size_t action_count,
    std::size_t action,
    double sample) {
    if (!variance_reduction_enabled() || action >= action_count) {
        return;
    }
    auto& row = scratch.ensure_node_baseline_row(node_idx, action_count);
    row.value_sum[action] += sample;
    ++row.sample_count[action];
}

void HUNLFlatMCCFR::record_variance_sample(
    Counters& counters,
    double raw_estimate,
    double corrected_estimate) noexcept {
    ++counters.variance_samples;
    counters.raw_estimate_sum += raw_estimate;
    counters.raw_estimate_sq_sum += raw_estimate * raw_estimate;
    counters.corrected_estimate_sum += corrected_estimate;
    counters.corrected_estimate_sq_sum += corrected_estimate * corrected_estimate;
}

double HUNLFlatMCCFR::estimate_variance(
    std::uint64_t sample_count,
    double value_sum,
    double value_sq_sum) noexcept {
    if (sample_count == 0U) {
        return 0.0;
    }
    const auto mean = value_sum / static_cast<double>(sample_count);
    return std::max(0.0, value_sq_sum / static_cast<double>(sample_count) - mean * mean);
}

void HUNLFlatMCCFR::add_sampled_kernel_profile(double seconds, HUNLSampledSimdBackend backend) noexcept {
    profile_.sampled_simd_backend = hunl_sampled_simd_backend();
    if (backend == HUNLSampledSimdBackend::Avx2Fma) {
        profile_.sampled_kernel_simd_seconds += seconds;
        ++profile_.sampled_kernel_simd_calls;
        return;
    }
    profile_.sampled_kernel_scalar_seconds += seconds;
    ++profile_.sampled_kernel_scalar_calls;
}

double HUNLFlatMCCFR::traverse(std::uint32_t node_idx, TraversalContext& context) {
    if (context.traverse_impl == nullptr) {
        throw std::logic_error("HUNLFlatMCCFR traversal kernel is not set");
    }
    return (this->*context.traverse_impl)(node_idx, context);
}

bool HUNLFlatMCCFR::can_use_dense_validation_infoset_action_hand_fast_path() const noexcept {
    return !config_.use_sparse_storage &&
        infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand &&
        infoset_table_.precision() == HUNLFlatStoragePrecision::Float64;
}

bool HUNLFlatMCCFR::can_use_iterative_external_dense_traversal() const noexcept {
    return config_.use_iterative_external_dense_traversal &&
        config_.mode == HUNLFlatSamplingMode::External &&
        !variance_reduction_enabled() &&
        can_use_dense_validation_infoset_action_hand_fast_path();
}

double HUNLFlatMCCFR::traverse_exact(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
            double value = 0.0;
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = outcomes[i];
                value += outcome.probability * traverse_exact(outcome.child, context);
            }
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
                if (meta.player == context.traversing_player) {
                    context.counters->traversing_player_action_expansions +=
                        static_cast<std::uint64_t>(meta.action_count);
                }
            }
            return traverse_full_expansion_decision(
                meta,
                context,
                meta.player == context.traversing_player);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_public_chance(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_public_chance(sampled.child, context);
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return sampled_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
                if (meta.player == context.traversing_player) {
                    context.counters->traversing_player_action_expansions +=
                        static_cast<std::uint64_t>(meta.action_count);
                }
            }
            return traverse_full_expansion_decision(
                meta,
                context,
                meta.player == context.traversing_player);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_public_chance_vr(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_public_chance_vr(sampled.child, context);

            double expected_baseline = 0.0;
            const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = outcomes[i];
                expected_baseline += outcome.probability * node_action_baseline(node_idx, i);
            }
            const auto corrected_value =
                sampled_value - node_action_baseline(node_idx, sampled_index) + expected_baseline;
            observe_node_action_baseline(*context.scratch, node_idx, meta.chance_count, sampled_index, sampled_value);
            if (context.counters != nullptr) {
                record_variance_sample(*context.counters, sampled_value, corrected_value);
            }
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return corrected_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
                if (meta.player == context.traversing_player) {
                    context.counters->traversing_player_action_expansions +=
                        static_cast<std::uint64_t>(meta.action_count);
                }
            }
            return traverse_full_expansion_decision(
                meta,
                context,
                meta.player == context.traversing_player);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_external(std::uint32_t node_idx, TraversalContext& context) {
    if (can_use_iterative_external_dense_traversal()) {
        return traverse_external_dense_iterative(node_idx, context);
    }
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_external(sampled.child, context);
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return sampled_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
                if (meta.player == context.traversing_player) {
                    context.counters->traversing_player_action_expansions +=
                        static_cast<std::uint64_t>(meta.action_count);
                }
            }
            if (can_use_dense_validation_infoset_action_hand_fast_path()) {
                if (meta.player != context.traversing_player) {
                    return traverse_opponent_sampled_decision_dense_validation(meta, context, false);
                }
                return traverse_full_expansion_decision_dense_validation(meta, context, true);
            }
            if (meta.player != context.traversing_player) {
                return traverse_opponent_sampled_decision_generic(meta, context, false);
            }
            return traverse_full_expansion_decision_generic(meta, context, true);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_external_dense_iterative(std::uint32_t node_idx, TraversalContext& context) {
    if (context.scratch == nullptr || context.rng == nullptr) {
        throw std::logic_error("HUNLFlatMCCFR iterative traversal requires scratch and rng");
    }

    auto& scratch = *context.scratch;
    const auto max_actions = static_cast<std::size_t>(graph_.max_actions);
    if (max_actions == 0U) {
        throw std::logic_error("HUNLFlatMCCFR iterative traversal requires positive max_actions");
    }

    std::size_t stack_size = 0;
    double last_value = 0.0;
    scratch.external_frames[stack_size++] = ExternalTraversalFrame{
        node_idx,
        0U,
        0U,
        ExternalTraversalFrame::Stage::Enter,
        context.p0,
        context.p1,
        0.0,
    };

    while (stack_size > 0U) {
        auto& frame = scratch.external_frames[stack_size - 1U];
        const auto& meta = traversal_node_meta_[frame.node_idx];

        switch (frame.stage) {
            case ExternalTraversalFrame::Stage::Enter: {
                if (context.counters != nullptr) {
                    ++context.counters->nodes_visited;
                }

                switch (meta.type) {
                    case HUNLFlatNodeType::TerminalFold:
                    case HUNLFlatNodeType::TerminalShowdown:
                    case HUNLFlatNodeType::DepthLimited:
                        last_value = meta.terminal_utility[context.traversing_player];
                        --stack_size;
                        continue;

                    case HUNLFlatNodeType::Chance: {
                        if (context.counters != nullptr) {
                            ++context.counters->chance_nodes_visited;
                        }
                        const auto chance_start = std::chrono::steady_clock::now();
                        const auto sampled_index = sample_chance_child(frame.node_idx, meta, *context.rng);
                        frame.sampled_index = static_cast<std::uint32_t>(sampled_index);
                        frame.stage = ExternalTraversalFrame::Stage::ResumeAfterChanceChild;
                        const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
                        scratch.audit.chance_seconds += elapsed_seconds_since(chance_start);
                        scratch.external_frames[stack_size++] = ExternalTraversalFrame{
                            sampled.child,
                            0U,
                            0U,
                            ExternalTraversalFrame::Stage::Enter,
                            frame.p0,
                            frame.p1,
                            0.0,
                        };
                        continue;
                    }

                    case HUNLFlatNodeType::Decision: {
                        if (!meta.has_infoset) {
                            throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
                        }
                        if (context.counters != nullptr) {
                            ++context.counters->decision_nodes_visited;
                            if (meta.player == context.traversing_player) {
                                context.counters->traversing_player_action_expansions +=
                                    static_cast<std::uint64_t>(meta.action_count);
                            }
                        }

                        if (meta.player != context.traversing_player) {
                            const auto opponent_start = std::chrono::steady_clock::now();
                            const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
                            const auto action_count = static_cast<std::size_t>(meta.action_count);
                            const auto& average_strategy_row = average_policy_cache_[meta.infoset_id.value];
                            const auto* average_strategy = average_strategy_row.data();
                            const auto bucket_count = static_cast<std::size_t>(row_meta.bucket_count);

                            if (context.counters != nullptr) {
                                ++context.counters->opponent_sampled_decisions;
                                context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
                                context.counters->opponent_strategy_values_written +=
                                    static_cast<std::uint64_t>(bucket_count) * static_cast<std::uint64_t>(action_count);
                            }

                            const auto own_reach = meta.player == 0 ? frame.p0 : frame.p1;
                            auto& row = scratch.ensure_row(meta.infoset_id);
                            const auto dense_row = build_dense_traversal_row_view(
                                infoset_table_,
                                meta.infoset_id,
                                row_meta.bucket_count,
                                action_count,
                                row.regret_delta.data(),
                                row.strategy_delta.data());
                            const auto row_writeback_start = std::chrono::steady_clock::now();
                            for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                                const auto action_offset = action * dense_row.action_stride;
                                const auto* current_strategy_action = dense_row.current_strategy + action_offset;
                                auto* strategy_delta_action = dense_row.strategy_delta + action_offset;
                                for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
                                    strategy_delta_action[bucket] += own_reach * current_strategy_action[bucket];
                                }
                            }
                            scratch.audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

                            const auto sampled = sample_weighted_prefix(average_strategy, action_count, *context.rng);
                            if (context.counters != nullptr) {
                                ++context.counters->sampled_opponent_actions;
                            }

                            frame.stage = ExternalTraversalFrame::Stage::ResumeAfterOpponentChild;
                            const auto* children = graph_.children.data() + meta.child_begin;
                            double child_p0 = frame.p0;
                            double child_p1 = frame.p1;
                            if (meta.player == 0) {
                                child_p0 *= average_strategy[sampled.first];
                            } else {
                                child_p1 *= average_strategy[sampled.first];
                            }
                            scratch.audit.opponent_sampled_seconds += elapsed_seconds_since(opponent_start);
                            scratch.external_frames[stack_size++] = ExternalTraversalFrame{
                                children[sampled.first],
                                0U,
                                0U,
                                ExternalTraversalFrame::Stage::Enter,
                                child_p0,
                                child_p1,
                                0.0,
                            };
                            continue;
                        }

                        const auto action_count = static_cast<std::size_t>(meta.action_count);
                        if (context.counters != nullptr) {
                            ++context.counters->traversing_player_full_expansion_decisions;
                            context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
                        }
                        frame.child_index = 0U;
                        frame.return_value = 0.0;
                        frame.stage = ExternalTraversalFrame::Stage::ResumeAfterTraversingChild;
                        const auto* children = graph_.children.data() + meta.child_begin;
                        const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
                        double child_p0 = frame.p0;
                        double child_p1 = frame.p1;
                        if (meta.player == 0) {
                            child_p0 *= average_strategy[0];
                        } else {
                            child_p1 *= average_strategy[0];
                        }
                        scratch.external_frames[stack_size++] = ExternalTraversalFrame{
                            children[0],
                            0U,
                            0U,
                            ExternalTraversalFrame::Stage::Enter,
                            child_p0,
                            child_p1,
                            0.0,
                        };
                        continue;
                    }
                }
                break;
            }

            case ExternalTraversalFrame::Stage::ResumeAfterChanceChild:
            case ExternalTraversalFrame::Stage::ResumeAfterOpponentChild:
                --stack_size;
                continue;

            case ExternalTraversalFrame::Stage::ResumeAfterTraversingChild: {
                const auto action_count = static_cast<std::size_t>(meta.action_count);
                auto* frame_action_values =
                    scratch.external_frame_action_values.data() + (stack_size - 1U) * max_actions;
                frame_action_values[frame.child_index] = last_value;
                ++frame.child_index;

                if (frame.child_index < action_count) {
                    const auto* children = graph_.children.data() + meta.child_begin;
                    const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
                    double child_p0 = frame.p0;
                    double child_p1 = frame.p1;
                    if (meta.player == 0) {
                        child_p0 *= average_strategy[frame.child_index];
                    } else {
                        child_p1 *= average_strategy[frame.child_index];
                    }
                    scratch.external_frames[stack_size++] = ExternalTraversalFrame{
                        children[frame.child_index],
                        0U,
                        0U,
                        ExternalTraversalFrame::Stage::Enter,
                        child_p0,
                        child_p1,
                        0.0,
                    };
                    continue;
                }

                const auto traversing_start = std::chrono::steady_clock::now();
                const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
                const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
                const auto own_reach = meta.player == 0 ? frame.p0 : frame.p1;
                auto& row = scratch.ensure_row(meta.infoset_id);
                const auto dense_row = build_dense_traversal_row_view(
                    infoset_table_,
                    meta.infoset_id,
                    row_meta.bucket_count,
                    action_count,
                    row.regret_delta.data(),
                    row.strategy_delta.data());
                const auto row_writeback_start = std::chrono::steady_clock::now();
                for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
                    double node_value = 0.0;
                    for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                        const auto offset = action * dense_row.action_stride + bucket;
                        const auto probability = dense_row.current_strategy[offset];
                        dense_row.strategy_delta[offset] += own_reach * probability;
                        node_value += probability * frame_action_values[action];
                    }

                    const auto opponent_reach = context.traversing_player == 0 ? frame.p1 : frame.p0;
                    for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                        const auto offset = action * dense_row.action_stride + bucket;
                        dense_row.regret_delta[offset] += opponent_reach * (frame_action_values[action] - node_value);
                    }
                }
                scratch.audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

                double return_value = 0.0;
                for (std::size_t action = 0; action < action_count; ++action) {
                    return_value += average_strategy[action] * frame_action_values[action];
                }
                scratch.audit.traversing_player_seconds += elapsed_seconds_since(traversing_start);
                last_value = return_value;
                --stack_size;
                continue;
            }
        }
    }

    return last_value;
}

double HUNLFlatMCCFR::traverse_external_vr(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_external_vr(sampled.child, context);

            double expected_baseline = 0.0;
            const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = outcomes[i];
                expected_baseline += outcome.probability * node_action_baseline(node_idx, i);
            }
            const auto corrected_value =
                sampled_value - node_action_baseline(node_idx, sampled_index) + expected_baseline;
            observe_node_action_baseline(*context.scratch, node_idx, meta.chance_count, sampled_index, sampled_value);
            if (context.counters != nullptr) {
                record_variance_sample(*context.counters, sampled_value, corrected_value);
            }
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return corrected_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
                if (meta.player == context.traversing_player) {
                    context.counters->traversing_player_action_expansions +=
                        static_cast<std::uint64_t>(meta.action_count);
                }
            }
            if (can_use_dense_validation_infoset_action_hand_fast_path()) {
                if (meta.player != context.traversing_player) {
                    return traverse_opponent_sampled_decision_dense_validation(meta, context, true);
                }
                return traverse_full_expansion_decision_dense_validation(meta, context, true);
            }
            if (meta.player != context.traversing_player) {
                return traverse_opponent_sampled_decision_generic(meta, context, true);
            }
            return traverse_full_expansion_decision_generic(meta, context, true);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_average_strategy(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_average_strategy(sampled.child, context);
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return sampled_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
            }
            if (meta.player != context.traversing_player) {
                return traverse_opponent_sampled_decision(meta, context, false);
            }
            return traverse_average_strategy_decision(meta, context, false);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_average_strategy_vr(std::uint32_t node_idx, TraversalContext& context) {
    if (context.counters != nullptr) {
        ++context.counters->nodes_visited;
    }
    const auto& meta = traversal_node_meta_[node_idx];
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return meta.terminal_utility[context.traversing_player];
        case HUNLFlatNodeType::Chance: {
            if (context.counters != nullptr) {
                ++context.counters->chance_nodes_visited;
            }
            const auto chance_start = std::chrono::steady_clock::now();
            const auto sampled_index = sample_chance_child(node_idx, meta, *context.rng);
            const auto& sampled = graph_.chance_outcomes[meta.chance_begin + sampled_index];
            const auto sampled_value = traverse_average_strategy_vr(sampled.child, context);

            double expected_baseline = 0.0;
            const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = outcomes[i];
                expected_baseline += outcome.probability * node_action_baseline(node_idx, i);
            }
            const auto corrected_value =
                sampled_value - node_action_baseline(node_idx, sampled_index) + expected_baseline;
            observe_node_action_baseline(*context.scratch, node_idx, meta.chance_count, sampled_index, sampled_value);
            if (context.counters != nullptr) {
                record_variance_sample(*context.counters, sampled_value, corrected_value);
            }
            if (context.scratch != nullptr) {
                context.scratch->audit.chance_seconds += elapsed_seconds_since(chance_start);
            }
            return corrected_value;
        }
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                throw std::logic_error("HUNLFlatMCCFR decision node missing infoset");
            }
            if (context.counters != nullptr) {
                ++context.counters->decision_nodes_visited;
            }
            if (meta.player != context.traversing_player) {
                return traverse_opponent_sampled_decision(meta, context, true);
            }
            return traverse_average_strategy_decision(meta, context, true);
    }
    throw std::logic_error("HUNLFlatMCCFR unknown node type");
}

double HUNLFlatMCCFR::traverse_full_expansion_decision(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool count_traversing_full_expansion) {
    if (can_use_dense_validation_infoset_action_hand_fast_path()) {
        return traverse_full_expansion_decision_dense_validation(
            meta,
            context,
            count_traversing_full_expansion);
    }
    return traverse_full_expansion_decision_generic(
        meta,
        context,
        count_traversing_full_expansion);
}

double HUNLFlatMCCFR::traverse_full_expansion_decision_dense_validation(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool count_traversing_full_expansion) {
    const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    auto& action_values = context.scratch->action_values;
    const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
    const auto* children = graph_.children.data() + meta.child_begin;

    const auto traversing_start = std::chrono::steady_clock::now();
    if (context.counters != nullptr) {
        context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
        if (count_traversing_full_expansion) {
            ++context.counters->traversing_player_full_expansion_decisions;
        }
    }

    for (std::size_t action = 0; action < action_count; ++action) {
        auto child_context = context;
        if (meta.player == 0) {
            child_context.p0 *= average_strategy[action];
        } else {
            child_context.p1 *= average_strategy[action];
        }
        action_values[action] = (this->*context.traverse_impl)(children[action], child_context);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    auto& row = context.scratch->ensure_row(meta.infoset_id);
    const auto dense_row = build_dense_traversal_row_view(
        infoset_table_,
        meta.infoset_id,
        row_meta.bucket_count,
        action_count,
        row.regret_delta.data(),
        row.strategy_delta.data());
    const auto row_writeback_start = std::chrono::steady_clock::now();
    for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
        double node_value = 0.0;
        for (std::size_t action = 0; action < dense_row.action_count; ++action) {
            const auto offset = action * dense_row.action_stride + bucket;
            const auto probability = dense_row.current_strategy[offset];
            dense_row.strategy_delta[offset] += own_reach * probability;
            node_value += probability * action_values[action];
        }

        if (meta.player == context.traversing_player) {
            const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
            for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                const auto offset = action * dense_row.action_stride + bucket;
                dense_row.regret_delta[offset] += opponent_reach * (action_values[action] - node_value);
            }
        }
    }
    context.scratch->audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

    double return_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        return_value += average_strategy[action] * action_values[action];
    }
    if (meta.player == context.traversing_player) {
        context.scratch->audit.traversing_player_seconds += elapsed_seconds_since(traversing_start);
    }
    return return_value;
}

double HUNLFlatMCCFR::traverse_full_expansion_decision_generic(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool count_traversing_full_expansion) {
    const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    auto& action_values = context.scratch->action_values;
    auto& bucket_strategy = context.scratch->bucket_strategy;
    const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
    const auto* children = graph_.children.data() + meta.child_begin;

    const auto traversing_start = std::chrono::steady_clock::now();
    if (context.counters != nullptr) {
        context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
        if (count_traversing_full_expansion) {
            ++context.counters->traversing_player_full_expansion_decisions;
        }
    }

    for (std::size_t action = 0; action < action_count; ++action) {
        auto child_context = context;
        if (meta.player == 0) {
            child_context.p0 *= average_strategy[action];
        } else {
            child_context.p1 *= average_strategy[action];
        }
        action_values[action] = (this->*context.traverse_impl)(children[action], child_context);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    auto& row = context.scratch->ensure_row(meta.infoset_id);
    const auto row_writeback_start = std::chrono::steady_clock::now();
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
    context.scratch->audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

    double return_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        return_value += average_strategy[action] * action_values[action];
    }
    if (meta.player == context.traversing_player) {
        context.scratch->audit.traversing_player_seconds += elapsed_seconds_since(traversing_start);
    }
    return return_value;
}

double HUNLFlatMCCFR::traverse_opponent_sampled_decision(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool use_variance_reduction) {
    if (can_use_dense_validation_infoset_action_hand_fast_path()) {
        return traverse_opponent_sampled_decision_dense_validation(meta, context, use_variance_reduction);
    }
    return traverse_opponent_sampled_decision_generic(meta, context, use_variance_reduction);
}

double HUNLFlatMCCFR::traverse_opponent_sampled_decision_dense_validation(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool use_variance_reduction) {
    const auto opponent_start = std::chrono::steady_clock::now();
    const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    const auto& average_strategy_row = average_policy_cache_[meta.infoset_id.value];
    const auto* average_strategy = average_strategy_row.data();
    const auto bucket_count = static_cast<std::size_t>(row_meta.bucket_count);

    if (context.counters != nullptr) {
        ++context.counters->opponent_sampled_decisions;
        context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
        context.counters->opponent_strategy_values_written +=
            static_cast<std::uint64_t>(bucket_count) * static_cast<std::uint64_t>(action_count);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    auto& row = context.scratch->ensure_row(meta.infoset_id);
    const auto dense_row = build_dense_traversal_row_view(
        infoset_table_,
        meta.infoset_id,
        row_meta.bucket_count,
        action_count,
        row.regret_delta.data(),
        row.strategy_delta.data());
    const auto row_writeback_start = std::chrono::steady_clock::now();
    for (std::size_t action = 0; action < dense_row.action_count; ++action) {
        const auto action_offset = action * dense_row.action_stride;
        const auto* current_strategy_action = dense_row.current_strategy + action_offset;
        auto* strategy_delta_action = dense_row.strategy_delta + action_offset;
        for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
            strategy_delta_action[bucket] += own_reach * current_strategy_action[bucket];
        }
    }
    context.scratch->audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

    const auto sampled = sample_weighted_prefix(average_strategy, action_count, *context.rng);
    if (context.counters != nullptr) {
        ++context.counters->sampled_opponent_actions;
    }

    auto child_context = context;
    if (meta.player == 0) {
        child_context.p0 *= average_strategy[sampled.first];
    } else {
        child_context.p1 *= average_strategy[sampled.first];
    }
    const auto* children = graph_.children.data() + meta.child_begin;
    const auto child_idx = children[sampled.first];
    const auto sampled_value = (this->*context.traverse_impl)(child_idx, child_context);
    if (!use_variance_reduction) {
        context.scratch->audit.opponent_sampled_seconds += elapsed_seconds_since(opponent_start);
        return sampled_value;
    }

    double expected_baseline = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        expected_baseline += average_strategy[action] * infoset_action_baseline(meta.infoset_id, action);
    }
    const auto corrected_value =
        sampled_value - infoset_action_baseline(meta.infoset_id, sampled.first) + expected_baseline;
    observe_infoset_action_baseline(
        *context.scratch,
        meta.infoset_id,
        action_count,
        sampled.first,
        sampled_value);
    if (context.counters != nullptr) {
        record_variance_sample(*context.counters, sampled_value, corrected_value);
    }
    context.scratch->audit.opponent_sampled_seconds += elapsed_seconds_since(opponent_start);
    return corrected_value;
}

double HUNLFlatMCCFR::traverse_opponent_sampled_decision_generic(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool use_variance_reduction) {
    const auto opponent_start = std::chrono::steady_clock::now();
    const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    auto& bucket_strategy = context.scratch->bucket_strategy;
    const auto& average_strategy_row = average_policy_cache_[meta.infoset_id.value];
    const auto* average_strategy = average_strategy_row.data();
    const auto bucket_count = static_cast<std::size_t>(row_meta.bucket_count);

    if (context.counters != nullptr) {
        ++context.counters->opponent_sampled_decisions;
        context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
        context.counters->opponent_strategy_values_written +=
            static_cast<std::uint64_t>(bucket_count) * static_cast<std::uint64_t>(action_count);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    auto& row = context.scratch->ensure_row(meta.infoset_id);
    const auto row_writeback_start = std::chrono::steady_clock::now();
    for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
        fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto probability = bucket_strategy[action];
            const auto offset = row_value_index(row_meta, bucket, action);
            row.strategy_delta[offset] += own_reach * probability;
        }
    }
    context.scratch->audit.row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

    const auto sampled = sample_weighted_prefix(average_strategy, action_count, *context.rng);
    if (context.counters != nullptr) {
        ++context.counters->sampled_opponent_actions;
    }

    auto child_context = context;
    if (meta.player == 0) {
        child_context.p0 *= average_strategy[sampled.first];
    } else {
        child_context.p1 *= average_strategy[sampled.first];
    }
    const auto* children = graph_.children.data() + meta.child_begin;
    const auto child_idx = children[sampled.first];
    const auto sampled_value = (this->*context.traverse_impl)(child_idx, child_context);
    if (!use_variance_reduction) {
        context.scratch->audit.opponent_sampled_seconds += elapsed_seconds_since(opponent_start);
        return sampled_value;
    }

    double expected_baseline = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        expected_baseline += average_strategy[action] * infoset_action_baseline(meta.infoset_id, action);
    }
    const auto corrected_value =
        sampled_value - infoset_action_baseline(meta.infoset_id, sampled.first) + expected_baseline;
    observe_infoset_action_baseline(
        *context.scratch,
        meta.infoset_id,
        action_count,
        sampled.first,
        sampled_value);
    if (context.counters != nullptr) {
        record_variance_sample(*context.counters, sampled_value, corrected_value);
    }
    context.scratch->audit.opponent_sampled_seconds += elapsed_seconds_since(opponent_start);
    return corrected_value;
}

double HUNLFlatMCCFR::traverse_average_strategy_decision(
    const TraversalNodeMeta& meta,
    TraversalContext& context,
    bool use_variance_reduction) {
    const auto as_start = std::chrono::steady_clock::now();
    const auto& row_meta = infoset_meta_[meta.infoset_meta_index];
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    auto& action_values = context.scratch->action_values;
    auto& bucket_strategy = context.scratch->bucket_strategy;
    auto& inclusion_probabilities = context.scratch->inclusion_probabilities;
    auto& sampled_actions = context.scratch->sampled_actions;
    auto& sampled_action_indices = context.scratch->sampled_action_indices;
    const auto& average_strategy = average_policy_cache_[meta.infoset_id.value];
    const auto* children = graph_.children.data() + meta.child_begin;

    if (context.counters != nullptr) {
        ++context.counters->as_decision_nodes;
        context.counters->decision_actions_touched += static_cast<std::uint64_t>(action_count);
    }

    const auto own_reach = meta.player == 0 ? context.p0 : context.p1;
    auto& row = context.scratch->ensure_row(meta.infoset_id);
    DenseTraversalRowView dense_row{};
    if (can_use_dense_validation_infoset_action_hand_fast_path()) {
        dense_row = build_dense_traversal_row_view(
            infoset_table_,
            meta.infoset_id,
            row_meta.bucket_count,
            action_count,
            row.regret_delta.data(),
            row.strategy_delta.data());
    }
    auto row_writeback_seconds = 0.0;
    auto row_writeback_start = std::chrono::steady_clock::now();
    if (dense_row.valid()) {
        for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                const auto offset = action * dense_row.action_stride + bucket;
                dense_row.strategy_delta[offset] += own_reach * dense_row.current_strategy[offset];
            }
        }
    } else {
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = row_value_index(row_meta, bucket, action);
                row.strategy_delta[offset] += own_reach * bucket_strategy[action];
            }
        }
    }
    row_writeback_seconds += elapsed_seconds_since(row_writeback_start);

    fill_average_strategy_sampling_probabilities(
        meta.infoset_id,
        inclusion_probabilities.data(),
        action_count,
        context.scratch->strategy_sum_values.data());
    std::size_t sampled_count = 0;
    if (context.counters != nullptr) {
        context.counters->as_actions_considered += static_cast<std::uint64_t>(action_count);
    }

    double rho_sum = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto rho = inclusion_probabilities[action];
        rho_sum += rho;
        const auto selected = context.rng->bernoulli(rho);
        sampled_actions[action] = selected ? 1U : 0U;
        if (!selected) {
            continue;
        }
        sampled_action_indices[sampled_count++] = action;
    }

    double none_selected_probability = 1.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        none_selected_probability *= (1.0 - inclusion_probabilities[action]);
    }

    if (sampled_count == 0 && action_count > 0) {
        const auto forced = rho_sum > 0.0
            ? sample_weighted_prefix(inclusion_probabilities.data(), action_count, *context.rng).first
            : std::size_t{0};
        sampled_actions[forced] = 1U;
        sampled_action_indices[0] = forced;
        sampled_count = 1;
        if (context.counters != nullptr) {
            ++context.counters->as_forced_at_least_one_count;
        }
    }

    if (rho_sum > 0.0) {
        for (std::size_t sampled_idx = 0; sampled_idx < sampled_count; ++sampled_idx) {
            const auto action = sampled_action_indices[sampled_idx];
            inclusion_probabilities[action] =
                std::min(
                    1.0,
                    inclusion_probabilities[action] +
                        none_selected_probability * (inclusion_probabilities[action] / rho_sum));
        }
    }

    if (context.counters != nullptr) {
        context.counters->as_actions_sampled += static_cast<std::uint64_t>(sampled_count);
        context.counters->traversing_player_action_expansions += static_cast<std::uint64_t>(sampled_count);
    }

    std::fill(action_values.begin(), action_values.begin() + action_count, 0.0);
    for (std::size_t sampled_idx = 0; sampled_idx < sampled_count; ++sampled_idx) {
        const auto action = sampled_action_indices[sampled_idx];
        auto child_context = context;
        if (meta.player == 0) {
            child_context.p0 *= average_strategy[action];
        } else {
            child_context.p1 *= average_strategy[action];
        }
        const auto inclusion_probability = std::max(inclusion_probabilities[action], config_.as_epsilon);
        const auto sampled_value = (this->*context.traverse_impl)(children[action], child_context);
        const auto raw_estimate = sampled_value / inclusion_probability;
        if (!use_variance_reduction) {
            action_values[action] = raw_estimate;
            continue;
        }

        const auto baseline = infoset_action_baseline(meta.infoset_id, action);
        action_values[action] = baseline + (sampled_value - baseline) / inclusion_probability;
        observe_infoset_action_baseline(
            *context.scratch,
            meta.infoset_id,
            action_count,
            action,
            sampled_value);
        if (context.counters != nullptr) {
            record_variance_sample(*context.counters, raw_estimate, action_values[action]);
        }
    }

    auto& node_values = context.scratch->node_values;
    std::fill(node_values.begin(), node_values.begin() + row_meta.bucket_count, 0.0);
    if (dense_row.valid()) {
        for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
            double node_value = 0.0;
            for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                const auto offset = action * dense_row.action_stride + bucket;
                node_value += dense_row.current_strategy[offset] * action_values[action];
            }
            node_values[bucket] = node_value;
        }
    } else {
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            fill_current_strategy_bucket(meta.infoset_id, bucket, bucket_strategy.data(), action_count);
            for (std::size_t action = 0; action < action_count; ++action) {
                node_values[bucket] += bucket_strategy[action] * action_values[action];
            }
        }
    }

    const auto opponent_reach = context.traversing_player == 0 ? context.p1 : context.p0;
    row_writeback_start = std::chrono::steady_clock::now();
    if (dense_row.valid()) {
        for (std::size_t bucket = 0; bucket < dense_row.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < dense_row.action_count; ++action) {
                const auto offset = action * dense_row.action_stride + bucket;
                dense_row.regret_delta[offset] += opponent_reach * (action_values[action] - node_values[bucket]);
            }
        }
    } else {
        for (std::size_t bucket = 0; bucket < row_meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < action_count; ++action) {
                const auto offset = row_value_index(row_meta, bucket, action);
                row.regret_delta[offset] += opponent_reach * (action_values[action] - node_values[bucket]);
            }
        }
    }
    row_writeback_seconds += elapsed_seconds_since(row_writeback_start);
    context.scratch->audit.row_writeback_seconds += row_writeback_seconds;

    double return_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        return_value += average_strategy[action] * action_values[action];
    }
    context.scratch->audit.average_strategy_seconds += elapsed_seconds_since(as_start);
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
        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            auto* regret = infoset_table_.regret_mut(meta.id);
            auto* current_strategy = infoset_table_.current_strategy_mut(meta.id);
            const auto kernel_start = std::chrono::steady_clock::now();
            regret_matching_action_major_f64(
                regret,
                meta.action_count,
                meta.bucket_count,
                current_strategy);
            add_sampled_kernel_profile(
                std::chrono::duration<double>(std::chrono::steady_clock::now() - kernel_start).count(),
                hunl_sampled_simd_backend());
            continue;
        }

        regrets.assign(meta.action_count, 0.0);
        strategy.assign(meta.action_count, 0.0);
        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                regrets[action] = infoset_table_.regret_value(
                    meta.id,
                    bucket * static_cast<std::size_t>(meta.action_count) + action);
            }
            compute_strategy_row(regrets.data(), strategy.data(), meta.action_count);
            for (std::size_t action = 0; action < meta.action_count; ++action) {
                infoset_table_.set_current_strategy_value(
                    meta.id,
                    bucket * static_cast<std::size_t>(meta.action_count) + action,
                    strategy[action]);
            }
        }
    }
    profile_.strategy_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - strategy_start).count();
}

void HUNLFlatMCCFR::rebuild_average_policy_cache() {
    const auto cache_start = std::chrono::steady_clock::now();
    for (const auto& meta : infoset_meta_) {
        auto& averaged = average_policy_cache_[meta.id.value];
        fill_average_action_probabilities(
            meta.id,
            averaged.data(),
            meta.action_count,
            worker_scratch_[0].bucket_strategy.data());

        auto& as_probabilities = average_strategy_sampling_cache_[meta.id.value];
        fill_average_strategy_sum_values(
            meta.id,
            worker_scratch_[0].strategy_sum_values.data(),
            meta.action_count);
        double row_strategy_sum = 0.0;
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            const auto value = worker_scratch_[0].strategy_sum_values[action];
            row_strategy_sum += std::max(value, 0.0);
        }
        const auto denominator = config_.as_beta + row_strategy_sum;
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            double probability = 1.0;
            if (denominator > 0.0) {
                probability = (config_.as_beta +
                    config_.as_tau * std::max(worker_scratch_[0].strategy_sum_values[action], 0.0)) /
                    denominator;
            }
            probability = std::max(config_.as_epsilon, probability);
            as_probabilities[action] = std::min(1.0, probability);
        }
    }
    profile_.average_policy_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - cache_start).count();
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
        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand &&
            infoset_table_.precision() == HUNLFlatStoragePrecision::Float64) {
            const auto dense_row = build_dense_traversal_row_view(
                infoset_table_,
                infoset_id,
                meta.bucket_count,
                action_count,
                nullptr,
                nullptr);
            fill_current_strategy_bucket_dense(dense_row, bucket, out);
            return;
        }
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

void HUNLFlatMCCFR::fill_average_action_probabilities(
    InfosetId infoset_id,
    double* out,
    std::size_t action_count,
    double* bucket_scratch) const {
    const auto& meta = infoset_meta(infoset_id);
    if (out == nullptr) {
        return;
    }
    std::fill(out, out + action_count, 0.0);
    if (meta.bucket_count == 0) {
        return;
    }

    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        fill_current_strategy_bucket(infoset_id, bucket, bucket_scratch, meta.action_count);
        for (std::size_t action = 0; action < meta.action_count; ++action) {
            out[action] += bucket_scratch[action];
        }
    }
    for (std::size_t action = 0; action < meta.action_count; ++action) {
        out[action] /= static_cast<double>(meta.bucket_count);
    }
}

void HUNLFlatMCCFR::fill_average_strategy_sum_values(
    InfosetId infoset_id,
    double* out,
    std::size_t action_count) const {
    const auto& meta = infoset_meta(infoset_id);
    if (out == nullptr) {
        return;
    }
    std::fill(out, out + action_count, 0.0);
    if (meta.bucket_count == 0) {
        return;
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
            out[action] += value;
        }
    }
    for (std::size_t action = 0; action < meta.action_count; ++action) {
        out[action] /= static_cast<double>(meta.bucket_count);
    }
}

void HUNLFlatMCCFR::fill_average_strategy_sampling_probabilities(
    InfosetId infoset_id,
    double* out,
    std::size_t action_count,
    double* strategy_sum_scratch) const {
    if (out == nullptr || strategy_sum_scratch == nullptr) {
        return;
    }
    if (infoset_id.value < average_strategy_sampling_cache_.size()) {
        const auto& cached = average_strategy_sampling_cache_[infoset_id.value];
        if (cached.size() == action_count) {
            std::copy(cached.begin(), cached.end(), out);
            return;
        }
    }
    fill_average_strategy_sum_values(infoset_id, strategy_sum_scratch, action_count);
    double row_strategy_sum = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto value = strategy_sum_scratch[action];
        row_strategy_sum += std::max(value, 0.0);
    }

    const auto denominator = config_.as_beta + row_strategy_sum;
    for (std::size_t action = 0; action < action_count; ++action) {
        double probability = 1.0;
        if (denominator > 0.0) {
            probability = (config_.as_beta + config_.as_tau * std::max(strategy_sum_scratch[action], 0.0)) /
                denominator;
        }
        probability = std::max(config_.as_epsilon, probability);
        out[action] = std::min(1.0, probability);
    }
}

std::size_t HUNLFlatMCCFR::sample_chance_child(
    std::uint32_t node_idx,
    const TraversalNodeMeta& meta,
    PcsRng& rng) const {
    const auto total = chance_total_probability_[node_idx];
    if (total <= 0.0) {
        throw std::logic_error("HUNLFlatMCCFR chance node must have positive probability mass");
    }

    const auto* outcomes = graph_.chance_outcomes.data() + meta.chance_begin;
    double draw = rng.next_unit_f64() * total;
    for (std::size_t i = 0; i < meta.chance_count; ++i) {
        const auto& outcome = outcomes[i];
        if (draw < outcome.probability) {
            return i;
        }
        draw -= outcome.probability;
    }

    return meta.chance_count - 1U;
}

void HUNLFlatMCCFR::discount_dense_infoset_row(InfosetId infoset_id, std::uint32_t target_iteration) {
    auto& meta = infoset_table_.meta_mut().at(infoset_id.value);
    if (meta.last_discount_iter >= target_iteration) {
        return;
    }

    for (std::uint32_t tt = meta.last_discount_iter + 1U; tt <= target_iteration; ++tt) {
        const auto t = static_cast<double>(tt);
        const auto ta = std::pow(t, config_.dcfr_alpha);
        const auto tb = std::pow(t, config_.dcfr_beta);
        const auto pos_scale = ta / (ta + 1.0);
        const auto neg_scale = tb / (tb + 1.0);
        const auto strat_scale = std::pow(t / (t + 1.0), config_.dcfr_gamma);
        infoset_table_.discount_values(infoset_id, pos_scale, neg_scale, strat_scale);
    }
    meta.last_discount_iter = target_iteration;
}

void HUNLFlatMCCFR::discount_sparse_infoset_row(InfosetId infoset_id, std::uint32_t target_iteration) {
    auto* meta = sparse_storage_.meta_for_mut(infoset_id);
    if (meta == nullptr || meta->last_discount_iter >= target_iteration) {
        return;
    }

    auto row = sparse_storage_.view_mut(infoset_id);
    for (std::uint32_t tt = meta->last_discount_iter + 1U; tt <= target_iteration; ++tt) {
        const auto t = static_cast<double>(tt);
        const auto ta = std::pow(t, config_.dcfr_alpha);
        const auto tb = std::pow(t, config_.dcfr_beta);
        const auto pos_scale = ta / (ta + 1.0);
        const auto neg_scale = tb / (tb + 1.0);
        const auto strat_scale = std::pow(t / (t + 1.0), config_.dcfr_gamma);
        for (std::size_t offset = 0; offset < row.value_count(); ++offset) {
            const auto regret = static_cast<double>(row.regret[offset]);
            row.regret[offset] = static_cast<float>(regret >= 0.0 ? regret * pos_scale : regret * neg_scale);
            row.strategy_sum[offset] = static_cast<float>(static_cast<double>(row.strategy_sum[offset]) * strat_scale);
        }
    }
    meta->last_discount_iter = target_iteration;
}

void HUNLFlatMCCFR::apply_discount_if_enabled(std::uint32_t target_iteration, WorkerScratch& scratch) {
    if (!config_.use_discounting) {
        return;
    }

    const auto discount_start = std::chrono::steady_clock::now();
    for (const auto infoset_id : scratch.dirty_row_ids) {
        if (config_.use_sparse_storage) {
            discount_sparse_infoset_row(infoset_id, target_iteration);
        } else {
            discount_dense_infoset_row(infoset_id, target_iteration);
        }
    }
    profile_.discount_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - discount_start).count();
}

void HUNLFlatMCCFR::begin_player_batch(std::uint32_t target_iteration, PlayerId traversing_player) {
    (void)target_iteration;
    (void)traversing_player;
    compute_current_strategy_rows();
    rebuild_average_policy_cache();
    ++profile_.strategy_snapshot_rebuilds;
    ++strategy_snapshot_generation_;
    dispatched_strategy_snapshot_generation_ = 0;
    player_batch_snapshot_ready_ = true;
}

bool HUNLFlatMCCFR::run_next_player_subbatch() {
    if (!iteration_in_progress_) {
        iteration_in_progress_ = true;
        in_progress_target_iteration_ = iterations_ + 1U;
        in_progress_player_ = 0;
        in_progress_trajectory_begin_ = 0;
        player_batch_snapshot_ready_ = false;
        last_iteration_counters_ = {};
        if (profile_.workers.size() != worker_count_) {
            profile_.workers.resize(worker_count_);
        }
    }

    if (!player_batch_snapshot_ready_) {
        begin_player_batch(in_progress_target_iteration_, in_progress_player_);
    }

    const auto remaining = config_.traversals_per_iteration -
        static_cast<std::uint32_t>(in_progress_trajectory_begin_);
    const auto trajectory_count = std::min(config_.batch_size, remaining);
    run_player_subbatch(
        in_progress_target_iteration_,
        in_progress_player_,
        in_progress_trajectory_begin_,
        trajectory_count);
    in_progress_trajectory_begin_ += trajectory_count;

    if (in_progress_trajectory_begin_ < config_.traversals_per_iteration) {
        return false;
    }

    const auto player_count = config_.update_both_players ? 2U : 1U;
    if (static_cast<std::uint32_t>(in_progress_player_) + 1U < player_count) {
        in_progress_player_ = static_cast<PlayerId>(static_cast<std::uint32_t>(in_progress_player_) + 1U);
        in_progress_trajectory_begin_ = 0;
        player_batch_snapshot_ready_ = false;
        return false;
    }

    accumulate_last_iteration_into_total();
    ++iterations_;
    refresh_baseline_profile();
    iteration_in_progress_ = false;
    player_batch_snapshot_ready_ = false;
    return true;
}

void HUNLFlatMCCFR::run_player_subbatch(
    std::uint32_t target_iteration,
    PlayerId traversing_player,
    std::uint64_t trajectory_begin,
    std::uint32_t trajectory_count) {
    if (trajectory_count == 0U) {
        return;
    }
    if (!player_batch_snapshot_ready_ ||
        (dispatched_strategy_snapshot_generation_ != 0U &&
         dispatched_strategy_snapshot_generation_ != strategy_snapshot_generation_)) {
        throw std::logic_error("MCCFR subbatch dispatched without a fresh strategy snapshot");
    }
    dispatched_strategy_snapshot_generation_ = strategy_snapshot_generation_;

    const auto batches = HUNLSampledScheduler::partition_deterministic(
        trajectory_count,
        worker_count_);
    const auto traverse_start = std::chrono::steady_clock::now();
    const auto dispatch_start = std::chrono::steady_clock::now();

    if (worker_count_ > 1) {
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            current_target_iteration_ = target_iteration;
            current_traversing_player_ = traversing_player;
            current_trajectory_begin_ = trajectory_begin;
            worker_completed_count_ = 0;
            worker_exception_ = nullptr;
            for (std::size_t worker_index = 0; worker_index < worker_count_; ++worker_index) {
                current_worker_batches_[worker_index] = batches[worker_index];
            }
            ++worker_generation_;
        }
        worker_cv_.notify_all();
    }
    profile_.worker_wakeups += worker_count_;

    execute_worker_batch(0, target_iteration, traversing_player, trajectory_begin, batches[0].trajectories);

    if (worker_count_ > 1) {
        std::unique_lock<std::mutex> lock(worker_mutex_);
        worker_done_cv_.wait(lock, [&]() {
            return worker_completed_count_ == worker_count_ - 1;
        });
        if (worker_exception_ != nullptr) {
            std::rethrow_exception(worker_exception_);
        }
    }
    double subbatch_trajectory_seconds = 0.0;
    for (const auto& scratch : worker_scratch_) {
        subbatch_trajectory_seconds += scratch.last_trajectory_seconds;
    }
    profile_.trajectory_seconds += subbatch_trajectory_seconds;
    profile_.dispatch_seconds +=
        std::max(
            0.0,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - dispatch_start).count() -
                subbatch_trajectory_seconds);

    profile_.traverse_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - traverse_start).count();
    profile_.traversals += trajectory_count;
    for (const auto& scratch : worker_scratch_) {
        profile_.chance_seconds += scratch.audit.chance_seconds;
        profile_.opponent_sampled_seconds += scratch.audit.opponent_sampled_seconds;
        profile_.traversing_player_seconds += scratch.audit.traversing_player_seconds;
        profile_.average_strategy_seconds += scratch.audit.average_strategy_seconds;
        profile_.row_writeback_seconds += scratch.audit.row_writeback_seconds;
        profile_.chance_nodes_visited += scratch.counters.chance_nodes_visited;
        profile_.decision_nodes_visited += scratch.counters.decision_nodes_visited;
        profile_.opponent_sampled_decisions += scratch.counters.opponent_sampled_decisions;
        profile_.opponent_strategy_values_written += scratch.counters.opponent_strategy_values_written;
        profile_.traversing_player_full_expansion_decisions +=
            scratch.counters.traversing_player_full_expansion_decisions;
        profile_.decision_actions_touched += scratch.counters.decision_actions_touched;
        profile_.as_actions_considered += scratch.counters.as_actions_considered;
        profile_.as_actions_sampled += scratch.counters.as_actions_sampled;
        profile_.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
        profile_.as_decision_nodes += scratch.counters.as_decision_nodes;
    }
    profile_.active_infoset_samples +=
        std::accumulate(
            worker_scratch_.begin(),
            worker_scratch_.end(),
            std::uint64_t{0},
            [](std::uint64_t sum, const WorkerScratch& scratch) {
                return sum + static_cast<std::uint64_t>(scratch.dirty_row_ids.size());
            });
    profile_.worker_batch_executions += worker_count_;

    for (std::size_t worker_index = 0; worker_index < worker_count_; ++worker_index) {
        apply_discount_if_enabled(target_iteration, worker_scratch_[worker_index]);
        merge_worker_rows(worker_index);
        merge_worker_baselines(worker_index);
    }
    refresh_baseline_profile();
}

void HUNLFlatMCCFR::initialize_worker_threads() {
    if (worker_count_ <= 1) {
        return;
    }

    try {
        worker_threads_.reserve(worker_count_ - 1);
        for (std::size_t worker_index = 1; worker_index < worker_count_; ++worker_index) {
            worker_threads_.emplace_back(&HUNLFlatMCCFR::worker_loop, this, worker_index);
        }
    } catch (...) {
        shutdown_worker_threads();
        throw;
    }
}

void HUNLFlatMCCFR::shutdown_worker_threads() noexcept {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_shutdown_ = true;
    }
    worker_cv_.notify_all();
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

void HUNLFlatMCCFR::worker_loop(std::size_t worker_index) {
    std::uint64_t observed_generation = 0;
    for (;;) {
        std::uint32_t target_iteration = 0;
        PlayerId traversing_player = 0;
        std::uint64_t trajectory_begin = 0;
        HUNLSampledTrajectoryRange range;

        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait(lock, [&]() {
                return worker_shutdown_ || worker_generation_ != observed_generation;
            });
            if (worker_shutdown_) {
                return;
            }

            observed_generation = worker_generation_;
            target_iteration = current_target_iteration_;
            traversing_player = current_traversing_player_;
            trajectory_begin = current_trajectory_begin_;
            range = current_worker_batches_[worker_index].trajectories;
        }

        try {
            execute_worker_batch(worker_index, target_iteration, traversing_player, trajectory_begin, range);
        } catch (...) {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            if (worker_exception_ == nullptr) {
                worker_exception_ = std::current_exception();
            }
            ++worker_completed_count_;
            worker_done_cv_.notify_one();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            ++worker_completed_count_;
            if (worker_completed_count_ == worker_count_ - 1) {
                worker_done_cv_.notify_one();
            }
        }
    }
}

void HUNLFlatMCCFR::execute_worker_batch(
    std::size_t worker_index,
    std::uint32_t target_iteration,
    PlayerId traversing_player,
    std::uint64_t trajectory_begin,
    HUNLSampledTrajectoryRange range) {
    if (config_.test_throw_worker_index == static_cast<std::int32_t>(worker_index)) {
        throw std::runtime_error("injected MCCFR worker batch failure");
    }
    auto& scratch = worker_scratch_[worker_index];
    scratch.clear_keep_capacity();
    TraversalContext::TraverseImpl traverse_impl = &HUNLFlatMCCFR::traverse_exact;
    switch (config_.mode) {
        case HUNLFlatSamplingMode::Exact:
            traverse_impl = &HUNLFlatMCCFR::traverse_exact;
            break;
        case HUNLFlatSamplingMode::PublicChance:
            traverse_impl = variance_reduction_enabled()
                ? &HUNLFlatMCCFR::traverse_public_chance_vr
                : &HUNLFlatMCCFR::traverse_public_chance;
            break;
        case HUNLFlatSamplingMode::External:
            traverse_impl = variance_reduction_enabled()
                ? &HUNLFlatMCCFR::traverse_external_vr
                : &HUNLFlatMCCFR::traverse_external;
            break;
        case HUNLFlatSamplingMode::AverageStrategy:
            traverse_impl = variance_reduction_enabled()
                ? &HUNLFlatMCCFR::traverse_average_strategy_vr
                : &HUNLFlatMCCFR::traverse_average_strategy;
            break;
    }
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint64_t trajectory_id = range.begin; trajectory_id < range.end; ++trajectory_id) {
        const auto seed = PcsRng::mix_seed(
            config_.seed,
            target_iteration,
            static_cast<std::uint32_t>(traversing_player),
            trajectory_begin + trajectory_id);
        PcsRng rng(seed);
        TraversalContext context;
        context.traversing_player = traversing_player;
        context.rng = &rng;
        context.counters = &scratch.counters;
        context.scratch = &scratch;
        context.traverse_impl = traverse_impl;
        (void)traverse(graph_.root, context);
    }
    auto& worker_profile = profile_.workers[worker_index];
    const auto trajectory_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count();
    worker_profile.trajectory_seconds += trajectory_seconds;
    worker_profile.traverse_seconds += trajectory_seconds;
    worker_profile.chance_seconds += scratch.audit.chance_seconds;
    worker_profile.opponent_sampled_seconds += scratch.audit.opponent_sampled_seconds;
    worker_profile.traversing_player_seconds += scratch.audit.traversing_player_seconds;
    worker_profile.average_strategy_seconds += scratch.audit.average_strategy_seconds;
    worker_profile.row_writeback_seconds += scratch.audit.row_writeback_seconds;
    worker_profile.traversals += range.size();
    worker_profile.nodes_visited += scratch.counters.nodes_visited;
    worker_profile.chance_nodes_visited += scratch.counters.chance_nodes_visited;
    worker_profile.decision_nodes_visited += scratch.counters.decision_nodes_visited;
    worker_profile.sampled_opponent_actions += scratch.counters.sampled_opponent_actions;
    worker_profile.opponent_sampled_decisions += scratch.counters.opponent_sampled_decisions;
    worker_profile.opponent_strategy_values_written += scratch.counters.opponent_strategy_values_written;
    worker_profile.traversing_player_action_expansions += scratch.counters.traversing_player_action_expansions;
    worker_profile.traversing_player_full_expansion_decisions +=
        scratch.counters.traversing_player_full_expansion_decisions;
    worker_profile.decision_actions_touched += scratch.counters.decision_actions_touched;
    worker_profile.as_actions_considered += scratch.counters.as_actions_considered;
    worker_profile.as_actions_sampled += scratch.counters.as_actions_sampled;
    worker_profile.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
    worker_profile.as_decision_nodes += scratch.counters.as_decision_nodes;
    worker_profile.active_infosets += scratch.dirty_row_ids.size();
    ++worker_profile.batch_executions;
    scratch.last_trajectory_seconds = trajectory_seconds;
}

void HUNLFlatMCCFR::merge_worker_rows(std::size_t worker_index) {
    auto merge_start = std::chrono::steady_clock::now();
    auto& scratch = worker_scratch_[worker_index];
    if (scratch.dirty_row_ids.size() > 1U) {
        std::sort(scratch.dirty_row_ids.begin(), scratch.dirty_row_ids.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.value < rhs.value;
        });
    }

    for (const auto infoset_id : scratch.dirty_row_ids) {
        const auto& row = scratch.delta_rows[infoset_id.value];
        if (infoset_id.value < touched_infosets_.size() && touched_infosets_[infoset_id.value] == 0U) {
            touched_infosets_[infoset_id.value] = 1U;
            ++unique_infosets_touched_;
        }
        const auto& meta = infoset_meta(infoset_id);
        if (config_.use_sparse_storage) {
            auto sparse_row = ensure_sparse_row(infoset_id);
            for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
                sparse_row.regret[offset] =
                    static_cast<float>(static_cast<double>(sparse_row.regret[offset]) + row.regret_delta[offset]);
                sparse_row.strategy_sum[offset] = static_cast<float>(
                    static_cast<double>(sparse_row.strategy_sum[offset]) + row.strategy_delta[offset]);
            }
            continue;
        }

        if (infoset_table_.precision() == HUNLFlatStoragePrecision::Float64) {
            auto* regret = infoset_table_.regret_mut(infoset_id);
            auto* strategy_sum = infoset_table_.strategy_sum_mut(infoset_id);
            for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
                regret[offset] += row.regret_delta[offset];
                strategy_sum[offset] += row.strategy_delta[offset];
            }
            continue;
        }

        for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
            infoset_table_.set_regret_value(
                infoset_id,
                offset,
                infoset_table_.regret_value(infoset_id, offset) + row.regret_delta[offset]);
            infoset_table_.set_strategy_sum_value(
                infoset_id,
                offset,
                infoset_table_.strategy_sum_value(infoset_id, offset) + row.strategy_delta[offset]);
        }
    }

    const auto merge_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - merge_start).count();
    profile_.merge_seconds += merge_seconds;
    profile_.workers[worker_index].merge_seconds += merge_seconds;
    last_iteration_counters_.nodes_visited += scratch.counters.nodes_visited;
    last_iteration_counters_.chance_nodes_visited += scratch.counters.chance_nodes_visited;
    last_iteration_counters_.decision_nodes_visited += scratch.counters.decision_nodes_visited;
    last_iteration_counters_.sampled_opponent_actions += scratch.counters.sampled_opponent_actions;
    last_iteration_counters_.opponent_sampled_decisions += scratch.counters.opponent_sampled_decisions;
    last_iteration_counters_.opponent_strategy_values_written += scratch.counters.opponent_strategy_values_written;
    last_iteration_counters_.traversing_player_action_expansions +=
        scratch.counters.traversing_player_action_expansions;
    last_iteration_counters_.traversing_player_full_expansion_decisions +=
        scratch.counters.traversing_player_full_expansion_decisions;
    last_iteration_counters_.decision_actions_touched += scratch.counters.decision_actions_touched;
    last_iteration_counters_.as_actions_considered += scratch.counters.as_actions_considered;
    last_iteration_counters_.as_actions_sampled += scratch.counters.as_actions_sampled;
    last_iteration_counters_.as_forced_at_least_one_count += scratch.counters.as_forced_at_least_one_count;
    last_iteration_counters_.as_decision_nodes += scratch.counters.as_decision_nodes;
    last_iteration_counters_.variance_samples += scratch.counters.variance_samples;
    last_iteration_counters_.raw_estimate_sum += scratch.counters.raw_estimate_sum;
    last_iteration_counters_.raw_estimate_sq_sum += scratch.counters.raw_estimate_sq_sum;
    last_iteration_counters_.corrected_estimate_sum += scratch.counters.corrected_estimate_sum;
    last_iteration_counters_.corrected_estimate_sq_sum += scratch.counters.corrected_estimate_sq_sum;
}

void HUNLFlatMCCFR::accumulate_last_iteration_into_total() noexcept {
    total_counters_.nodes_visited += last_iteration_counters_.nodes_visited;
    total_counters_.chance_nodes_visited += last_iteration_counters_.chance_nodes_visited;
    total_counters_.decision_nodes_visited += last_iteration_counters_.decision_nodes_visited;
    total_counters_.sampled_opponent_actions += last_iteration_counters_.sampled_opponent_actions;
    total_counters_.opponent_sampled_decisions += last_iteration_counters_.opponent_sampled_decisions;
    total_counters_.opponent_strategy_values_written += last_iteration_counters_.opponent_strategy_values_written;
    total_counters_.traversing_player_action_expansions += last_iteration_counters_.traversing_player_action_expansions;
    total_counters_.traversing_player_full_expansion_decisions +=
        last_iteration_counters_.traversing_player_full_expansion_decisions;
    total_counters_.decision_actions_touched += last_iteration_counters_.decision_actions_touched;
    total_counters_.as_actions_considered += last_iteration_counters_.as_actions_considered;
    total_counters_.as_actions_sampled += last_iteration_counters_.as_actions_sampled;
    total_counters_.as_forced_at_least_one_count += last_iteration_counters_.as_forced_at_least_one_count;
    total_counters_.as_decision_nodes += last_iteration_counters_.as_decision_nodes;
    total_counters_.variance_samples += last_iteration_counters_.variance_samples;
    total_counters_.raw_estimate_sum += last_iteration_counters_.raw_estimate_sum;
    total_counters_.raw_estimate_sq_sum += last_iteration_counters_.raw_estimate_sq_sum;
    total_counters_.corrected_estimate_sum += last_iteration_counters_.corrected_estimate_sum;
    total_counters_.corrected_estimate_sq_sum += last_iteration_counters_.corrected_estimate_sq_sum;
}

void HUNLFlatMCCFR::merge_worker_baselines(std::size_t worker_index) {
    if (!variance_reduction_enabled()) {
        return;
    }

    auto& scratch = worker_scratch_[worker_index];
    std::sort(scratch.infoset_baseline_rows.begin(), scratch.infoset_baseline_rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    std::sort(scratch.node_baseline_rows.begin(), scratch.node_baseline_rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });

    for (const auto& row : scratch.infoset_baseline_rows) {
        auto& values = infoset_action_baselines_[row.id];
        auto& counts = infoset_action_baseline_counts_[row.id];
        if (values.empty()) {
            values.assign(row.value_sum.size(), 0.0f);
            counts.assign(row.value_sum.size(), 0U);
        }
        for (std::size_t action = 0; action < row.value_sum.size(); ++action) {
            const auto sample_count = row.sample_count[action];
            if (sample_count == 0U) {
                continue;
            }
            const auto total_count = counts[action] + sample_count;
            const auto total_sum =
                static_cast<double>(values[action]) * static_cast<double>(counts[action]) + row.value_sum[action];
            values[action] = static_cast<float>(total_sum / static_cast<double>(total_count));
            counts[action] = total_count;
        }
    }

    for (const auto& row : scratch.node_baseline_rows) {
        auto& values = node_action_baselines_[row.id];
        auto& counts = node_action_baseline_counts_[row.id];
        if (values.empty()) {
            values.assign(row.value_sum.size(), 0.0f);
            counts.assign(row.value_sum.size(), 0U);
        }
        for (std::size_t action = 0; action < row.value_sum.size(); ++action) {
            const auto sample_count = row.sample_count[action];
            if (sample_count == 0U) {
                continue;
            }
            const auto total_count = counts[action] + sample_count;
            const auto total_sum =
                static_cast<double>(values[action]) * static_cast<double>(counts[action]) + row.value_sum[action];
            values[action] = static_cast<float>(total_sum / static_cast<double>(total_count));
            counts[action] = total_count;
        }
    }
}

void HUNLFlatMCCFR::refresh_baseline_profile() noexcept {
    std::uint64_t infoset_rows = 0;
    std::uint64_t node_rows = 0;
    std::uint64_t bytes = 0;

    for (std::size_t i = 0; i < infoset_action_baselines_.size(); ++i) {
        if (infoset_action_baselines_[i].empty()) {
            continue;
        }
        ++infoset_rows;
        bytes += static_cast<std::uint64_t>(infoset_action_baselines_[i].capacity()) * sizeof(float);
        bytes += static_cast<std::uint64_t>(infoset_action_baseline_counts_[i].capacity()) * sizeof(std::uint32_t);
    }
    for (std::size_t i = 0; i < node_action_baselines_.size(); ++i) {
        if (node_action_baselines_[i].empty()) {
            continue;
        }
        ++node_rows;
        bytes += static_cast<std::uint64_t>(node_action_baselines_[i].capacity()) * sizeof(float);
        bytes += static_cast<std::uint64_t>(node_action_baseline_counts_[i].capacity()) * sizeof(std::uint32_t);
    }

    profile_.baseline_infoset_rows = infoset_rows;
    profile_.baseline_node_rows = node_rows;
    profile_.baseline_bytes = bytes;
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

HUNLSampledRootStrategy HUNLFlatMCCFR::export_root_average_strategy(std::size_t bucket_index) const {
    if (graph_.node_meta.empty() || graph_.root >= graph_.node_meta.size()) {
        return {};
    }

    const auto& root_meta = graph_.node_meta[graph_.root];
    std::vector<ActionId> root_actions;
    std::vector<int> root_targets;
    root_actions.reserve(root_meta.action_count);
    root_targets.reserve(root_meta.action_count);
    for (std::size_t action = 0; action < root_meta.action_count; ++action) {
        const auto action_index = root_meta.action_begin + action;
        root_actions.push_back(action_index < graph_.actions.size()
            ? graph_.actions[action_index]
            : static_cast<ActionId>(action));
        const auto child = graph_.children[root_meta.child_begin + action];
        root_targets.push_back(root_meta.player >= 0
            ? graph_.node_meta[child].contributions[static_cast<std::size_t>(root_meta.player)]
            : 0);
    }
    const auto decorate = [&root_actions, &root_targets](HUNLSampledRootStrategy strategy) {
        HUNLSampledStrategyExporter::attach_action_descriptors(strategy, root_actions, root_targets);
        return strategy;
    };
    if (!root_meta.has_infoset) {
        return decorate(HUNLSampledStrategyExporter::export_uniform(root_meta.action_count));
    }

    const auto infoset_id = root_meta.infoset_id;
    if (config_.use_sparse_storage) {
        const auto row = sparse_row_or_empty(infoset_id);
        return decorate(row.empty()
            ? HUNLSampledStrategyExporter::export_uniform(root_meta.action_count)
            : HUNLSampledStrategyExporter::export_average_strategy(row, bucket_index));
    }

    const auto& meta = infoset_meta(infoset_id);
    if (bucket_index >= meta.bucket_count) {
        return decorate(HUNLSampledStrategyExporter::export_uniform(meta.action_count));
    }
    HUNLSampledRootStrategy strategy;
    strategy.actions.reserve(meta.action_count);
    double total = 0.0;
    std::vector<double> values(meta.action_count, 0.0);
    for (std::size_t action = 0; action < meta.action_count; ++action) {
        const auto value = infoset_table_.strategy_sum_value(infoset_id, row_value_index(meta, bucket_index, action));
        values[action] = value;
        total += value;
    }
    const auto uniform = meta.action_count == 0 ? 0.0 : 1.0 / static_cast<double>(meta.action_count);
    for (std::size_t action = 0; action < meta.action_count; ++action) {
        strategy.actions.push_back({
            static_cast<std::uint32_t>(action),
            ACTION_FOLD,
            0,
            0,
            total > 0.0 ? values[action] / total : uniform,
        });
    }
    return decorate(std::move(strategy));
}

HUNLFlatMCCFR::RootStrategySnapshot HUNLFlatMCCFR::export_root_snapshot(
    std::size_t bucket_index,
    double probability_delta,
    std::uint64_t batches_completed,
    bool timed_out) const {
    const auto export_start = std::chrono::steady_clock::now();
    RootStrategySnapshot snapshot;
    snapshot.infoset_id = root_infoset_id();
    snapshot.infoset_key = std::string(graph_.infoset_key(snapshot.infoset_id));
    snapshot.strategy = export_root_average_strategy(bucket_index);
    snapshot.action_entropy = action_entropy(snapshot.strategy);
    snapshot.action_probability_delta = probability_delta;
    snapshot.batches_completed = batches_completed;
    snapshot.sampled_nodes_visited = total_counters_.nodes_visited;
    snapshot.unique_infosets_touched = unique_infosets_touched_;
    snapshot.memory_used_bytes = memory_used_bytes();
    snapshot.timed_out = timed_out;
    const_cast<Profile&>(profile_).snapshot_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - export_start).count();
    return snapshot;
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

double HUNLFlatMCCFR::raw_estimator_variance() const noexcept {
    return estimate_variance(
        total_counters_.variance_samples,
        total_counters_.raw_estimate_sum,
        total_counters_.raw_estimate_sq_sum);
}

double HUNLFlatMCCFR::corrected_estimator_variance() const noexcept {
    return estimate_variance(
        total_counters_.variance_samples,
        total_counters_.corrected_estimate_sum,
        total_counters_.corrected_estimate_sq_sum);
}

HUNLFlatMCCFR::BaselineStats HUNLFlatMCCFR::baseline_stats() const noexcept {
    return {
        profile_.baseline_infoset_rows,
        profile_.baseline_node_rows,
        profile_.baseline_bytes,
    };
}

std::uint64_t HUNLFlatMCCFR::memory_used_bytes() const noexcept {
    return memory_usage().total_bytes();
}

HUNLFlatMCCFR::MemoryUsage HUNLFlatMCCFR::memory_usage() const noexcept {
    MemoryUsage usage;
    usage.graph_bytes = graph_memory_bytes_;
    usage.infoset_metadata_bytes = vector_storage_bytes(infoset_meta_) + vector_storage_bytes(sparse_infoset_shapes_);
    if (config_.use_sparse_storage) {
        usage.central_storage_bytes += sparse_storage_.memory_estimate().total_bytes();
    }
    if (!config_.use_sparse_storage || config_.keep_dense_validation_backend) {
        usage.central_storage_bytes += static_cast<std::uint64_t>(infoset_table_.meta().capacity()) * sizeof(HUNLFlatInfosetTableMeta);
        usage.central_storage_bytes += infoset_table_.regret_storage_bytes();
        usage.central_storage_bytes += infoset_table_.strategy_sum_storage_bytes();
        usage.central_storage_bytes += infoset_table_.current_strategy_storage_bytes();
    }
    for (const auto& row : average_policy_cache_) {
        usage.policy_cache_bytes += vector_storage_bytes(row);
    }
    for (const auto& row : average_strategy_sampling_cache_) {
        usage.policy_cache_bytes += vector_storage_bytes(row);
    }
    usage.traversal_metadata_bytes += vector_storage_bytes(traversal_node_meta_);
    usage.traversal_metadata_bytes += vector_storage_bytes(chance_total_probability_);
    usage.traversal_metadata_bytes += vector_storage_bytes(touched_infosets_);
    usage.traversal_metadata_bytes += vector_storage_bytes(current_worker_batches_);
    for (const auto& scratch : worker_scratch_) {
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.action_values);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.average_strategy);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.sampled_action_indices);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.bucket_strategy);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.node_values);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.inclusion_probabilities);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.strategy_sum_values);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.sampled_actions);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.delta_rows);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.dirty_row_ids);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.row_active);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.external_frames);
        usage.worker_scratch_bytes += vector_storage_bytes(scratch.external_frame_action_values);
        for (const auto& row : scratch.delta_rows) {
            usage.worker_scratch_bytes += vector_storage_bytes(row.regret_delta);
            usage.worker_scratch_bytes += vector_storage_bytes(row.strategy_delta);
        }
    }
    usage.baseline_bytes = profile_.baseline_bytes;
    return usage;
}

InfosetId HUNLFlatMCCFR::root_infoset_id() const noexcept {
    if (graph_.node_meta.empty() || graph_.root >= graph_.node_meta.size()) {
        return {};
    }
    const auto& root_meta = graph_.node_meta[graph_.root];
    return root_meta.has_infoset ? root_meta.infoset_id : InfosetId{};
}

double HUNLFlatMCCFR::action_entropy(const HUNLSampledRootStrategy& strategy) noexcept {
    double entropy = 0.0;
    for (const auto& action : strategy.actions) {
        if (action.probability > 0.0) {
            entropy -= action.probability * std::log(action.probability);
        }
    }
    return entropy;
}

double HUNLFlatMCCFR::action_probability_delta(
    const HUNLSampledRootStrategy& lhs,
    const HUNLSampledRootStrategy& rhs) noexcept {
    const auto count = std::min(lhs.actions.size(), rhs.actions.size());
    double delta = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        delta += std::abs(lhs.actions[i].probability - rhs.actions[i].probability);
    }
    return delta;
}

}  // namespace core
