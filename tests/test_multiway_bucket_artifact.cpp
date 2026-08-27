#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <vector>

namespace {

texas::solver::multiway::MultiwayModelIdentity identity() {
    texas::solver::multiway::MultiwayBlueprintConfig config;
    return texas::solver::multiway::make_multiway_model_identity(config);
}

std::vector<texas::solver::multiway::MultiwayBucketBoardRequest> boards() {
    return {
        {texas::core::Street::Flop, {0U, 5U, 10U}},
        {texas::core::Street::Turn, {0U, 5U, 10U, 15U}},
        {texas::core::Street::River, {0U, 5U, 10U, 15U, 20U}},
    };
}

}  // namespace

TEST_CASE(multiway_bucket_artifact_builds_canonical_coverage_with_baseline_counts) {
    const auto profile = texas::solver::multiway::MultiwayBucketBaselineProfile::standard();
    const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards(), profile);

    texas::solver::multiway::validate_multiway_bucket_coverage(registry, boards());
    EXPECT_EQ(registry.table(texas::core::Street::Flop, boards()[0].canonical_board).bucket_count(), 96U);
    EXPECT_EQ(registry.table(texas::core::Street::Turn, boards()[1].canonical_board).bucket_count(), 128U);
    EXPECT_EQ(registry.table(texas::core::Street::River, boards()[2].canonical_board).bucket_count(), 192U);
}

TEST_CASE(multiway_bucket_artifact_assignments_are_stable_for_the_same_inputs) {
    const auto first = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
    const auto second = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
    const std::array<std::uint8_t, 2> hole = {1U, 2U};

    EXPECT_EQ(first.lookup(texas::core::Street::Flop, boards()[0].canonical_board, hole),
              second.lookup(texas::core::Street::Flop, boards()[0].canonical_board, hole));
    EXPECT_EQ(texas::solver::multiway::serialize_multiway_bucket_registry(first),
              texas::solver::multiway::serialize_multiway_bucket_registry(second));
}

TEST_CASE(multiway_bucket_artifact_round_trips_and_rejects_corruption) {
    const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
    auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
    const auto restored = texas::solver::multiway::deserialize_multiway_bucket_registry(bytes);

    EXPECT_EQ(restored.identity(), registry.identity());
    EXPECT_EQ(restored.lookup(texas::core::Street::River, boards()[2].canonical_board, {1U, 2U}),
              registry.lookup(texas::core::Street::River, boards()[2].canonical_board, {1U, 2U}));

    bytes.pop_back();
    EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_rejects_missing_board_and_legacy_schema) {
    const std::vector<texas::solver::multiway::MultiwayBucketBoardRequest> flop_only = {
        {texas::core::Street::Flop, {0U, 5U, 10U}},
    };
    const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), flop_only);
    EXPECT_THROW(texas::solver::multiway::validate_multiway_bucket_coverage(registry, boards()), std::out_of_range);

    auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
    bytes[4] = 2U;
    EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_requires_sorted_canonical_boards) {
    const std::vector<std::uint8_t> noncanonical = {10U, 0U, 5U};
    EXPECT_TRUE(!texas::solver::multiway::is_multiway_canonical_board(texas::core::Street::Flop, noncanonical));
    EXPECT_THROW(texas::solver::multiway::build_multiway_baseline_bucket_table(
                     identity(), texas::core::Street::Flop, noncanonical,
                     texas::solver::multiway::MultiwayBucketBaselineProfile::standard()),
                 std::invalid_argument);
}
