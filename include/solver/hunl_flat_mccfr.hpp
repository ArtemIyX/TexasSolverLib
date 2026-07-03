#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_simd.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "util/pcs.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace core {

class HUNLFlatMCCFR {
public:
    struct RootStrategySnapshot {
        InfosetId infoset_id{};
        std::string infoset_key;
        HUNLSampledRootStrategy strategy;
        double action_entropy = 0.0;
        double action_probability_delta = 0.0;
        std::uint64_t batches_completed = 0;
        std::uint64_t sampled_nodes_visited = 0;
        std::uint64_t unique_infosets_touched = 0;
        std::uint64_t memory_used_bytes = 0;
        bool timed_out = false;
    };

    struct DeadlineSolveResult {
        RootStrategySnapshot latest_snapshot;
        std::vector<RootStrategySnapshot> snapshots;
        std::uint32_t iterations_completed = 0;
        std::uint64_t batches_completed = 0;
        double export_seconds = 0.0;
        bool timed_out = false;
    };

    struct BaselineStats {
        std::uint64_t infoset_rows = 0;
        std::uint64_t node_rows = 0;
        std::uint64_t bytes = 0;
    };

    struct WorkerProfile {
        double traverse_seconds = 0.0;
        double merge_seconds = 0.0;
        std::uint64_t traversals = 0;
        std::uint64_t nodes_visited = 0;
        std::uint64_t sampled_opponent_actions = 0;
        std::uint64_t traversing_player_action_expansions = 0;
        std::uint64_t as_actions_considered = 0;
        std::uint64_t as_actions_sampled = 0;
        std::uint64_t as_forced_at_least_one_count = 0;
        std::uint64_t active_infosets = 0;
    };

    struct Profile {
        double snapshot_seconds = 0.0;
        double discount_seconds = 0.0;
        double strategy_seconds = 0.0;
        double traverse_seconds = 0.0;
        double merge_seconds = 0.0;
        double sampled_kernel_scalar_seconds = 0.0;
        double sampled_kernel_simd_seconds = 0.0;
        std::uint64_t traversals = 0;
        std::uint64_t baseline_infoset_rows = 0;
        std::uint64_t baseline_node_rows = 0;
        std::uint64_t baseline_bytes = 0;
        std::uint64_t sampled_kernel_scalar_calls = 0;
        std::uint64_t sampled_kernel_simd_calls = 0;
        std::uint64_t as_actions_considered = 0;
        std::uint64_t as_actions_sampled = 0;
        std::uint64_t as_forced_at_least_one_count = 0;
        HUNLSampledSimdBackend sampled_simd_backend = HUNLSampledSimdBackend::Scalar;
        std::vector<WorkerProfile> workers;
    };

    struct Counters {
        std::uint64_t nodes_visited = 0;
        std::uint64_t sampled_opponent_actions = 0;
        std::uint64_t traversing_player_action_expansions = 0;
        std::uint64_t as_actions_considered = 0;
        std::uint64_t as_actions_sampled = 0;
        std::uint64_t as_forced_at_least_one_count = 0;
        std::uint64_t variance_samples = 0;
        double raw_estimate_sum = 0.0;
        double raw_estimate_sq_sum = 0.0;
        double corrected_estimate_sum = 0.0;
        double corrected_estimate_sq_sum = 0.0;
    };

