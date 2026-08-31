#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"

#include "core/fingerprint.hpp"

#include <limits>

namespace texas::solver::multiway {

void MultiwayBlueprintLookupAudit::record(
    MultiwayBlueprintLookupStatus status, MultiwayInfosetId infoset,
    std::uint32_t bucket, std::uint64_t action_menu_id) noexcept {
    core::fingerprint::append_u64(fingerprint_state_, static_cast<std::uint8_t>(status));
    core::fingerprint::append_u64(fingerprint_state_, infoset.public_state.value);
    core::fingerprint::append_u64(fingerprint_state_,
        static_cast<std::uint64_t>(static_cast<std::int64_t>(infoset.seat)));
    core::fingerprint::append_u64(fingerprint_state_, bucket);
    core::fingerprint::append_u64(fingerprint_state_, action_menu_id);
    switch (status) {
        case MultiwayBlueprintLookupStatus::Hit: ++lookup_hits; break;
        case MultiwayBlueprintLookupStatus::Missing: ++missing_infosets; break;
        case MultiwayBlueprintLookupStatus::MissingBucket: ++missing_buckets; break;
        case MultiwayBlueprintLookupStatus::IncompatibleMenu: ++action_menu_mismatches; break;
    }
}

std::uint64_t MultiwayBlueprintLookupAudit::fingerprint() const noexcept {
    return core::fingerprint::finish(fingerprint_state_);
}

MultiwayBlueprintLookupStatus MultiwayBlueprintPolicyProvider::strategy_into(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    const MultiwayActionDescriptor* legal_actions,
    std::size_t action_count,
    Probability* output) const noexcept {
    if (store_ == nullptr || legal_actions == nullptr || output == nullptr || action_count == 0U) {
        return MultiwayBlueprintLookupStatus::Missing;
    }
    const auto menu_id = legal_actions[0].action_menu_id;
    const auto row = store_->find(infoset, bucket, menu_id);
    if (!row.valid()) {
        if (store_->has_infoset_bucket(infoset, bucket)) {
            return MultiwayBlueprintLookupStatus::IncompatibleMenu;
        }
        return store_->has_infoset(infoset)
            ? MultiwayBlueprintLookupStatus::MissingBucket
            : MultiwayBlueprintLookupStatus::Missing;
    }
    if (row.action_count != action_count) return MultiwayBlueprintLookupStatus::IncompatibleMenu;
    for (std::size_t index = 0U; index < action_count; ++index) {
        if (row.actions[index].action != legal_actions[index]) {
            return MultiwayBlueprintLookupStatus::IncompatibleMenu;
        }
    }
    constexpr auto total = static_cast<Probability>(std::numeric_limits<std::uint16_t>::max());
    for (std::size_t index = 0U; index < action_count; ++index) {
        output[index] = static_cast<Probability>(row.actions[index].probability) / total;
    }
    return MultiwayBlueprintLookupStatus::Hit;
}

}  // namespace texas::solver::multiway
