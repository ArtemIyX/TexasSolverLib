#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "util/pcs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

namespace core {

class HUNLFlatMCCFR {
public:
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
        double strategy_seconds = 0.0;
        double traverse_seconds = 0.0;
        double merge_seconds = 0.0;
        std::uint64_t traversals = 0;
        std::uint64_t as_actions_considered = 0;
        std::uint64_t as_actions_sampled = 0;
        std::uint64_t as_forced_at_least_one_count = 0;
        std::vector<WorkerProfile> workers;
    };

    struct Counters {
        std::uint64_t nodes_visited = 0;
        std::uint64_t sampled_opponent_actions = 0;
        std::uint64_t traversing_player_action_expansions = 0;
        std::uint64_t as_actions_considered = 0;
        std::uint64_t as_actions_sampled = 0;
        std::uint64_t as_forced_at_least_one_count = 0;
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

    [[nodiscard]] const HUNLFlatSolveGraph& graph() const noexcept;
    [[nodiscard]] const HUNLFlatInfosetTable& infoset_table() const noexcept;
    [[nodiscard]] HUNLFlatInfosetTable& infoset_table_mut() noexcept;
    [[nodiscard]] std::uint32_t iterations() const noexcept;
    [[nodiscard]] const HUNLFlatMCCFRConfig& config() const noexcept;

    [[nodiscard]] std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;
    [[nodiscard]] HUNLFlatAverageStrategyTable export_average_strategy_table() const;
    [[nodiscard]] const Counters& last_iteration_counters() const noexcept;
    [[nodiscard]] const Counters& total_counters() const noexcept;
    [[nodiscard]] const Profile& profile() const noexcept;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] bool using_sparse_storage() const noexcept;
    [[nodiscard]] const HUNLSampledStorage& sparse_storage() const noexcept;
    [[nodiscard]] double average_strategy_sampling_ratio() const noexcept;

private:
    struct WorkerDeltaRow {
        InfosetId id{};
        std::vector<double> regret_delta;
        std::vector<double> strategy_delta;
    };

    struct WorkerScratch {
        std::vector<double> action_values;
        std::vector<double> average_strategy;
        std::vector<WorkerDeltaRow> rows;
        std::unordered_map<InfosetId, std::size_t> row_lookup;
        Counters counters;

        void clear_keep_capacity() noexcept;
        WorkerDeltaRow& ensure_row(InfosetId id, std::size_t value_count);
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
    [[nodiscard]] std::uint32_t sample_chance_child(const HUNLFlatNodeMeta& meta, PcsRng& rng) const;
    void run_player_batch(std::uint32_t target_iteration, PlayerId traversing_player);
    void merge_worker_rows(std::size_t worker_index);
    void initialize_sparse_infoset_shapes(const std::array<std::size_t, 2>& bucket_count_per_player);
    [[nodiscard]] const HUNLFlatInfosetTableMeta& infoset_meta(InfosetId infoset_id) const noexcept;
    [[nodiscard]] std::size_t row_value_index(
        const HUNLFlatInfosetTableMeta& meta,
        std::size_t bucket,
        std::size_t action) const noexcept;
    [[nodiscard]] HUNLSampledRowView ensure_sparse_row(InfosetId infoset_id);
    [[nodiscard]] HUNLSampledConstRowView sparse_row_or_empty(InfosetId infoset_id) const noexcept;

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
};

}  // namespace core
