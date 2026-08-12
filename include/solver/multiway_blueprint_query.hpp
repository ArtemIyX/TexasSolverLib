#pragma once

#include "core/namespaces.hpp"

#include "solver/multiway_export.hpp"

#include <cstddef>

namespace texas::solver::multiway {

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

}  // namespace texas::solver::multiway
