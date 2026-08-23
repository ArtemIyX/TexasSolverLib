#include "games/multiway_rules.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_resolver.hpp"
#include "test_harness.hpp"

#include <stdexcept>

TEST_CASE(multiway_standard_rules_match_the_six_max_playbook_profile) {
    const auto rules = texas::MultiwayGameRules::standard_6max();

    EXPECT_EQ(rules.profile_version, 1U);
    EXPECT_EQ(rules.player_count, 6U);
    EXPECT_EQ(rules.initial_stack_chips, 10'000);
    EXPECT_EQ(rules.small_blind_chips, 50);
    EXPECT_EQ(rules.big_blind_chips, 100);
    EXPECT_EQ(rules.ante_chips, 0);
    EXPECT_EQ(rules.straddle_chips, 0);
    EXPECT_EQ(rules.rake_policy.identity(), texas::MultiwayRakePolicy::explicit_zero().identity());
    EXPECT_TRUE(!rules.rebuys_enabled);

    const auto game = rules.make_initial_game_config(0);
    EXPECT_EQ(game.starting_stacks.size(), 6U);
    EXPECT_EQ(game.big_blind, 100);
}

TEST_CASE(multiway_rules_reject_unsupported_forced_money_or_rebuys) {
    auto rules = texas::MultiwayGameRules::standard_6max();
    rules.ante_chips = 1;
    EXPECT_THROW(rules.validate(), std::invalid_argument);

    rules = texas::MultiwayGameRules::standard_6max();
    rules.straddle_chips = 200;
    EXPECT_THROW(rules.validate(), std::invalid_argument);

    rules = texas::MultiwayGameRules::standard_6max();
    rules.rebuys_enabled = true;
    EXPECT_THROW(rules.validate(), std::invalid_argument);
}

TEST_CASE(multiway_identity_binds_rules_and_resolver_schemas) {
    texas::MultiwayBlueprintConfig config;
    const auto identity = texas::make_multiway_model_identity(config);

    ++config.rules_profile_version;
    const auto changed_rules = texas::make_multiway_model_identity(config);
    EXPECT_TRUE(changed_rules != identity);
    EXPECT_TRUE(changed_rules.rules_schema_hash != identity.rules_schema_hash);

    config = {};
    ++config.resolver_schema_version;
    const auto changed_resolver = texas::make_multiway_model_identity(config);
    EXPECT_TRUE(changed_resolver != identity);
    EXPECT_TRUE(changed_resolver.resolver_schema_hash != identity.resolver_schema_hash);
}

TEST_CASE(multiway_resolver_contract_defaults_to_a_single_sampling_seed) {
    texas::MultiwayResolverRequest request;
    EXPECT_EQ(request.sampling_seed, 1U);
    EXPECT_TRUE(!texas::MultiwayResolverResult{}.has_sampled_action);
}
