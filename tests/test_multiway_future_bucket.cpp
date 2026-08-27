#include "solver/multiway_future_bucket.hpp"
#include "solver/multiway_future_bucket_calibration.hpp"
#include "core/canonical_combo.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_resolver.hpp"
#include "test_harness.hpp"

#include <memory>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

texas::MultiwayModelIdentity identity() {
    return texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{});
}

std::vector<texas::MultiwayBucketBoardRequest> boards() {
    return {{texas::Street::Flop, {0U, 5U, 10U}}, {texas::Street::Turn, {0U, 5U, 10U, 15U}}};
}

texas::MultiwayFutureBucketProfile profile() {
    texas::MultiwayFutureBucketProfile result;
    result.flop_bucket_count = 2U;
    result.turn_bucket_count = 2U;
    result.river_bucket_count = 2U;
    return result;
}

}  // namespace

std::filesystem::path future_bucket_path() {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_future_bucket_") +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
}

TEST_CASE(multiway_future_bucket_features_are_deterministic_and_reject_blocked_holes) {
    const auto first = texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U});
    const auto second = texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U});
    EXPECT_EQ(first.feature_version, second.feature_version);
    EXPECT_EQ(first.values, second.values);
    EXPECT_THROW(
        texas::make_multiway_future_bucket_features(texas::Street::Flop, {0U, 5U, 10U}, {0U, 2U}),
        std::invalid_argument);
}

TEST_CASE(multiway_future_bucket_artifact_producer_is_deterministic_and_round_trips) {
    const auto first = texas::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    const auto second = texas::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    const auto bytes = texas::serialize_multiway_future_bucket_artifact(first);
    const auto restored = texas::deserialize_multiway_future_bucket_artifact(bytes);

    EXPECT_EQ(bytes, texas::serialize_multiway_future_bucket_artifact(second));
    EXPECT_EQ(restored.profile().feature_version, first.profile().feature_version);
    EXPECT_EQ(restored.lookup(texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}),
              first.lookup(texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}));
}

TEST_CASE(multiway_future_bucket_artifact_rejects_invalid_bucket_metadata) {
    auto bytes = texas::serialize_multiway_future_bucket_artifact(
        texas::build_multiway_future_bucket_artifact(identity(), boards(), profile()));
    bytes[4] = static_cast<std::uint8_t>(texas::MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION + 1U);
    EXPECT_THROW(texas::deserialize_multiway_future_bucket_artifact(bytes), std::invalid_argument);
}

TEST_CASE(multiway_future_bucket_producer_uses_canonical_combo_indices) {
    const auto artifact = texas::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    const auto& table = artifact.registry().table(texas::Street::Flop, {0U, 5U, 10U});
    EXPECT_EQ(table.assignments().size(), texas::MULTIWAY_HOLE_COMBINATION_COUNT);
    const auto combo = texas::canonical_combos().id({1U, 2U});
    EXPECT_TRUE(table.assignments()[combo] < table.bucket_count());
}

TEST_CASE(multiway_future_bucket_artifact_can_be_injected_into_resolver_config) {
    auto artifact = std::make_shared<texas::MultiwayFutureBucketArtifact>(
        texas::build_multiway_future_bucket_artifact(identity(), boards(), profile()));
    texas::MultiwayResolverConfig config;
    config.future_bucket_artifact = std::move(artifact);
    const texas::MultiwayResolver resolver(config);
    (void)resolver;
    EXPECT_TRUE(true);
}

TEST_CASE(multiway_future_bucket_artifact_round_trips_through_atomic_disk_boundary) {
    const auto path = future_bucket_path();
    const auto expected = texas::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    texas::save_multiway_future_bucket_artifact_atomic(path, expected);
    const auto loaded = texas::load_multiway_future_bucket_artifact(path, identity());
    EXPECT_EQ(loaded.registry().identity(), expected.registry().identity());
    EXPECT_EQ(loaded.lookup(texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}),
              expected.lookup(texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}));
    std::filesystem::remove(path);
}

TEST_CASE(multiway_future_bucket_artifact_disk_boundary_rejects_truncation_and_identity_mismatch) {
    const auto path = future_bucket_path();
    const auto expected = texas::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    texas::save_multiway_future_bucket_artifact_atomic(path, expected);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.put('M');
    }
    EXPECT_THROW(texas::load_multiway_future_bucket_artifact(path, identity()), std::invalid_argument);
    texas::save_multiway_future_bucket_artifact_atomic(path, expected);
    auto different = texas::MultiwayBlueprintConfig{};
    ++different.bucket_model_version;
    EXPECT_THROW(
        texas::load_multiway_future_bucket_artifact(path, texas::make_multiway_model_identity(different)),
        std::invalid_argument);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_future_bucket_calibration_splits_and_gates_samples) {
    const std::vector<texas::MultiwayFutureBucketCalibrationSample> samples = {
        {{texas::Street::Flop, {0U, 5U, 10U}}, {1U, 2U}, 0.1, false},
        {{texas::Street::Flop, {0U, 5U, 10U}}, {3U, 4U}, 0.15, false},
        {{texas::Street::Turn, {0U, 5U, 10U, 15U}}, {1U, 2U}, 0.2, false}};
    const auto split = texas::split_multiway_future_bucket_samples(samples, 7U, 0.5);
    EXPECT_EQ(split.size(), samples.size());
    const auto result = texas::calibrate_multiway_future_bucket_profile(
        profile(), identity(), split);
    EXPECT_EQ(result.samples, samples.size());
    EXPECT_TRUE(result.artifact_bytes > 0U);
    EXPECT_EQ(result.missing_buckets, std::size_t{0U});
}

TEST_CASE(multiway_future_bucket_calibration_selects_smallest_passing_profile) {
    const std::vector<texas::MultiwayFutureBucketCalibrationSample> samples = {
        {{texas::Street::Flop, {0U, 5U, 10U}}, {1U, 2U}, 0.1, false}};
    auto small = profile();
    small.flop_bucket_count = small.turn_bucket_count = small.river_bucket_count = 1U;
    const auto selection = texas::select_smallest_multiway_future_bucket_profile(
        {profile(), small}, identity(), samples);
    EXPECT_TRUE(selection.selected);
    EXPECT_EQ(selection.profile.flop_bucket_count, 1U);
}
