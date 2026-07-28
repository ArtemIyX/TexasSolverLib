#include "solver/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>

TEST_CASE(multiway_model_identity_is_deterministic_and_versioned) {
    core::MultiwayBlueprintConfig config;
    const auto first = core::make_multiway_model_identity(config);
    const auto second = core::make_multiway_model_identity(config);

    EXPECT_EQ(first, second);
    EXPECT_TRUE(first.combined_hash != 0U);

    ++config.bucket_model_version;
    const auto changed = core::make_multiway_model_identity(config);
    EXPECT_TRUE(changed != first);
    EXPECT_TRUE(changed.bucket_model_hash != first.bucket_model_hash);
}

TEST_CASE(multiway_blueprint_config_rejects_invalid_rules_and_versions) {
    core::MultiwayBlueprintConfig config;
    config.player_count = 7;
    EXPECT_THROW(config.validate(), std::invalid_argument);

    config = {};
    config.flop_bucket_count = 0;
    EXPECT_THROW(config.validate(), std::invalid_argument);

    config = {};
    config.code_schema_version = 0;
    EXPECT_THROW(config.validate(), std::invalid_argument);
}
