#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"

#include <limits>

namespace texas::solver::multiway {

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
    if (!row.valid()) return MultiwayBlueprintLookupStatus::Missing;
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
