#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

std::vector<std::uint32_t> assignments_for(const std::vector<std::uint8_t>& board) {
    std::vector<std::uint32_t> assignments(texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            for (const auto card : board) {
                if (card == first || card == second) {
                    assignments[texas::solver::multiway::MultiwayBucketTable::hole_index(hole)] = texas::solver::multiway::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

}  // namespace

TEST_CASE(multiway_bucket_table_maps_live_holes_and_rejects_blocked_holes) {
    const std::vector<std::uint8_t> board = {0, 1, 2};
    texas::solver::multiway::MultiwayBlueprintConfig config;
    const auto table = texas::solver::multiway::MultiwayBucketTable(
        texas::solver::multiway::make_multiway_model_identity(config), texas::core::Street::Flop, board, 1,
        assignments_for(board));

    EXPECT_EQ(table.lookup({3, 4}), 0U);
    EXPECT_THROW(table.lookup({0, 4}), std::invalid_argument);
    EXPECT_EQ(
        texas::solver::multiway::MultiwayBucketTable::hole_index({3, 4}),
        texas::solver::multiway::MultiwayBucketTable::hole_index({4, 3}));
}

TEST_CASE(multiway_bucket_indices_match_canonical_combo_indices) {
    const std::vector<std::uint8_t> board = {0U, 1U, 2U};
    texas::solver::multiway::MultiwayBlueprintConfig config;
    const texas::solver::multiway::MultiwayBucketTable table(
        texas::solver::multiway::make_multiway_model_identity(config), texas::core::Street::Flop, board, 1U,
        assignments_for(board));
    const texas::solver::multiway::MultiwayBucketRegistry registry({table});

    EXPECT_EQ(table.lookup({3U, 4U}), table.lookup({4U, 3U}));
    EXPECT_EQ(registry.table(texas::core::Street::Flop, board).canonical_board(), board);
    EXPECT_EQ(registry.lookup(texas::core::Street::Flop, board, {3U, 4U}), table.lookup({3U, 4U}));
    EXPECT_THROW(table.lookup({0U, 4U}), std::invalid_argument);

    const auto& combos = texas::core::canonical_combos();
    for (std::size_t raw_id = 0U; raw_id < texas::core::CANONICAL_HOLE_COMBINATION_COUNT; ++raw_id) {
        const auto hole = combos.cards(static_cast<texas::core::CanonicalComboId>(raw_id));
        EXPECT_EQ(
            static_cast<std::size_t>(raw_id),
            texas::solver::multiway::MultiwayBucketTable::hole_index(hole));
    }
}
