#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

core::MultiwayBlueprintRow row(std::uint64_t state, core::PlayerId seat, std::uint32_t bucket) {
    core::MultiwayBlueprintRow result;
    result.infoset = {{state}, seat};
    result.bucket = bucket;
    result.action_menu_id = 77U;
    result.actions = {{{core::MultiwayAction::Check, 0U, 0, 77U}, 65535U}};
    return result;
}

core::MultiwayModelIdentity identity() {
    return core::make_multiway_model_identity(core::MultiwayBlueprintConfig{});
}

}  // namespace

TEST_CASE(multiway_blueprint_store_finds_128_sorted_rows) {
    std::vector<core::MultiwayBlueprintRow> rows;
    rows.reserve(128U);
    for (std::uint64_t index = 128U; index > 0U; --index) rows.push_back(row(index, 0, 0U));
    const core::MultiwayBlueprintStore store(identity(), std::move(rows));

    EXPECT_EQ(store.row_count(), std::size_t{128U});
    EXPECT_TRUE(store.memory_bytes() >= 128U * sizeof(core::MultiwayBlueprintRow));
    for (std::uint64_t index = 1U; index <= 128U; ++index) {
        const auto* found = store.find({{index}, 0}, 0U, 77U);
        EXPECT_TRUE(found != nullptr);
        EXPECT_EQ(found->infoset.public_state.value, index);
        EXPECT_EQ(found->actions.size(), std::size_t{1U});
    }
}

TEST_CASE(multiway_blueprint_store_rejects_duplicate_and_malformed_rows) {
    EXPECT_THROW(core::MultiwayBlueprintStore(identity(), {row(1U, 0, 0U), row(1U, 0, 0U)}), std::invalid_argument);
    auto malformed = row(2U, 0, 0U);
    malformed.actions[0].probability = 1U;
    EXPECT_THROW(core::MultiwayBlueprintStore(identity(), {malformed}), std::invalid_argument);
}

TEST_CASE(multiway_blueprint_provider_distinguishes_hit_miss_and_menu_mismatch) {
    const auto stored = row(1U, 0, 0U);
    const core::MultiwayBlueprintStore store(identity(), {stored});
    const core::MultiwayBlueprintPolicyProvider provider(store);
    core::Probability probability = 0.0;
    EXPECT_EQ(
        provider.strategy_into({{1U}, 0}, 0U, &stored.actions.front().action, 1U, &probability),
        core::MultiwayBlueprintLookupStatus::Hit);
    EXPECT_NEAR(probability, 1.0, 1e-12);

    EXPECT_EQ(
        provider.strategy_into({{2U}, 0}, 0U, &stored.actions.front().action, 1U, &probability),
        core::MultiwayBlueprintLookupStatus::Missing);
    auto incompatible = stored.actions.front().action;
    incompatible.target_street_contribution = 1;
    EXPECT_EQ(
        provider.strategy_into({{1U}, 0}, 0U, &incompatible, 1U, &probability),
        core::MultiwayBlueprintLookupStatus::IncompatibleMenu);
}
