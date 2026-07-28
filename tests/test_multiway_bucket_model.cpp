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
