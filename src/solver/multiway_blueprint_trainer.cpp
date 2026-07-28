#include "solver/multiway_blueprint_trainer.hpp"

#include <limits>
#include <stdexcept>

namespace core {

MultiwayBlueprintTrainer::MultiwayBlueprintTrainer(
    MultiwayModelIdentity identity,
    MultiwayRootBatchRunner& batch_runner,
    const MultiwaySolverCoordinator& coordinator)
    : identity_(identity), batch_runner_(&batch_runner), coordinator_(&coordinator) {
    identity_.validate();
}

void MultiwayBlueprintTrainer::run_batches(
    std::uint64_t batch_count,
    std::uint64_t trajectories_per_batch,
    std::uint64_t seed) {
    if (trajectories_per_batch == 0U ||
        batch_count > (std::numeric_limits<std::uint64_t>::max() - trajectories_) / trajectories_per_batch) {
        throw std::invalid_argument("multiway blueprint trainer has an invalid batch request");
    }
    for (std::uint64_t batch = 0; batch < batch_count; ++batch) {
        (void)batch_runner_->run(trajectories_, trajectories_per_batch, seed + batch);
        trajectories_ += trajectories_per_batch;
        ++batches_;
    }
}

MultiwayBlueprintSnapshot MultiwayBlueprintTrainer::publish() const {
    return export_multiway_root_snapshot(identity_, *coordinator_, trajectories_);
}

}  // namespace core
