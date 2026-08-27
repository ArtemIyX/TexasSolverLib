#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl/bucket/hunl_bucket_map.hpp"
#include "solver/hunl/flat/hunl_flat_state.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <array>
#include <filesystem>
#include <memory>

namespace {

TEST_CASE(hunl_solver_storage_infoset_table_uses_bucket_map_bucket_counts) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_solver_storage_bucket_map_counts.npz", std::nullopt, std::nullopt,
        config->initial_board,
        [](texas::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 3U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 3}, texas::ABSTRACTION_SCHEMA_VERSION,
                                                "river-3", std::nullopt});
    const auto graph = texas::HUNLFlatSolveGraph::build(config);
    const auto map = texas::HUNLFlatBucketMap::from_abstraction(graph, texas::load_abstraction(path));
    const auto table = texas::HUNLFlatInfosetTable::build(
        graph, {99, 99}, &map, texas::HUNLFlatValueLayout::InfosetHandAction);
    for (const auto& infoset : graph.infosets) {
        if (infoset.street == texas::Street::River) {
            const auto& meta = table.meta()[infoset.id.value];
            EXPECT_EQ(meta.bucket_count, 3U);
            EXPECT_EQ(meta.value_count, 3U * static_cast<std::uint32_t>(infoset.action_count));
        }
    }
    std::filesystem::remove(path);
}

}  // namespace
