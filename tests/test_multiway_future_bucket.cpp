#include "solver/multiway_future_bucket.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>

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
