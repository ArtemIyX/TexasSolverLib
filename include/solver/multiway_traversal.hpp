#pragma once

#include "solver/multiway_cfr.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_public_builder.hpp"
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

private:
    MultiwaySolverCoordinator* coordinator_ = nullptr;
    const MultiwayRootSnapshot* root_ = nullptr;
    const MultiwayActionAbstraction* action_abstraction_ = nullptr;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    const MultiwayLeafEvaluator* leaf_evaluator_ = nullptr;
};

}  // namespace core
