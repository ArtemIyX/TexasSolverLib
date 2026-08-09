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

TEST_CASE(multiway_model_identity_changes_for_every_semantic_configuration_input) {
    const core::MultiwayBlueprintConfig baseline_config;
    const auto baseline = core::make_multiway_model_identity(baseline_config);
    const auto expect_changed = [&baseline_config, &baseline](const auto& mutate) {
        auto changed_config = baseline_config;
        mutate(changed_config);
        const auto changed = core::make_multiway_model_identity(changed_config);
        EXPECT_TRUE(changed != baseline);
        EXPECT_TRUE(changed.combined_hash != baseline.combined_hash);
    };

    expect_changed([](auto& config) { --config.player_count; });
    expect_changed([](auto& config) { ++config.initial_stack_chips; });
    expect_changed([](auto& config) { ++config.small_blind_chips; });
    expect_changed([](auto& config) { ++config.big_blind_chips; });
    expect_changed([](auto& config) { ++config.ante_chips; });
    expect_changed([](auto& config) {
        config.rake_policy.mode = core::MultiwayRakeMode::PercentageOfContestedPot;
        config.rake_policy.basis_points = 100U;
        config.rake_policy.cap = 100;
    });
    expect_changed([](auto& config) { ++config.flop_bucket_count; });
    expect_changed([](auto& config) { ++config.turn_bucket_count; });
    expect_changed([](auto& config) { ++config.river_bucket_count; });
    expect_changed([](auto& config) { ++config.action_abstraction_version; });
    expect_changed([](auto& config) { ++config.bucket_model_version; });
    expect_changed([](auto& config) { ++config.terminal_model_version; });
    expect_changed([](auto& config) { ++config.rules_profile_version; });
    expect_changed([](auto& config) { ++config.resolver_schema_version; });
    expect_changed([](auto& config) { ++config.code_schema_version; });
    expect_changed([](auto& config) { ++config.range_semantics_version; });
    expect_changed([](auto& config) { ++config.future_bucket_model_version; });
    expect_changed([](auto& config) { ++config.off_tree_policy_version; });
    expect_changed([](auto& config) { ++config.continuation_policy_version; });
    expect_changed([](auto& config) { ++config.runtime_search_schema_version; });
}
