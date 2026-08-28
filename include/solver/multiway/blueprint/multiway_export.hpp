#pragma once

#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayQuantizedRootAction {
    MultiwayActionDescriptor action{};
    std::uint16_t probability = 0;
};

enum class MultiwayBlueprintPolicyKind : std::uint8_t {
    Current,
    WeightedAverage,
    LateWindowAverage,
};

// Persisted resume boundary. It intentionally stores no row data: a compact
// root artifact may resume only when a caller also retains its live trainer.
struct MultiwayBlueprintTrainingMetadata {
    std::uint64_t batches = 0;
    std::uint64_t trajectories = 0;
    std::uint64_t deterministic_seed = 0;
    std::uint64_t late_window_start_batch = 0;
    std::uint64_t schedule_hash = 0;
    std::uint64_t pruned_negative_regrets = 0;
    std::uint64_t terminal_visits = 0;
    std::uint64_t leaf_visits = 0;
    std::uint64_t missing_lookup_requests = 0;
    std::uint8_t linear_iteration_weighting = 0;
    std::uint8_t discounting_enabled = 0;
    std::uint8_t negative_regret_pruning_enabled = 0;
    std::uint8_t reserved = 0;
};

// Immutable root-only artifact for deployment. It intentionally excludes
// global rows, reach tables, and worker scratch.
struct MultiwayBlueprintSnapshot {
    MultiwayModelIdentity identity{};
    MultiwayPublicStateId public_state{};
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0;
    std::uint64_t trajectories = 0;
    MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage;
    MultiwayBlueprintTrainingMetadata training{};
    std::vector<MultiwayQuantizedRootAction> actions;

    void validate() const;
};

[[nodiscard]] MultiwayBlueprintSnapshot export_multiway_root_snapshot(
    const MultiwayModelIdentity& identity,
    const MultiwaySolverCoordinator& coordinator,
    std::uint64_t trajectories,
    MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage,
    const MultiwayBlueprintTrainingMetadata& training = {});

}  // namespace texas::solver::multiway
