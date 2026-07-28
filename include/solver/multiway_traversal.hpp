#pragma once

#include "solver/multiway_cfr.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"

#include <cstdint>

namespace core {

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
        const MultiwayExternalSamplingRequest& request);
};

// Initial executable traversal scope. It evaluates every root action at an
// exact terminal or a supplied depth-limit leaf, then emits a worker-local
// external-sampling update. Deeper recursive sampling extends this boundary
// without changing its root/worker contracts.
class MultiwayRootExternalSamplingTraversal {
public:
    MultiwayRootExternalSamplingTraversal(
        MultiwaySolverCoordinator& coordinator,
        const MultiwayRootSnapshot& root,
        const MultiwayActionAbstraction& action_abstraction,
        const MultiwayBucketRegistry& buckets,
        const MultiwayLeafEvaluator* leaf_evaluator = nullptr);

    [[nodiscard]] bool run(
        PlayerId traverser,
        std::uint64_t trajectory_id,
        std::uint64_t seed,
        MultiwayWorkerDeltaStream& stream);

    [[nodiscard]] PlayerId root_traverser() const noexcept {
        return root_->public_state.betting.current_player;
    }

private:
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    const MultiwayRootSnapshot* root_ = nullptr;
    const MultiwayActionAbstraction* action_abstraction_ = nullptr;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    const MultiwayLeafEvaluator* leaf_evaluator_ = nullptr;
};

struct MultiwayRootBatchResult {
    std::uint64_t trajectories_attempted = 0;
    std::uint64_t trajectories_accepted = 0;
    std::uint64_t trajectories_discarded = 0;
    std::uint64_t delta_entries_merged = 0;
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
        std::uint64_t seed);

private:
    MultiwayRootExternalSamplingTraversal traversal_;
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    std::uint32_t worker_count_ = 0;
    std::size_t worker_delta_capacity_ = 0;
};

}  // namespace core
