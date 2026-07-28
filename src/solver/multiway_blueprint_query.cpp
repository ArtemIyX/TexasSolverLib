#include "solver/multiway_blueprint_query.hpp"

#include <limits>
#include <stdexcept>

namespace core {

Probability MultiwayBlueprintQuery::root_action_probability(
    const MultiwayBlueprintSnapshot& snapshot,
    const MultiwayModelIdentity& expected_identity,
    std::size_t action_index) {
    snapshot.validate();
    expected_identity.validate();
    if (snapshot.identity != expected_identity) {
        throw std::invalid_argument("multiway blueprint query model identity mismatch");
    }
    if (action_index >= snapshot.actions.size()) {
        throw std::out_of_range("multiway blueprint query action index is unavailable");
    }
    return static_cast<Probability>(snapshot.actions[action_index].probability) /
        std::numeric_limits<std::uint16_t>::max();
}

MultiwayRootActionProbability MultiwayBlueprintQuery::root_action(
    const MultiwayBlueprintSnapshot& snapshot,
    const MultiwayModelIdentity& expected_identity,
    std::size_t action_index) {
    return {
        snapshot.actions.at(action_index).action,
        root_action_probability(snapshot, expected_identity, action_index),
    };
}

}  // namespace core
