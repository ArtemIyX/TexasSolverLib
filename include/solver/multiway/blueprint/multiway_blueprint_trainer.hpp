#pragma once

#include "solver/multiway/blueprint/multiway_export.hpp"
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "games/multiway_rules.hpp"

#include <cstdint>
#include <array>
#include <chrono>
#include <memory>
#include <vector>

namespace texas::solver::multiway {

enum class MultiwayTrainingCapacityStage : std::uint8_t {
    None = 0U,
    PublicStates = 1U,
    SparseRows = 2U,
    SparseValues = 3U,
    WorkerDeltas = 4U,
};

[[nodiscard]] MultiwayRootSnapshot make_multiway_initial_blueprint_root(
    const MultiwayGameRules& rules,
    MultiwayPrivateConfig private_ranges,
    const MultiwayActionAbstraction& action_abstraction,
    std::uint64_t action_abstraction_version,
    std::uint64_t leaf_model_version,
    PlayerId first_player = 0);

struct MultiwayBlueprintIterationSchedule {
    bool linear_iteration_weighting = true;
    bool discount_regrets = false;
    double regret_discount_factor = 1.0;
    std::uint64_t discount_interval_batches = 1;
    bool prune_negative_regrets = false;
    std::uint64_t pruning_warmup_batches = 0;
    std::uint64_t pruning_interval_batches = 1;
    // Negative-regret pruning keeps a bounded floor and periodically explores
    // every action to recover actions that were previously skipped.
    double pruning_threshold = 0.0;
    double regret_floor = 0.0;
    std::uint64_t recovery_interval_batches = 20;
    double pruning_exploration_probability = 0.05;
    double pruning_action_probability_threshold = 0.0;
    // Zero disables late-window export. A non-zero value is the first
    // included batch in its separately accumulated weighted window.
    std::uint64_t late_window_start_batch = 0;

    void validate() const;
    [[nodiscard]] double strategy_weight(std::uint64_t one_based_batch) const;
    [[nodiscard]] std::uint64_t identity() const noexcept;
};

struct MultiwayBlueprintTrainingStatus {
    std::uint64_t batches = 0;
    std::uint64_t trajectories = 0;
    std::uint64_t discarded_trajectories = 0;
    std::uint64_t merged_stream_fingerprint = 0;
    std::uint64_t pruned_negative_regrets = 0;
    std::uint64_t visited_public_descriptors = 0;
    std::uint64_t admitted_rows = 0;
    std::uint64_t preflop_rows = 0;
    std::uint64_t flop_rows = 0;
    std::uint64_t turn_rows = 0;
    std::uint64_t river_rows = 0;
    std::uint64_t admitted_action_cells = 0;
    std::uint64_t terminal_visits = 0;
    std::uint64_t leaf_visits = 0;
    std::array<std::uint64_t, 4> street_visits{};
    std::uint64_t missing_lookup_requests = 0;
    std::uint64_t late_window_start_batch = 0;
    bool late_window_active = false;
    // Batch-boundary telemetry. These fields are diagnostic and are not part
    // of the strategy identity or deterministic merge state.
    std::uint64_t configured_max_public_states = 0;
    std::uint64_t configured_max_sparse_rows = 0;
    std::uint64_t configured_max_sparse_values = 0;
    std::uint64_t configured_worker_delta_capacity = 0;
    std::uint64_t peak_public_states = 0;
    std::uint64_t peak_sparse_rows = 0;
    std::uint64_t peak_sparse_values = 0;
    std::uint64_t peak_worker_delta_entries = 0;
    std::uint64_t cumulative_worker_delta_entries = 0;
    std::uint64_t peak_compact_storage_bytes = 0;
    std::uint64_t current_process_rss_bytes = 0;
    std::uint64_t peak_process_rss_bytes = 0;
    std::uint64_t elapsed_wall_nanoseconds = 0;
    std::uint64_t worker_active_nanoseconds = 0;
    std::uint64_t coordinator_wait_nanoseconds = 0;
    std::uint64_t delta_sort_nanoseconds = 0;
    std::uint64_t merge_nanoseconds = 0;
    std::uint64_t minimum_worker_trajectories = 0;
    std::uint64_t maximum_worker_trajectories = 0;
    std::uint32_t requested_worker_count = 0;
    std::uint32_t effective_worker_count = 0;
    std::uint64_t trajectories_per_batch = 0;
    std::uint64_t memory_preflight_estimate_bytes = 0;
    bool process_rss_available = false;
    std::vector<std::uint64_t> admitted_rows_per_batch;
    MultiwayTrainingCapacityStage capacity_exhaustion_stage =
        MultiwayTrainingCapacityStage::None;
};

// Public-only coverage summary for a sparse full-blueprint export. Zero
// counts remain meaningful: they distinguish an empty sparse table from an
// unavailable report without retaining private cards or ranges.
struct MultiwayBlueprintCoverageManifest {
    std::uint64_t visited_public_descriptors = 0;
    std::uint64_t admitted_rows = 0;
    std::uint64_t admitted_action_cells = 0;
    std::uint64_t terminal_visits = 0;
    std::uint64_t leaf_visits = 0;
    std::uint64_t missing_lookup_requests = 0;
    std::uint64_t preflop_rows = 0;
    std::uint64_t flop_rows = 0;
    std::uint64_t turn_rows = 0;
    std::uint64_t river_rows = 0;
    std::array<std::uint64_t, 4> street_visits{};
};

struct MultiwayBlueprintTrainingCheckpoint {
    MultiwayModelIdentity identity{};
    MultiwayBlueprintTrainingMetadata training{};
    MultiwayBlueprintCoverageManifest coverage{};
    MultiwayCoordinatorCheckpoint coordinator{};
    std::vector<double> late_window_baseline;

