#pragma once

#include "solver/multiway_cfr.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"

#include <cstddef>
#include <cstdint>

namespace core {

class MultiwayBlueprintPolicyProvider;
class MultiwayFixedContinuationSelector;

inline constexpr std::uint32_t MULTIWAY_MAX_DECISION_DEPTH = 64U;
inline constexpr std::uint32_t MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH = 3U;
// The production abstraction emits at most five entries; one exact off-tree
// insertion needs six. Two spare entries keep the boundary explicit.
inline constexpr std::size_t MULTIWAY_MAX_TRAVERSAL_ACTIONS = MULTIWAY_MAX_ABSTRACTED_ACTIONS;

// Allocation-free after the caller has prepared the external-sampling request.
// Recursive game traversal owns action-value estimation; this kernel owns only
// the mathematically shared CFR-to-worker-delta conversion.
class MultiwayExternalSamplingTraversal {
public:
    [[nodiscard]] static bool append_infoset_update(
        MultiwayWorkerDeltaStream& stream,
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        std::uint64_t trajectory_id,
        const MultiwayExternalSamplingRequest& request,
        double iteration_weight = 1.0);
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
        const MultiwayFixedContinuationSelector* continuation_selector = nullptr);

    [[nodiscard]] bool run(
        PlayerId traverser,
        std::uint64_t trajectory_id,
        std::uint64_t seed,
        MultiwayWorkerDeltaStream& stream,
        double iteration_weight = 1.0) const;

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
        const TraversalContext& context) const;

    MultiwaySolverCoordinator* coordinator_ = nullptr;
    const MultiwayRootSnapshot* root_ = nullptr;
    const MultiwayActionAbstraction* action_abstraction_ = nullptr;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    const MultiwayLeafEvaluator* leaf_evaluator_ = nullptr;
    std::uint32_t max_decision_depth_ = 1U;
    std::uint32_t max_public_chance_depth_ = 0U;
    const MultiwayBlueprintPolicyProvider* blueprint_policy_ = nullptr;
    const MultiwayFixedContinuationSelector* continuation_selector_ = nullptr;
    MultiwayTerminalAdapter terminal_;
};

struct MultiwayRootBatchResult {
    std::uint64_t trajectories_attempted = 0;
    std::uint64_t trajectories_accepted = 0;
    std::uint64_t trajectories_discarded = 0;
    std::uint64_t delta_entries_merged = 0;
    std::uint64_t minimum_worker_trajectories = 0;
    std::uint64_t maximum_worker_trajectories = 0;
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
        std::size_t worker_delta_capacity);

    [[nodiscard]] MultiwayRootBatchResult run(
        std::uint64_t first_trajectory_id,
        std::uint64_t trajectory_count,
        std::uint64_t seed,
        double iteration_weight = 1.0);

    // Test-only failure injection verifies join-before-merge behavior.
    void set_test_worker_failure_for_testing(std::int32_t worker_index) noexcept;

private:
    struct WorkerScratch {
        explicit WorkerScratch(std::size_t worker_index, std::size_t delta_capacity)
            : stream(worker_index, delta_capacity) {}

        MultiwayWorkerDeltaStream stream;
        std::uint64_t attempted = 0;
        std::uint64_t accepted = 0;
        std::uint64_t discarded = 0;

        void reset() noexcept {
            stream.rewind(0U);
            attempted = 0;
            accepted = 0;
            discarded = 0;
        }
    };

    MultiwayRootExternalSamplingTraversal traversal_;
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    std::uint32_t worker_count_ = 0;
    std::size_t worker_delta_capacity_ = 0;
    std::vector<WorkerScratch> worker_scratch_;
    std::vector<const MultiwayWorkerDeltaStream*> worker_stream_views_;
    std::int32_t test_worker_failure_index_ = -1;
};

}  // namespace core
