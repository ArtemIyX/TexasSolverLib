#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_blueprint_config.hpp"

#include <cstdint>

namespace texas::solver::multiway {

// A checkpoint and its queries must agree on every abstraction and settlement
// input. Hashes are stable FNV-1a fingerprints, not security hashes.
struct MultiwayModelIdentity {
    std::uint64_t rules_hash = 0;
    std::uint64_t rules_schema_hash = 0;
    std::uint64_t action_abstraction_hash = 0;
    std::uint64_t bucket_model_hash = 0;
    std::uint64_t terminal_model_hash = 0;
    std::uint64_t resolver_schema_hash = 0;
    std::uint64_t code_schema_hash = 0;
    std::uint64_t range_semantics_hash = 0;
    std::uint64_t future_bucket_model_hash = 0;
    std::uint64_t off_tree_policy_hash = 0;
    std::uint64_t continuation_policy_hash = 0;
    std::uint64_t runtime_search_schema_hash = 0;
    std::uint64_t combined_hash = 0;

    void validate() const;

    constexpr bool operator==(const MultiwayModelIdentity& other) const noexcept {
        return rules_hash == other.rules_hash &&
               rules_schema_hash == other.rules_schema_hash &&
               action_abstraction_hash == other.action_abstraction_hash &&
               bucket_model_hash == other.bucket_model_hash &&
               terminal_model_hash == other.terminal_model_hash &&
               resolver_schema_hash == other.resolver_schema_hash &&
               code_schema_hash == other.code_schema_hash &&
               range_semantics_hash == other.range_semantics_hash &&
               future_bucket_model_hash == other.future_bucket_model_hash &&
               off_tree_policy_hash == other.off_tree_policy_hash &&
               continuation_policy_hash == other.continuation_policy_hash &&
               runtime_search_schema_hash == other.runtime_search_schema_hash &&
               combined_hash == other.combined_hash;
    }
    constexpr bool operator!=(const MultiwayModelIdentity& other) const noexcept {
        return !(*this == other);
    }
};

template <typename Function>
void visit_multiway_model_identity_components(
    MultiwayModelIdentity& identity,
    Function&& function) {
    function(identity.rules_hash);
    function(identity.rules_schema_hash);
    function(identity.action_abstraction_hash);
    function(identity.bucket_model_hash);
    function(identity.terminal_model_hash);
    function(identity.resolver_schema_hash);
    function(identity.code_schema_hash);
    function(identity.range_semantics_hash);
    function(identity.future_bucket_model_hash);
    function(identity.off_tree_policy_hash);
    function(identity.continuation_policy_hash);
    function(identity.runtime_search_schema_hash);
}

template <typename Function>
void visit_multiway_model_identity_components(
    const MultiwayModelIdentity& identity,
    Function&& function) {
    function(identity.rules_hash);
    function(identity.rules_schema_hash);
    function(identity.action_abstraction_hash);
    function(identity.bucket_model_hash);
    function(identity.terminal_model_hash);
    function(identity.resolver_schema_hash);
    function(identity.code_schema_hash);
    function(identity.range_semantics_hash);
    function(identity.future_bucket_model_hash);
    function(identity.off_tree_policy_hash);
    function(identity.continuation_policy_hash);
    function(identity.runtime_search_schema_hash);
}

template <typename Function>
void visit_multiway_model_identity_fields(MultiwayModelIdentity& identity, Function&& function) {
    visit_multiway_model_identity_components(identity, function);
    function(identity.combined_hash);
}

template <typename Function>
void visit_multiway_model_identity_fields(
    const MultiwayModelIdentity& identity,
    Function&& function) {
    visit_multiway_model_identity_components(identity, function);
    function(identity.combined_hash);
}

[[nodiscard]] MultiwayModelIdentity make_multiway_model_identity(
    const MultiwayBlueprintConfig& config);

}  // namespace texas::solver::multiway
