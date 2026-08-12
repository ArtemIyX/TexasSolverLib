#include "solver/multiway_export.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway {

void MultiwayBlueprintSnapshot::validate() const {
    identity.validate();
    if (public_state.value == 0U || infoset.public_state != public_state || infoset.seat < 0 ||
        actions.empty() ||
        (policy_kind != MultiwayBlueprintPolicyKind::Current &&
         policy_kind != MultiwayBlueprintPolicyKind::WeightedAverage &&
         policy_kind != MultiwayBlueprintPolicyKind::LateWindowAverage) ||
        training.trajectories != trajectories) {
        throw std::invalid_argument("multiway blueprint snapshot has invalid root metadata");
    }
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].action.action_index != index || actions[index].action.action_menu_id == 0U) {
            throw std::invalid_argument("multiway blueprint snapshot has invalid action order");
        }
        total += actions[index].probability;
    }
    if (total != std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("multiway blueprint snapshot probabilities are not normalized");
    }
}

MultiwayBlueprintSnapshot export_multiway_root_snapshot(
    const MultiwayModelIdentity& identity,
    const MultiwaySolverCoordinator& coordinator,
    std::uint64_t trajectories,
    MultiwayBlueprintPolicyKind policy_kind,
    const MultiwayBlueprintTrainingMetadata& training) {
    identity.validate();
    const auto policy = policy_kind == MultiwayBlueprintPolicyKind::Current
        ? coordinator.export_root_current_policy()
        : coordinator.export_root_policy();
    auto persisted_training = training;
    if (persisted_training.trajectories == 0U) persisted_training.trajectories = trajectories;
    MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = identity;
    snapshot.public_state = policy.public_state;
    snapshot.infoset = policy.infoset;
    snapshot.bucket = policy.bucket;
    snapshot.trajectories = trajectories;
    snapshot.policy_kind = policy_kind;
    snapshot.training = persisted_training;
    snapshot.actions.reserve(policy.actions.size());
    std::uint32_t assigned = 0;
    for (std::size_t index = 0; index < policy.actions.size(); ++index) {
        const auto raw = policy.actions[index].probability * std::numeric_limits<std::uint16_t>::max();
        const std::uint16_t quantized = index + 1U == policy.actions.size()
            ? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
            : static_cast<std::uint16_t>(std::floor(raw));
        assigned += quantized;
        snapshot.actions.push_back({policy.actions[index].action, quantized});
    }
    snapshot.validate();
    return snapshot;
}

}  // namespace texas::solver::multiway
