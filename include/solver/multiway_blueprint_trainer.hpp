#pragma once

#include "solver/multiway_export.hpp"
#include "solver/multiway_traversal.hpp"

#include <cstdint>

namespace core {

// Coordinator-owned offline session. Publishing creates an immutable compact
// root artifact and never exposes dense sparse-row storage to callers.
class MultiwayBlueprintTrainer {
public:
    MultiwayBlueprintTrainer(
        MultiwayModelIdentity identity,
        MultiwayRootBatchRunner& batch_runner,
        const MultiwaySolverCoordinator& coordinator);

    void run_batches(
        std::uint64_t batch_count,
        std::uint64_t trajectories_per_batch,
        std::uint64_t seed);

    [[nodiscard]] std::uint64_t trajectories() const noexcept { return trajectories_; }
    [[nodiscard]] std::uint64_t batches() const noexcept { return batches_; }
    [[nodiscard]] MultiwayBlueprintSnapshot publish() const;

private:
    MultiwayModelIdentity identity_{};
    MultiwayRootBatchRunner* batch_runner_ = nullptr;
    const MultiwaySolverCoordinator* coordinator_ = nullptr;
    std::uint64_t trajectories_ = 0;
    std::uint64_t batches_ = 0;
};

}  // namespace core
