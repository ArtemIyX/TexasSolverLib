#include "solver/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <array>

#include <stdexcept>

TEST_CASE(multiway_model_identity_is_deterministic_and_versioned) {
    texas::MultiwayBlueprintConfig config;
    const auto first = texas::make_multiway_model_identity(config);
    const auto second = texas::make_multiway_model_identity(config);

    EXPECT_EQ(first, second);
    EXPECT_TRUE(first.combined_hash != 0U);

    ++config.bucket_model_version;
    const auto changed = texas::make_multiway_model_identity(config);
    EXPECT_TRUE(changed != first);
    EXPECT_TRUE(changed.bucket_model_hash != first.bucket_model_hash);
}

TEST_CASE(multiway_model_identity_defaults_to_resolver_schema_v2) {
    const texas::MultiwayBlueprintConfig current_config;
    EXPECT_EQ(current_config.resolver_schema_version, 2U);
    const auto current = texas::make_multiway_model_identity(current_config);

    auto legacy_config = current_config;
    legacy_config.resolver_schema_version = 1U;
    const auto legacy = texas::make_multiway_model_identity(legacy_config);

    EXPECT_TRUE(current.resolver_schema_hash != legacy.resolver_schema_hash);
    EXPECT_TRUE(current.combined_hash != legacy.combined_hash);
}

TEST_CASE(multiway_model_identity_field_visitor_has_stable_component_order) {
    const auto identity = texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{});
    std::array<std::uint64_t, 12U> fields{};
    std::size_t count = 0U;
    texas::visit_multiway_model_identity_components(identity, [&](std::uint64_t field) {
        fields[count++] = field;
    });

    EXPECT_EQ(count, fields.size());
    EXPECT_EQ(fields[0], identity.rules_hash);
    EXPECT_EQ(fields[5], identity.resolver_schema_hash);
    EXPECT_EQ(fields[11], identity.runtime_search_schema_hash);
}

TEST_CASE(multiway_blueprint_config_rejects_invalid_rules_and_versions) {
    texas::MultiwayBlueprintConfig config;
    config.player_count = 7;
    EXPECT_THROW(config.validate(), std::invalid_argument);

    config = {};
    config.flop_bucket_count = 0;
    EXPECT_THROW(config.validate(), std::invalid_argument);

    config = {};
    config.code_schema_version = 0;
    EXPECT_THROW(config.validate(), std::invalid_argument);

    config = {};
    config.continuation_policy = static_cast<texas::MultiwayContinuationPolicyKind>(255U);
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_model_identity_changes_for_every_semantic_configuration_input) {
    const texas::MultiwayBlueprintConfig baseline_config;
    const auto baseline = texas::make_multiway_model_identity(baseline_config);
    const auto expect_changed = [&baseline_config, &baseline](const auto& mutate) {
        auto changed_config = baseline_config;
        mutate(changed_config);
        const auto changed = texas::make_multiway_model_identity(changed_config);
        EXPECT_TRUE(changed != baseline);
        EXPECT_TRUE(changed.combined_hash != baseline.combined_hash);
    };

    expect_changed([](auto& config) { --config.player_count; });
    expect_changed([](auto& config) { ++config.initial_stack_chips; });
    expect_changed([](auto& config) { ++config.small_blind_chips; });
    expect_changed([](auto& config) { ++config.big_blind_chips; });
    expect_changed([](auto& config) { ++config.ante_chips; });
    expect_changed([](auto& config) {
        config.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
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
    expect_changed([](auto& config) {
        config.continuation_policy = texas::MultiwayContinuationPolicyKind::RaiseBiased;
    });
    expect_changed([](auto& config) { ++config.runtime_search_schema_version; });
}
