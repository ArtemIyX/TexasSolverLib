#pragma once

#include "solver/multiway/engine/multiway_cfr.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/continuation/multiway_leaf_evaluator.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/engine/multiway_scheduler.hpp"
#include "solver/multiway/session/multiway_search_profile.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_terminal_adapter.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace texas::solver::multiway {

class MultiwayBlueprintPolicyProvider;
class MultiwayFutureBucketArtifact;

inline constexpr std::uint32_t MULTIWAY_MAX_DECISION_DEPTH = 64U;
inline constexpr std::uint32_t MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH = 3U;
// The production abstraction emits at most five entries; one exact off-tree
// insertion needs six. Two spare entries keep the boundary explicit.
inline constexpr std::size_t MULTIWAY_MAX_TRAVERSAL_ACTIONS = MULTIWAY_MAX_ABSTRACTED_ACTIONS;

struct MultiwayTraversalPruningConfig {
    bool enabled = false;
    std::uint64_t warmup_batches = 0;
    std::uint64_t recovery_interval_batches = 20;
    double exploration_probability = 0.05;
    double action_probability_threshold = 0.0;
    double regret_threshold = 0.0;

    void validate() const;
    [[nodiscard]] bool recovery_batch(std::uint64_t batch_number) const noexcept;
    [[nodiscard]] bool should_explore_action(
        std::uint64_t batch_number, bool below_threshold,
        bool immediate_terminal, bool river) const noexcept;
};

// Bounded lazy traversal. It samples opponent and public-chance nodes,
// enumerates traverser decisions, and stops at exact terminals or the typed
// strategic leaf boundary.
class MultiwayRootExternalSamplingTraversal {
public:
    MultiwayRootExternalSamplingTraversal(
        MultiwaySolverCoordinator& coordinator,
        const MultiwayRootSnapshot& root,
        const MultiwayActionAbstraction& action_abstraction,
        const MultiwayBucketRegistry& buckets,
        const MultiwayLeafEvaluator* leaf_evaluator = nullptr,
        std::uint32_t max_decision_depth = 1U,
        std::uint32_t max_public_chance_depth = 0U,
        const MultiwayBlueprintPolicyProvider* blueprint_policy = nullptr,
        const MultiwayFixedContinuationSelector* continuation_selector = nullptr,
        const MultiwayFutureBucketArtifact* future_bucket_artifact = nullptr,
        MultiwayTraversalPruningConfig pruning = {});

    [[nodiscard]] bool run(
        PlayerId traverser,
        std::uint64_t trajectory_id,
        std::uint64_t seed,
        MultiwayWorkerDeltaStream& stream,
        double iteration_weight = 1.0,
        MultiwaySearchProfile* profile = nullptr,
        MultiwayContinuationDeltaStream* continuation_stream = nullptr,
        std::uint64_t batch_number = 0U) const;

    [[nodiscard]] const MultiwayFixedContinuationSelector* continuation_selector() const noexcept {
        return continuation_selector_;
    }

    [[nodiscard]] PlayerId root_traverser() const noexcept {
        return root_->public_state.betting.current_player;
    }

    [[nodiscard]] PlayerId traverser_for_trajectory(
        std::uint64_t trajectory_id) const noexcept {
        return root_->seat_order[trajectory_id % root_->seat_order.size()];
    }

private:
    struct TraversalContext;

    [[nodiscard]] Value traverse_decision(
        const MultiwayPublicStateDescriptor& state,
        std::uint32_t decision_depth,
        std::uint32_t public_chance_depth,
        TraversalContext& context) const;

    [[nodiscard]] Value traverse_public_chance(
        const MultiwayPublicStateDescriptor& state,
        std::uint32_t decision_depth,
        std::uint32_t public_chance_depth,
        TraversalContext& context) const;

    [[nodiscard]] Value evaluate_leaf(
        const MultiwayPublicStateDescriptor& state,
        TraversalContext& context) const;

