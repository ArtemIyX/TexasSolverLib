#pragma once

#include "solver/multiway_export.hpp"

#include <cstddef>

namespace core {

class MultiwayBlueprintQuery {
public:
    [[nodiscard]] static Probability root_action_probability(
        const MultiwayBlueprintSnapshot& snapshot,
        const MultiwayModelIdentity& expected_identity,
        std::size_t action_index);

    [[nodiscard]] static MultiwayRootActionProbability root_action(
        const MultiwayBlueprintSnapshot& snapshot,
        const MultiwayModelIdentity& expected_identity,
        std::size_t action_index);
};

}  // namespace core
