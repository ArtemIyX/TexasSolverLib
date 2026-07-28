#pragma once

#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace core {

struct MultiwayQuantizedRootAction {
    MultiwayActionDescriptor action{};
    std::uint16_t probability = 0;
};

// Immutable root-only artifact for deployment. It intentionally excludes
// global rows, reach tables, and worker scratch.
struct MultiwayBlueprintSnapshot {
    MultiwayModelIdentity identity{};
    MultiwayPublicStateId public_state{};
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0;
    std::uint64_t trajectories = 0;
    std::vector<MultiwayQuantizedRootAction> actions;

    void validate() const;
};

[[nodiscard]] MultiwayBlueprintSnapshot export_multiway_root_snapshot(
    const MultiwayModelIdentity& identity,
    const MultiwaySolverCoordinator& coordinator,
    std::uint64_t trajectories);

}  // namespace core
