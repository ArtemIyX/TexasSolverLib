#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_blueprint_store.hpp"
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
