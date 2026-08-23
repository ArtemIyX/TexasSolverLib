#include "solver/multiway_model_identity.hpp"
#include "core/fingerprint.hpp"

#include <stdexcept>

namespace texas::solver::multiway {
namespace {

using texas::core::fingerprint::append_u64;
using texas::core::fingerprint::finish;

std::uint64_t rules_hash(const MultiwayBlueprintConfig& config) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, config.player_count);
    append_u64(hash, static_cast<std::uint64_t>(config.initial_stack_chips));
    append_u64(hash, static_cast<std::uint64_t>(config.small_blind_chips));
    append_u64(hash, static_cast<std::uint64_t>(config.big_blind_chips));
    append_u64(hash, static_cast<std::uint64_t>(config.ante_chips));
    append_u64(hash, config.rake_policy.identity());
    append_u64(hash, config.rules_profile_version);
    return finish(hash);
}

}  // namespace

void MultiwayModelIdentity::validate() const {
    if (rules_hash == 0U || rules_schema_hash == 0U || action_abstraction_hash == 0U ||
        bucket_model_hash == 0U || terminal_model_hash == 0U || resolver_schema_hash == 0U ||
        code_schema_hash == 0U || range_semantics_hash == 0U || future_bucket_model_hash == 0U ||
        off_tree_policy_hash == 0U || continuation_policy_hash == 0U ||
        runtime_search_schema_hash == 0U || combined_hash == 0U) {
        throw std::invalid_argument("multiway model identity requires non-zero hashes");
    }
}

MultiwayModelIdentity make_multiway_model_identity(const MultiwayBlueprintConfig& config) {
    config.validate();

    MultiwayModelIdentity identity;
    identity.rules_hash = rules_hash(config);

    auto rules_schema_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(rules_schema_hash, config.rules_profile_version);
    identity.rules_schema_hash = finish(rules_schema_hash);

    auto action_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(action_hash, config.action_abstraction_version);
    identity.action_abstraction_hash = finish(action_hash);

    auto bucket_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(bucket_hash, config.bucket_model_version);
    append_u64(bucket_hash, config.flop_bucket_count);
    append_u64(bucket_hash, config.turn_bucket_count);
    append_u64(bucket_hash, config.river_bucket_count);
    identity.bucket_model_hash = finish(bucket_hash);

    auto terminal_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(terminal_hash, config.terminal_model_version);
    append_u64(terminal_hash, config.rake_policy.identity());
    identity.terminal_model_hash = finish(terminal_hash);

    auto resolver_schema_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(resolver_schema_hash, config.resolver_schema_version);
    identity.resolver_schema_hash = finish(resolver_schema_hash);

    auto schema_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(schema_hash, config.code_schema_version);
    identity.code_schema_hash = finish(schema_hash);

    auto range_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(range_hash, config.range_semantics_version);
    identity.range_semantics_hash = finish(range_hash);

    auto future_bucket_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(future_bucket_hash, config.future_bucket_model_version);
    identity.future_bucket_model_hash = finish(future_bucket_hash);

    auto off_tree_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(off_tree_hash, config.off_tree_policy_version);
    identity.off_tree_policy_hash = finish(off_tree_hash);

    auto continuation_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(continuation_hash, config.continuation_policy_version);
    append_u64(continuation_hash, static_cast<std::uint64_t>(config.continuation_policy));
    identity.continuation_policy_hash = finish(continuation_hash);

    auto runtime_search_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(runtime_search_hash, config.runtime_search_schema_version);
    identity.runtime_search_schema_hash = finish(runtime_search_hash);

    auto combined_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(combined_hash, identity.rules_hash);
    append_u64(combined_hash, identity.rules_schema_hash);
    append_u64(combined_hash, identity.action_abstraction_hash);
    append_u64(combined_hash, identity.bucket_model_hash);
    append_u64(combined_hash, identity.terminal_model_hash);
    append_u64(combined_hash, identity.resolver_schema_hash);
    append_u64(combined_hash, identity.code_schema_hash);
    append_u64(combined_hash, identity.range_semantics_hash);
    append_u64(combined_hash, identity.future_bucket_model_hash);
    append_u64(combined_hash, identity.off_tree_policy_hash);
    append_u64(combined_hash, identity.continuation_policy_hash);
    append_u64(combined_hash, identity.runtime_search_schema_hash);
    identity.combined_hash = finish(combined_hash);
    identity.validate();
    return identity;
}

}  // namespace texas::solver::multiway