    MultiwaySolverCoordinator* coordinator_ = nullptr;
    const MultiwayRootSnapshot* root_ = nullptr;
    const MultiwayActionAbstraction* action_abstraction_ = nullptr;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    const MultiwayLeafEvaluator* leaf_evaluator_ = nullptr;
    std::uint32_t max_decision_depth_ = 1U;
    std::uint32_t max_public_chance_depth_ = 0U;
    const MultiwayBlueprintPolicyProvider* blueprint_policy_ = nullptr;
    const MultiwayFixedContinuationSelector* continuation_selector_ = nullptr;
    const MultiwayFutureBucketArtifact* future_bucket_artifact_ = nullptr;
    MultiwayTraversalPruningConfig pruning_{};
    std::uint64_t range_model_identity_ = 0;
    MultiwayTerminalAdapter terminal_;
};

struct MultiwayRootBatchResult {
    std::uint64_t trajectories_attempted = 0;
    std::uint64_t trajectories_accepted = 0;
    std::uint64_t trajectories_discarded = 0;
    std::uint64_t delta_entries_merged = 0;
    std::uint64_t minimum_worker_trajectories = 0;
    std::uint64_t maximum_worker_trajectories = 0;
    MultiwaySearchProfileSnapshot profile{};
    MultiwayRunMetadata run{};
    bool clean = false;
};

// Deterministic root-only batch runner. Workers are kept logically separate
// until coordinator merge; parallel dispatch can be introduced without
// changing seeds, partitions, or merge order.
class MultiwayRootBatchRunner {
public:
    MultiwayRootBatchRunner(
        MultiwayRootExternalSamplingTraversal traversal,
        MultiwaySolverCoordinator& coordinator,
        std::uint32_t worker_count,
        std::size_t worker_delta_capacity,
        MultiwaySearchProfileMode profile_mode = MultiwaySearchProfileMode::Disabled);
    ~MultiwayRootBatchRunner();

    MultiwayRootBatchRunner(const MultiwayRootBatchRunner&) = delete;
    MultiwayRootBatchRunner& operator=(const MultiwayRootBatchRunner&) = delete;

    [[nodiscard]] MultiwayRootBatchResult run(
        std::uint64_t first_trajectory_id,
        std::uint64_t trajectory_count,
        std::uint64_t seed,
        double iteration_weight = 1.0,
            std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max());
    void set_batch_number(std::uint64_t batch_number) noexcept { active_batch_number_ = batch_number; }

private:
    void worker_loop(std::size_t worker_index);

    struct WorkerScratch {
        explicit WorkerScratch(std::size_t worker_index, std::size_t delta_capacity)
            : stream(worker_index, delta_capacity), continuation_stream(worker_index, delta_capacity) {}

        MultiwayWorkerDeltaStream stream;
        MultiwayContinuationDeltaStream continuation_stream;
        std::uint64_t attempted = 0;
        std::uint64_t accepted = 0;
        std::uint64_t discarded = 0;
        MultiwaySearchProfile profile{};

        void reset(MultiwaySearchProfileMode profile_mode) noexcept {
            stream.rewind(0U);
            continuation_stream.rewind(0U);
            attempted = 0;
            accepted = 0;
            discarded = 0;
            profile.reset(profile_mode);
        }
    };

    MultiwayRootExternalSamplingTraversal traversal_;
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    std::uint32_t worker_count_ = 0;
    std::size_t worker_delta_capacity_ = 0;
    MultiwaySearchProfileMode profile_mode_ = MultiwaySearchProfileMode::Disabled;
    std::vector<WorkerScratch> worker_scratch_;
    std::vector<const MultiwayWorkerDeltaStream*> worker_stream_views_;
    std::vector<const MultiwayContinuationDeltaStream*> continuation_stream_views_;
    std::vector<MultiwayWorkerBatch> worker_batches_;
    std::vector<std::thread> threads_;
    std::mutex pool_mutex_;
    std::condition_variable work_cv_;
    std::condition_variable completion_cv_;
    bool stop_workers_ = false;
    bool batch_active_ = false;
    std::uint64_t batch_generation_ = 0U;
    std::size_t completed_workers_ = 0U;
    std::size_t active_batch_count_ = 0U;
    std::uint64_t active_first_trajectory_id_ = 0U;
    std::uint64_t active_seed_ = 0U;
    std::uint64_t active_batch_number_ = 0U;
    double active_iteration_weight_ = 1.0;
    std::chrono::steady_clock::time_point active_deadline_ =
        std::chrono::steady_clock::time_point::max();
    std::atomic<bool> cancelled_{false};
    std::exception_ptr worker_error_;
};

}  // namespace texas::solver::multiway