    explicit HUNLFlatMCCFR(
        HUNLFlatSolveGraph graph,
        std::array<std::size_t, 2> bucket_count_per_player,
        HUNLFlatMCCFRConfig config = {},
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        std::size_t workers = 1,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

    void run_iteration();
    void run_iterations(std::uint32_t iterations);
    [[nodiscard]] DeadlineSolveResult run_until(
        std::chrono::steady_clock::time_point deadline,
        std::size_t snapshot_stride_batches = 1);
    [[nodiscard]] DeadlineSolveResult solve_for(
        std::chrono::milliseconds budget,
        std::size_t snapshot_stride_batches = 1);

    [[nodiscard]] const HUNLFlatSolveGraph& graph() const noexcept;
    [[nodiscard]] const HUNLFlatInfosetTable& infoset_table() const noexcept;
    [[nodiscard]] HUNLFlatInfosetTable& infoset_table_mut() noexcept;
    [[nodiscard]] std::uint32_t iterations() const noexcept;
    [[nodiscard]] const HUNLFlatMCCFRConfig& config() const noexcept;

    [[nodiscard]] std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;
    [[nodiscard]] HUNLFlatAverageStrategyTable export_average_strategy_table() const;
    [[nodiscard]] HUNLSampledRootStrategy export_root_average_strategy(std::size_t bucket_index = 0) const;
    [[nodiscard]] RootStrategySnapshot export_root_snapshot(
        std::size_t bucket_index = 0,
        double probability_delta = 0.0,
        std::uint64_t batches_completed = 0,
        bool timed_out = false) const;
    [[nodiscard]] const Counters& last_iteration_counters() const noexcept;
    [[nodiscard]] const Counters& total_counters() const noexcept;
    [[nodiscard]] const Profile& profile() const noexcept;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] bool using_sparse_storage() const noexcept;
    [[nodiscard]] const HUNLSampledStorage& sparse_storage() const noexcept;
    [[nodiscard]] double average_strategy_sampling_ratio() const noexcept;
    [[nodiscard]] double raw_estimator_variance() const noexcept;
    [[nodiscard]] double corrected_estimator_variance() const noexcept;
    [[nodiscard]] BaselineStats baseline_stats() const noexcept;

private:
    struct WorkerDeltaRow {
        InfosetId id{};
        std::vector<double> regret_delta;
        std::vector<double> strategy_delta;
    };

    struct WorkerBaselineRow {
        std::uint32_t id = 0;
        std::vector<double> value_sum;
        std::vector<std::uint32_t> sample_count;
    };

    struct WorkerScratch {
        std::vector<double> action_values;
        std::vector<double> average_strategy;
        std::vector<WorkerDeltaRow> rows;
        std::unordered_map<InfosetId, std::size_t> row_lookup;
        std::vector<WorkerBaselineRow> infoset_baseline_rows;
        std::unordered_map<InfosetId, std::size_t> infoset_baseline_lookup;
        std::vector<WorkerBaselineRow> node_baseline_rows;
        std::unordered_map<std::uint32_t, std::size_t> node_baseline_lookup;
        Counters counters;

        void clear_keep_capacity() noexcept;
        WorkerDeltaRow& ensure_row(InfosetId id, std::size_t value_count);
        WorkerBaselineRow& ensure_infoset_baseline_row(InfosetId id, std::size_t action_count);
        WorkerBaselineRow& ensure_node_baseline_row(std::uint32_t node_idx, std::size_t action_count);
    };

    struct TraversalContext {
        PlayerId traversing_player = 0;
        double p0 = 1.0;
        double p1 = 1.0;
        PcsRng* rng = nullptr;
        Counters* counters = nullptr;
        WorkerScratch* scratch = nullptr;
    };

