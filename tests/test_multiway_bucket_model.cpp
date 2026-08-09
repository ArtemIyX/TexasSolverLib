#include "solver/multiway_bucket_model.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

std::vector<std::uint32_t> assignments_for(const std::vector<std::uint8_t>& board) {
    std::vector<std::uint32_t> assignments(core::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            for (const auto card : board) {
                if (card == first || card == second) {
                    assignments[core::MultiwayBucketTable::hole_index(hole)] = core::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

}  // namespace

TEST_CASE(multiway_bucket_table_maps_live_holes_and_rejects_blocked_holes) {
    const std::vector<std::uint8_t> board = {0, 1, 2};
    core::MultiwayBlueprintConfig config;
    const auto table = core::MultiwayBucketTable(
        core::make_multiway_model_identity(config), core::Street::Flop, board, 1,
        assignments_for(board));

    EXPECT_EQ(table.lookup({3, 4}), 0U);
    EXPECT_THROW(table.lookup({0, 4}), std::invalid_argument);
    EXPECT_EQ(
        core::MultiwayBucketTable::hole_index({3, 4}),
        core::MultiwayBucketTable::hole_index({4, 3}));
}

TEST_CASE(multiway_bucket_hunl_adapter_matches_compact_artifact_indices) {
    const std::vector<std::uint8_t> compact_board = {0U, 1U, 2U};
    core::MultiwayBlueprintConfig config;
    const core::MultiwayBucketTable table(
        core::make_multiway_model_identity(config), core::Street::Flop, compact_board, 1U,
        assignments_for(compact_board));
    const core::MultiwayBucketRegistry registry({table});
    const std::vector<std::uint8_t> hunl_board = {8U, 9U, 10U};

    EXPECT_EQ(table.lookup({3U, 4U}), table.lookup_hunl({11U, 12U}));
    EXPECT_EQ(table.lookup_hunl({11U, 12U}), table.lookup_hunl({12U, 11U}));
    EXPECT_EQ(registry.table_hunl(core::Street::Flop, hunl_board).canonical_board(), compact_board);
    EXPECT_EQ(
        registry.lookup_hunl(core::Street::Flop, hunl_board, {11U, 12U}),
        registry.lookup(core::Street::Flop, compact_board, {3U, 4U}));
    EXPECT_THROW(table.lookup_hunl({8U, 12U}), std::invalid_argument);
    EXPECT_THROW(table.lookup_hunl({0U, 12U}), std::invalid_argument);
    EXPECT_THROW(registry.table_hunl(core::Street::Flop, compact_board), std::invalid_argument);

    const auto& combos = core::canonical_combos();
    for (std::size_t raw_id = 0U; raw_id < core::CANONICAL_HOLE_COMBINATION_COUNT; ++raw_id) {
        const auto hunl_hole = combos.cards(static_cast<core::CanonicalComboId>(raw_id));
        const std::array<std::uint8_t, 2> compact_hole = {
            static_cast<std::uint8_t>(hunl_hole[0] - core::HUNL_CARD_FIRST),
            static_cast<std::uint8_t>(hunl_hole[1] - core::HUNL_CARD_FIRST),
        };
        EXPECT_EQ(
            core::MultiwayBucketTable::hole_index_hunl(hunl_hole),
            core::MultiwayBucketTable::hole_index(compact_hole));
    }
}
