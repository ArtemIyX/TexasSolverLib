#pragma once

#include "solver/multiway_blueprint_config.hpp"

#include <cstdint>

namespace core {

// A checkpoint and its queries must agree on every abstraction and settlement
// input. Hashes are stable FNV-1a fingerprints, not security hashes.
struct MultiwayModelIdentity {
    std::uint64_t rules_hash = 0;
    std::uint64_t action_abstraction_hash = 0;
    std::uint64_t bucket_model_hash = 0;
    std::uint64_t terminal_model_hash = 0;
    std::uint64_t code_schema_hash = 0;
    std::uint64_t combined_hash = 0;

    void validate() const;

    constexpr bool operator==(const MultiwayModelIdentity& other) const noexcept {
        return rules_hash == other.rules_hash &&
               action_abstraction_hash == other.action_abstraction_hash &&
               bucket_model_hash == other.bucket_model_hash &&
               terminal_model_hash == other.terminal_model_hash &&
               code_schema_hash == other.code_schema_hash &&
               combined_hash == other.combined_hash;
    }
    constexpr bool operator!=(const MultiwayModelIdentity& other) const noexcept {
        return !(*this == other);
    }
};

[[nodiscard]] MultiwayModelIdentity make_multiway_model_identity(
    const MultiwayBlueprintConfig& config);

}  // namespace core