    [[nodiscard]] double traverse(std::uint32_t node_idx, TraversalContext& context);
    void compute_current_strategy_rows();
    void fill_current_strategy_bucket(
        InfosetId infoset_id,
        std::size_t bucket,
        double* out,
        std::size_t action_count) const;
    [[nodiscard]] std::vector<double> average_action_probabilities(InfosetId infoset_id) const;
    [[nodiscard]] std::vector<double> average_strategy_sum_values(InfosetId infoset_id) const;
    [[nodiscard]] std::vector<double> average_strategy_sampling_probabilities(InfosetId infoset_id) const;
    [[nodiscard]] std::size_t sample_chance_child(const HUNLFlatNodeMeta& meta, PcsRng& rng) const;
    void apply_discount_if_enabled(std::uint32_t target_iteration, WorkerScratch& scratch);
    void discount_dense_infoset_row(InfosetId infoset_id, std::uint32_t target_iteration);
    void discount_sparse_infoset_row(InfosetId infoset_id, std::uint32_t target_iteration);
    void run_player_batch(std::uint32_t target_iteration, PlayerId traversing_player);
    void run_player_subbatch(
        std::uint32_t target_iteration,
        PlayerId traversing_player,
        std::uint64_t trajectory_begin,
        std::uint32_t trajectory_count);
    void merge_worker_rows(std::size_t worker_index);
    void initialize_sparse_infoset_shapes(const std::array<std::size_t, 2>& bucket_count_per_player);
    [[nodiscard]] const HUNLFlatInfosetTableMeta& infoset_meta(InfosetId infoset_id) const noexcept;
    [[nodiscard]] std::size_t row_value_index(
        const HUNLFlatInfosetTableMeta& meta,
        std::size_t bucket,
        std::size_t action) const noexcept;
    [[nodiscard]] HUNLSampledRowView ensure_sparse_row(InfosetId infoset_id);
    [[nodiscard]] HUNLSampledConstRowView sparse_row_or_empty(InfosetId infoset_id) const noexcept;
    [[nodiscard]] bool variance_reduction_enabled() const noexcept;
    [[nodiscard]] double infoset_action_baseline(InfosetId infoset_id, std::size_t action) const noexcept;
    [[nodiscard]] double node_action_baseline(std::uint32_t node_idx, std::size_t action) const noexcept;
    void observe_infoset_action_baseline(
        WorkerScratch& scratch,
        InfosetId infoset_id,
        std::size_t action_count,
        std::size_t action,
        double sample);
    void observe_node_action_baseline(
        WorkerScratch& scratch,
        std::uint32_t node_idx,
        std::size_t action_count,
        std::size_t action,
        double sample);
    void merge_worker_baselines(std::size_t worker_index);
    void refresh_baseline_profile() noexcept;
    static void record_variance_sample(Counters& counters, double raw_estimate, double corrected_estimate) noexcept;
    [[nodiscard]] static double estimate_variance(
        std::uint64_t sample_count,
        double value_sum,
        double value_sq_sum) noexcept;
    void accumulate_last_iteration_into_total() noexcept;
    void add_sampled_kernel_profile(double seconds, HUNLSampledSimdBackend backend) noexcept;
    [[nodiscard]] std::uint64_t memory_used_bytes() const noexcept;
    [[nodiscard]] InfosetId root_infoset_id() const noexcept;
    [[nodiscard]] static double action_entropy(const HUNLSampledRootStrategy& strategy) noexcept;
    [[nodiscard]] static double action_probability_delta(
        const HUNLSampledRootStrategy& lhs,
        const HUNLSampledRootStrategy& rhs) noexcept;

    HUNLFlatSolveGraph graph_;
    HUNLFlatInfosetTable infoset_table_;
    HUNLSampledStorage sparse_storage_;
    HUNLFlatMCCFRConfig config_;
    std::vector<HUNLFlatInfosetTableMeta> infoset_meta_;
    std::vector<HUNLSampledInfosetShape> sparse_infoset_shapes_;
    std::size_t worker_count_ = 1;
    std::uint32_t iterations_ = 0;
    Counters last_iteration_counters_;
    Counters total_counters_;
    Profile profile_;
    std::vector<WorkerScratch> worker_scratch_;
    std::vector<std::uint8_t> touched_infosets_;
    std::uint64_t unique_infosets_touched_ = 0;
    std::uint64_t graph_memory_bytes_ = 0;
    std::vector<std::vector<float>> infoset_action_baselines_;
    std::vector<std::vector<std::uint32_t>> infoset_action_baseline_counts_;
    std::vector<std::vector<float>> node_action_baselines_;
    std::vector<std::vector<std::uint32_t>> node_action_baseline_counts_;
};

}  // namespace core
