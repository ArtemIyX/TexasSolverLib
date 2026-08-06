#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <vector>

namespace {

core::MultiwayModelIdentity identity() {
    core::MultiwayBlueprintConfig config;
    return core::make_multiway_model_identity(config);
}

std::vector<core::MultiwayBucketBoardRequest> boards() {
    return {
        {core::Street::Flop, {0U, 5U, 10U}},
        {core::Street::Turn, {0U, 5U, 10U, 15U}},
        {core::Street::River, {0U, 5U, 10U, 15U, 20U}},
    };
}

}  // namespace

TEST_CASE(multiway_bucket_artifact_builds_canonical_coverage_with_baseline_counts) {
    const auto profile = core::MultiwayBucketBaselineProfile::standard();
    const auto registry = core::build_multiway_baseline_bucket_registry(identity(), boards(), profile);

    core::validate_multiway_bucket_coverage(registry, boards());
    EXPECT_EQ(registry.table(core::Street::Flop, boards()[0].canonical_board).bucket_count(), 96U);
    EXPECT_EQ(registry.table(core::Street::Turn, boards()[1].canonical_board).bucket_count(), 128U);
    EXPECT_EQ(registry.table(core::Street::River, boards()[2].canonical_board).bucket_count(), 192U);
}

TEST_CASE(multiway_bucket_artifact_assignments_are_stable_for_the_same_inputs) {
    const auto first = core::build_multiway_baseline_bucket_registry(identity(), boards());
    const auto second = core::build_multiway_baseline_bucket_registry(identity(), boards());
    const std::array<std::uint8_t, 2> hole = {1U, 2U};

    EXPECT_EQ(first.lookup(core::Street::Flop, boards()[0].canonical_board, hole),
              second.lookup(core::Street::Flop, boards()[0].canonical_board, hole));
    EXPECT_EQ(core::serialize_multiway_bucket_registry(first),
              core::serialize_multiway_bucket_registry(second));
}

TEST_CASE(multiway_bucket_artifact_round_trips_and_rejects_corruption) {
    const auto registry = core::build_multiway_baseline_bucket_registry(identity(), boards());
    auto bytes = core::serialize_multiway_bucket_registry(registry);
    const auto restored = core::deserialize_multiway_bucket_registry(bytes);

    EXPECT_EQ(restored.identity(), registry.identity());
    EXPECT_EQ(restored.lookup(core::Street::River, boards()[2].canonical_board, {1U, 2U}),
              registry.lookup(core::Street::River, boards()[2].canonical_board, {1U, 2U}));

    bytes.pop_back();
    EXPECT_THROW(core::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_rejects_missing_board_and_schema_version) {
    const std::vector<core::MultiwayBucketBoardRequest> flop_only = {
        {core::Street::Flop, {0U, 5U, 10U}},
    };
    const auto registry = core::build_multiway_baseline_bucket_registry(identity(), flop_only);
    EXPECT_THROW(core::validate_multiway_bucket_coverage(registry, boards()), std::out_of_range);

    auto bytes = core::serialize_multiway_bucket_registry(registry);
    bytes[4] = static_cast<std::uint8_t>(core::MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION + 1U);
    EXPECT_THROW(core::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_requires_sorted_canonical_boards) {
    const std::vector<std::uint8_t> noncanonical = {10U, 0U, 5U};
    EXPECT_TRUE(!core::is_multiway_canonical_board(core::Street::Flop, noncanonical));
    EXPECT_THROW(core::build_multiway_baseline_bucket_table(
                     identity(), core::Street::Flop, noncanonical,
                     core::MultiwayBucketBaselineProfile::standard()),
                 std::invalid_argument);
}
