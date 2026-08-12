#pragma once

#include "core/namespaces.hpp"

#include "solver/multiway_blueprint_store.hpp"

#include <cstddef>

namespace texas::solver::multiway {

enum class MultiwayBlueprintLookupStatus : std::uint8_t {
    Hit,
    Missing,
    IncompatibleMenu,
};

// Read-only, lock-free policy lookup for traversal. A miss is explicit so
// callers can select their configured local or static fallback.
class MultiwayBlueprintPolicyProvider {
public:
    explicit MultiwayBlueprintPolicyProvider(const MultiwayBlueprintStore& store) noexcept : store_(&store) {}

    [[nodiscard]] MultiwayBlueprintLookupStatus strategy_into(
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        const MultiwayActionDescriptor* legal_actions,
        std::size_t action_count,
        Probability* output) const noexcept;

private:
    const MultiwayBlueprintStore* store_ = nullptr;
};

}  // namespace texas::solver::multiway
