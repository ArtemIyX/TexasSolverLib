#pragma once

#include "core/namespaces.hpp"

#include "solver/multiway_export.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_traversal.hpp"
#include "solver/multiway_bucket_artifact.hpp"
#include "games/multiway_rules.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayBlueprintIterationSchedule {
    bool linear_iteration_weighting = true;
    bool discount_regrets = false;
    double regret_discount_factor = 1.0;
    std::uint64_t discount_interval_batches = 1;
    bool prune_negative_regrets = false;
    std::uint64_t pruning_warmup_batches = 0;
    std::uint64_t pruning_interval_batches = 1;
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
    std::uint64_t pruned_negative_regrets = 0;
    std::uint64_t visited_public_descriptors = 0;
    std::uint64_t admitted_rows = 0;
    std::uint64_t admitted_action_cells = 0;
    std::uint64_t terminal_visits = 0;
    std::uint64_t leaf_visits = 0;
    std::uint64_t missing_lookup_requests = 0;
    std::uint64_t late_window_start_batch = 0;
    bool late_window_active = false;
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
};

struct MultiwayBlueprintTrainingCheckpoint {
    MultiwayModelIdentity identity{};
    MultiwayBlueprintTrainingMetadata training{};
    MultiwayCoordinatorCheckpoint coordinator{};
    std::vector<double> late_window_baseline;
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
    std::uint32_t max_decision_depth = 1;
    std::uint32_t max_public_chance_depth = 0;

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
        std::uint64_t deterministic_seed = 1);

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
    void resume_from(const MultiwayBlueprintSnapshot& checkpoint);
    void resume_from(const MultiwayBlueprintTrainingCheckpoint& checkpoint);

private:
    MultiwayModelIdentity identity_{};
    MultiwayRootBatchRunner* batch_runner_ = nullptr;
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    MultiwayBlueprintIterationSchedule schedule_{};
    std::uint64_t deterministic_seed_ = 1;
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
    void resume_from_checkpoint(const MultiwayBlueprintSnapshot& checkpoint);
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
    std::unique_ptr<MultiwaySolverCoordinator> coordinator_;
    std::unique_ptr<MultiwayRootBatchRunner> batch_runner_;
    std::unique_ptr<MultiwayBlueprintTrainer> trainer_;
};

}  // namespace texas::solver::multiway
