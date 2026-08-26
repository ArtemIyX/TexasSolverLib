#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

texas::MultiwayBlueprintRow row(std::uint64_t state, texas::PlayerId seat, std::uint32_t bucket) {
    texas::MultiwayBlueprintRow result;
    result.infoset = {{state}, seat};
    result.bucket = bucket;
    result.action_menu_id = 77U;
    result.actions = {{{texas::MultiwayAction::Check, 0U, 0, 77U}, 65535U}};
    return result;
}

texas::MultiwayModelIdentity identity() {
    return texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{});
}

}  // namespace

TEST_CASE(multiway_blueprint_store_finds_128_sorted_rows) {
    std::vector<texas::MultiwayBlueprintRow> rows;
    rows.reserve(128U);
    for (std::uint64_t index = 128U; index > 0U; --index) rows.push_back(row(index, 0, 0U));
    const texas::MultiwayBlueprintStore store(identity(), std::move(rows));

    EXPECT_EQ(store.row_count(), std::size_t{128U});
    EXPECT_TRUE(store.memory_bytes() >= 128U * sizeof(texas::MultiwayBlueprintRowView));
    for (std::uint64_t index = 1U; index <= 128U; ++index) {
        const auto found = store.find({{index}, 0}, 0U, 77U);
        EXPECT_TRUE(found.valid());
        EXPECT_EQ(found.infoset.public_state.value, index);
        EXPECT_EQ(found.action_count, std::size_t{1U});
    }
}

TEST_CASE(multiway_blueprint_store_flattens_runtime_actions) {
    auto first = row(1U, 0, 0U);
    auto second = row(2U, 0, 0U);
    second.actions.push_back({{texas::MultiwayAction::Fold, 1U, 0, 77U}, 1U});
    second.actions[0].probability = 65534U;
    const texas::MultiwayBlueprintStore store(identity(), {first, second});

    const auto found = store.find({{2U}, 0}, 0U, 77U);
    EXPECT_TRUE(found.valid());
    EXPECT_EQ(found.action_count, std::size_t{2U});
    EXPECT_EQ(found.actions[0].probability, std::uint16_t{65534U});
    EXPECT_EQ(found.actions[1].probability, std::uint16_t{1U});

    const auto first_view = store.find({{1U}, 0}, 0U, 77U);
    EXPECT_TRUE(first_view.valid());
    EXPECT_TRUE(first_view.actions + first_view.action_count == found.actions);
}

TEST_CASE(multiway_blueprint_store_rejects_duplicate_and_malformed_rows) {
    EXPECT_THROW(texas::MultiwayBlueprintStore(identity(), {row(1U, 0, 0U), row(1U, 0, 0U)}), std::invalid_argument);
    auto malformed = row(2U, 0, 0U);
    malformed.actions[0].probability = 1U;
    EXPECT_THROW(texas::MultiwayBlueprintStore(identity(), {malformed}), std::invalid_argument);
}

TEST_CASE(multiway_blueprint_provider_distinguishes_hit_miss_and_menu_mismatch) {
    const auto stored = row(1U, 0, 0U);
    const texas::MultiwayBlueprintStore store(identity(), {stored});
    const texas::MultiwayBlueprintPolicyProvider provider(store);
    texas::Probability probability = 0.0;
    EXPECT_EQ(
        provider.strategy_into({{1U}, 0}, 0U, &stored.actions.front().action, 1U, &probability),
        texas::MultiwayBlueprintLookupStatus::Hit);
    EXPECT_NEAR(probability, 1.0, 1e-12);

    EXPECT_EQ(
        provider.strategy_into({{2U}, 0}, 0U, &stored.actions.front().action, 1U, &probability),
        texas::MultiwayBlueprintLookupStatus::Missing);
    auto incompatible = stored.actions.front().action;
    incompatible.target_street_contribution = 1;
    EXPECT_EQ(
        provider.strategy_into({{1U}, 0}, 0U, &incompatible, 1U, &probability),
        texas::MultiwayBlueprintLookupStatus::IncompatibleMenu);
}
