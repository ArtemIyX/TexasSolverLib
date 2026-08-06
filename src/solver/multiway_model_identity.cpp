#include "solver/multiway_model_identity.hpp"

#include <stdexcept>

namespace core {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

std::uint64_t finish(std::uint64_t hash) noexcept {
    return hash == 0U ? 1U : hash;
}

std::uint64_t rules_hash(const MultiwayBlueprintConfig& config) noexcept {
    auto hash = kFnvOffset;
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
        code_schema_hash == 0U || combined_hash == 0U) {
        throw std::invalid_argument("multiway model identity requires non-zero hashes");
    }
}

MultiwayModelIdentity make_multiway_model_identity(const MultiwayBlueprintConfig& config) {
    config.validate();

    MultiwayModelIdentity identity;
    identity.rules_hash = rules_hash(config);

    auto rules_schema_hash = kFnvOffset;
    append_u64(rules_schema_hash, config.rules_profile_version);
    identity.rules_schema_hash = finish(rules_schema_hash);

    auto action_hash = kFnvOffset;
    append_u64(action_hash, config.action_abstraction_version);
    identity.action_abstraction_hash = finish(action_hash);

    auto bucket_hash = kFnvOffset;
    append_u64(bucket_hash, config.bucket_model_version);
    append_u64(bucket_hash, config.flop_bucket_count);
    append_u64(bucket_hash, config.turn_bucket_count);
    append_u64(bucket_hash, config.river_bucket_count);
    identity.bucket_model_hash = finish(bucket_hash);

    auto terminal_hash = kFnvOffset;
    append_u64(terminal_hash, config.terminal_model_version);
    append_u64(terminal_hash, config.rake_policy.identity());
    identity.terminal_model_hash = finish(terminal_hash);

    auto resolver_schema_hash = kFnvOffset;
    append_u64(resolver_schema_hash, config.resolver_schema_version);
    identity.resolver_schema_hash = finish(resolver_schema_hash);

    auto schema_hash = kFnvOffset;
    append_u64(schema_hash, config.code_schema_version);
    identity.code_schema_hash = finish(schema_hash);

    auto combined_hash = kFnvOffset;
    append_u64(combined_hash, identity.rules_hash);
    append_u64(combined_hash, identity.rules_schema_hash);
    append_u64(combined_hash, identity.action_abstraction_hash);
    append_u64(combined_hash, identity.bucket_model_hash);
    append_u64(combined_hash, identity.terminal_model_hash);
    append_u64(combined_hash, identity.resolver_schema_hash);
    append_u64(combined_hash, identity.code_schema_hash);
    identity.combined_hash = finish(combined_hash);
    identity.validate();
    return identity;
}

}  // namespace core
