#include "solver/multiway_future_bucket.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

core::MultiwayModelIdentity identity() {
    return core::make_multiway_model_identity(core::MultiwayBlueprintConfig{});
}

std::vector<core::MultiwayBucketBoardRequest> boards() {
    return {{core::Street::Flop, {0U, 5U, 10U}}, {core::Street::Turn, {0U, 5U, 10U, 15U}}};
}

core::MultiwayFutureBucketProfile profile() {
    core::MultiwayFutureBucketProfile result;
    result.flop_bucket_count = 2U;
    result.turn_bucket_count = 2U;
    result.river_bucket_count = 2U;
    return result;
}

}  // namespace

TEST_CASE(multiway_future_bucket_features_are_deterministic_and_reject_blocked_holes) {
    const auto first = core::make_multiway_future_bucket_features(
        core::Street::Flop, {0U, 5U, 10U}, {1U, 2U});
    const auto second = core::make_multiway_future_bucket_features(
        core::Street::Flop, {0U, 5U, 10U}, {1U, 2U});
    EXPECT_EQ(first.feature_version, second.feature_version);
    EXPECT_EQ(first.values, second.values);
    EXPECT_THROW(
        core::make_multiway_future_bucket_features(core::Street::Flop, {0U, 5U, 10U}, {0U, 2U}),
        std::invalid_argument);
}

TEST_CASE(multiway_future_bucket_artifact_producer_is_deterministic_and_round_trips) {
    const auto first = core::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    const auto second = core::build_multiway_future_bucket_artifact(identity(), boards(), profile());
    const auto bytes = core::serialize_multiway_future_bucket_artifact(first);
    const auto restored = core::deserialize_multiway_future_bucket_artifact(bytes);

    EXPECT_EQ(bytes, core::serialize_multiway_future_bucket_artifact(second));
    EXPECT_EQ(restored.profile().feature_version, first.profile().feature_version);
    EXPECT_EQ(restored.lookup_hunl(core::Street::Flop, {8U, 13U, 18U}, {9U, 10U}),
              first.lookup_hunl(core::Street::Flop, {8U, 13U, 18U}, {9U, 10U}));
}

TEST_CASE(multiway_future_bucket_artifact_rejects_invalid_bucket_metadata) {
    auto bytes = core::serialize_multiway_future_bucket_artifact(
        core::build_multiway_future_bucket_artifact(identity(), boards(), profile()));
    bytes[4] = static_cast<std::uint8_t>(core::MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION + 1U);
    EXPECT_THROW(core::deserialize_multiway_future_bucket_artifact(bytes), std::invalid_argument);
}
