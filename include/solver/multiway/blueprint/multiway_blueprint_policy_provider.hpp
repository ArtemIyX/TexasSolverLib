#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"

#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway {

enum class MultiwayBlueprintLookupStatus : std::uint8_t {
    Hit,
    Missing,
    MissingBucket,
    IncompatibleMenu,
};

struct MultiwayBlueprintLookupAudit {
    std::uint64_t lookup_hits = 0U;
    std::uint64_t missing_infosets = 0U;
    std::uint64_t missing_buckets = 0U;
    std::uint64_t action_menu_mismatches = 0U;

    void record(MultiwayBlueprintLookupStatus status, MultiwayInfosetId infoset,
                std::uint32_t bucket, std::uint64_t action_menu_id) noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

private:
    std::uint64_t fingerprint_state_ = 1469598103934665603ULL;
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