    void validate() const;
};

// Production composition boundary. Artifact inputs are non-owning and must
// outlive a training session; their model identity is validated on creation.
struct MultiwayBlueprintTrainingConfig {
    MultiwayGameRules rules = MultiwayGameRules::standard_6max();
    MultiwayBlueprintConfig blueprint{};
    MultiwayBucketBaselineProfile bucket_profile = MultiwayBucketBaselineProfile::standard();
    MultiwayActionAbstractionConfig action_abstraction{};
    MultiwayCFRConfig cfr{6, true};
    MultiwaySolverLimits limits{1, 1, 1024, 1024, 8192, 8192, 8192};
    MultiwayBlueprintIterationSchedule schedule{};
    std::uint64_t deterministic_seed = 1;
    // The production default is a complete preflop-to-river traversal. Tests
    // and constrained DEV profiles may explicitly select shallower limits.
    std::uint32_t max_decision_depth = MULTIWAY_MAX_DECISION_DEPTH;
    std::uint32_t max_public_chance_depth = MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH;
    // Diagnostic execution metadata. Zero uses the effective runtime value.
    std::uint32_t requested_worker_count = 0U;
    std::uint64_t memory_preflight_estimate_bytes = 0U;

    void validate() const;
    [[nodiscard]] MultiwayModelIdentity identity() const;
};

// Coordinator-owned offline session. Publishing creates an immutable compact
// root artifact and never exposes dense sparse-row storage to callers.
class MultiwayBlueprintTrainer {
public:
    MultiwayBlueprintTrainer(
        MultiwayModelIdentity identity,
        MultiwayRootBatchRunner& batch_runner,
        MultiwaySolverCoordinator& coordinator,
        MultiwayBlueprintIterationSchedule schedule = {},
        std::uint64_t deterministic_seed = 1,
        std::uint32_t requested_worker_count = 0U,
        std::uint64_t memory_preflight_estimate_bytes = 0U);

    void run_batches(
        std::uint64_t batch_count,
        std::uint64_t trajectories_per_batch,
        std::uint64_t seed);

    [[nodiscard]] std::uint64_t trajectories() const noexcept { return status_.trajectories; }
    [[nodiscard]] std::uint64_t batches() const noexcept { return status_.batches; }
    [[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept { return status_; }
    [[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
    [[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
    [[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;
    [[nodiscard]] MultiwayBlueprintSnapshot publish(
        MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
    void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
    void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);

private:
    MultiwayModelIdentity identity_{};
    MultiwayRootBatchRunner* batch_runner_ = nullptr;
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    MultiwayBlueprintIterationSchedule schedule_{};
    std::uint64_t deterministic_seed_ = 1;
    std::chrono::steady_clock::time_point started_at_{};
    MultiwayBlueprintTrainingStatus status_{};
    std::vector<double> late_window_baseline_;
};

// Owns the production assembly while retaining only non-owning bucket/leaf
// inputs. It is intentionally a library surface, not a process or service.
class MultiwayBlueprintTrainingSession {
public:
    MultiwayBlueprintTrainingSession(
        MultiwayBlueprintTrainingConfig config,
        MultiwayRootSnapshot root,
        const MultiwayBucketRegistry& buckets,
        const MultiwayLeafEvaluator* leaf_evaluator = nullptr);

    void run_batches(std::uint64_t batch_count);
    void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
    void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);
    [[nodiscard]] MultiwayBlueprintSnapshot export_policy(
        MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
    [[nodiscard]] const MultiwayBlueprintTrainingConfig& config() const noexcept { return config_; }
    [[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept;
    [[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
    [[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
    [[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;

private:
    MultiwayBlueprintTrainingConfig config_{};
    MultiwayRootSnapshot root_{};
    const MultiwayBucketRegistry* buckets_ = nullptr;
    MultiwayLeafEvaluator leaf_evaluator_{};
    std::unique_ptr<MultiwayActionAbstraction> action_abstraction_;
    std::unique_ptr<MultiwayFixedContinuationSelector> continuation_selector_;
    std::unique_ptr<MultiwaySolverCoordinator> coordinator_;
    std::unique_ptr<MultiwayRootBatchRunner> batch_runner_;
    std::unique_ptr<MultiwayBlueprintTrainer> trainer_;
};

}  // namespace texas::solver::multiway
