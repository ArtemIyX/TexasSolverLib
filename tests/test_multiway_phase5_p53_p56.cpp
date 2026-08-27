#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <vector>

namespace {

texas::MultiwayState expansion_state() {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {10'000, 10'000, 10'000};
    config.initial_contributions = {100, 100, 100};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = texas::Street::Flop;
    return texas::MultiwayState::initial(config);
}

texas::MultiwayActionAbstraction expansion_abstraction() {
    texas::MultiwayActionAbstractionConfig config;
    config.translation_max_pseudo_harmonic_distance_basis_points = 1U;
    return texas::MultiwayActionAbstraction(config);
}

texas::MultiwayDeviationExpansionConfig expansion_config() {
    texas::MultiwayDeviationExpansionConfig config;
    config.minimum_pseudo_harmonic_distance_basis_points = 1U;
    return config;
}

texas::MultiwayModelIdentity future_identity() {
    return texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{});
}

texas::MultiwayFutureBucketProfile small_profile() {
    texas::MultiwayFutureBucketProfile profile;
    profile.lloyd_iterations = 1U;
    profile.flop_bucket_count = 2U;
    profile.turn_bucket_count = 2U;
    profile.river_bucket_count = 2U;
    return profile;
}

std::vector<texas::MultiwayBucketBoardRequest> flop_boards() {
    return {{texas::Street::Flop, {0U, 5U, 10U}}};
}

struct FutureFeatureFixture {
    texas::Street street;
    std::vector<std::uint8_t> board;
    std::array<std::uint8_t, 2> hole;
};

FutureFeatureFixture future_feature_fixture(std::uint8_t index) {
    static const std::array<FutureFeatureFixture, 15U> fixtures = {{
        {texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {3U, 4U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {6U, 7U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {11U, 12U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {20U, 21U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {30U, 31U}},
        {texas::Street::Flop, {0U, 5U, 10U}, {40U, 41U}},
        {texas::Street::Turn, {0U, 5U, 10U, 15U}, {1U, 2U}},
        {texas::Street::Turn, {0U, 5U, 10U, 15U}, {3U, 4U}},
        {texas::Street::Turn, {0U, 5U, 10U, 15U}, {6U, 7U}},
        {texas::Street::Turn, {0U, 5U, 10U, 15U}, {20U, 21U}},
        {texas::Street::River, {0U, 5U, 10U, 15U, 20U}, {1U, 2U}},
        {texas::Street::River, {0U, 5U, 10U, 15U, 20U}, {3U, 4U}},
        {texas::Street::River, {0U, 5U, 10U, 15U, 20U}, {6U, 7U}},
        {texas::Street::River, {0U, 5U, 10U, 15U, 20U}, {30U, 31U}},
    }};
    return fixtures[index];
}

}  // namespace

#define P53_EXPANSION_CASE(label, target) \
    TEST_CASE(multiway_phase5_p53_expands_##label) { \
        const auto state = expansion_state(); \
        const auto abstraction = expansion_abstraction(); \
        const auto menu = abstraction.make_legal_actions(state.snapshot()); \
        EXPECT_EQ(abstraction.classify_observed_action( \
            state.snapshot(), menu, texas::MultiwayAction::Bet, target, expansion_config()), \
            texas::MultiwayDeviationDisposition::Expand); \
    }

P53_EXPANSION_CASE(target_500, 500)
P53_EXPANSION_CASE(target_600, 600)
P53_EXPANSION_CASE(target_700, 700)
P53_EXPANSION_CASE(target_800, 800)
P53_EXPANSION_CASE(target_900, 900)
P53_EXPANSION_CASE(target_1000, 1000)
P53_EXPANSION_CASE(target_1500, 1500)
P53_EXPANSION_CASE(target_2000, 2000)
P53_EXPANSION_CASE(target_2500, 2500)
P53_EXPANSION_CASE(target_3000, 3000)
P53_EXPANSION_CASE(target_4000, 4000)
P53_EXPANSION_CASE(target_5000, 5000)
P53_EXPANSION_CASE(target_6000, 6000)

#undef P53_EXPANSION_CASE

TEST_CASE(multiway_phase5_p53_exact_menu_action_translates) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    const auto menu = abstraction.make_legal_actions(state.snapshot());
    const auto exact = menu.front();
    EXPECT_EQ(abstraction.classify_observed_action(
        state.snapshot(), menu, exact.action, exact.target_street_contribution, expansion_config()),
        texas::MultiwayDeviationDisposition::Translate);
}

TEST_CASE(multiway_phase5_p53_rejects_expansion_below_active_seat_limit) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    auto config = expansion_config();
    config.minimum_active_seats = 4U;
    EXPECT_EQ(abstraction.classify_observed_action(
        state.snapshot(), abstraction.make_legal_actions(state.snapshot()),
        texas::MultiwayAction::Bet, 2500, config), texas::MultiwayDeviationDisposition::Translate);
}

TEST_CASE(multiway_phase5_p53_rejects_expansion_for_disabled_flop) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    auto config = expansion_config();
    config.enabled_postflop_street_mask = 0x02U;
    EXPECT_EQ(abstraction.classify_observed_action(
        state.snapshot(), abstraction.make_legal_actions(state.snapshot()),
        texas::MultiwayAction::Bet, 2500, config), texas::MultiwayDeviationDisposition::Translate);
}

TEST_CASE(multiway_phase5_p53_rejects_expansion_at_menu_capacity) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    const auto menu = abstraction.make_legal_actions(state.snapshot());
    auto config = expansion_config();
    config.maximum_menu_actions = static_cast<std::uint8_t>(menu.size());
    EXPECT_EQ(abstraction.classify_observed_action(
        state.snapshot(), menu, texas::MultiwayAction::Bet, 2500, config),
        texas::MultiwayDeviationDisposition::Translate);
}

TEST_CASE(multiway_phase5_p53_non_aggressive_action_translates) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    EXPECT_EQ(abstraction.classify_observed_action(
        state.snapshot(), abstraction.make_legal_actions(state.snapshot()),
        texas::MultiwayAction::Check, 0, expansion_config()), texas::MultiwayDeviationDisposition::Translate);
}

TEST_CASE(multiway_phase5_p53_invalid_target_is_rejected) {
    const auto state = expansion_state();
    const auto abstraction = expansion_abstraction();
    EXPECT_THROW(abstraction.classify_observed_action(
        state.snapshot(), abstraction.make_legal_actions(state.snapshot()),
        texas::MultiwayAction::Bet, -1, expansion_config()), std::invalid_argument);
}

#define P53_INVALID_CONFIG_CASE(label, mutation) \
    TEST_CASE(multiway_phase5_p53_invalid_config_##label) { \
        auto config = expansion_config(); mutation; \
        EXPECT_THROW(config.validate(), std::invalid_argument); \
    }

P53_INVALID_CONFIG_CASE(zero_policy_version, config.policy_version = 0U)
P53_INVALID_CONFIG_CASE(excess_distance, config.minimum_pseudo_harmonic_distance_basis_points = 20001U)
P53_INVALID_CONFIG_CASE(single_seat, config.minimum_active_seats = 1U)
P53_INVALID_CONFIG_CASE(seven_seats, config.minimum_active_seats = 7U)
P53_INVALID_CONFIG_CASE(empty_street_mask, config.enabled_postflop_street_mask = 0U)
P53_INVALID_CONFIG_CASE(zero_menu_capacity, config.maximum_menu_actions = 0U)
P53_INVALID_CONFIG_CASE(excess_menu_capacity, config.maximum_menu_actions = 9U)

#undef P53_INVALID_CONFIG_CASE

#define P56_FEATURE_CASE(label, index) \
    TEST_CASE(multiway_phase5_p56_feature_determinism_##label) { \
        const auto fixture = future_feature_fixture(index); \
        const auto first = texas::make_multiway_future_bucket_features( \
            fixture.street, fixture.board, fixture.hole); \
        const auto second = texas::make_multiway_future_bucket_features( \
            fixture.street, fixture.board, fixture.hole); \
        EXPECT_EQ(first.feature_version, second.feature_version); \
        EXPECT_EQ(first.values, second.values); \
    }

P56_FEATURE_CASE(flop_01, 0U)
P56_FEATURE_CASE(flop_02, 1U)
P56_FEATURE_CASE(flop_03, 2U)
P56_FEATURE_CASE(flop_04, 3U)
P56_FEATURE_CASE(flop_05, 4U)
P56_FEATURE_CASE(flop_06, 5U)
P56_FEATURE_CASE(flop_07, 6U)
P56_FEATURE_CASE(turn_01, 7U)
P56_FEATURE_CASE(turn_02, 8U)
P56_FEATURE_CASE(turn_03, 9U)
P56_FEATURE_CASE(turn_04, 10U)
P56_FEATURE_CASE(river_01, 11U)
P56_FEATURE_CASE(river_02, 12U)
P56_FEATURE_CASE(river_03, 13U)
P56_FEATURE_CASE(river_04, 14U)

#undef P56_FEATURE_CASE

TEST_CASE(multiway_phase5_p56_feature_rejects_zero_version) {
    EXPECT_THROW(texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}, 0U), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_feature_rejects_blocked_first_card) {
    EXPECT_THROW(texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {0U, 2U}), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_feature_rejects_blocked_second_card) {
    EXPECT_THROW(texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {1U, 5U}), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_feature_rejects_duplicate_hole) {
    EXPECT_THROW(texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {0U, 5U, 10U}, {1U, 1U}), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_feature_rejects_noncanonical_board) {
    EXPECT_THROW(texas::make_multiway_future_bucket_features(
        texas::Street::Flop, {10U, 0U, 5U}, {1U, 2U}), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_profile_selects_flop_count) {
    EXPECT_EQ(small_profile().bucket_count(texas::Street::Flop), 2U);
}

TEST_CASE(multiway_phase5_p56_profile_selects_turn_count) {
    EXPECT_EQ(small_profile().bucket_count(texas::Street::Turn), 2U);
}

TEST_CASE(multiway_phase5_p56_profile_selects_river_count) {
    EXPECT_EQ(small_profile().bucket_count(texas::Street::River), 2U);
}

#define P56_INVALID_PROFILE_CASE(label, mutation) \
    TEST_CASE(multiway_phase5_p56_invalid_profile_##label) { \
        auto profile = small_profile(); mutation; \
        EXPECT_THROW(profile.validate(), std::invalid_argument); \
    }

P56_INVALID_PROFILE_CASE(zero_feature_version, profile.feature_version = 0U)
P56_INVALID_PROFILE_CASE(zero_clustering_version, profile.clustering_version = 0U)
P56_INVALID_PROFILE_CASE(zero_seed, profile.seed = 0U)
P56_INVALID_PROFILE_CASE(zero_iterations, profile.lloyd_iterations = 0U)
P56_INVALID_PROFILE_CASE(zero_flop_count, profile.flop_bucket_count = 0U)
P56_INVALID_PROFILE_CASE(zero_turn_count, profile.turn_bucket_count = 0U)
P56_INVALID_PROFILE_CASE(zero_river_count, profile.river_bucket_count = 0U)

#undef P56_INVALID_PROFILE_CASE

TEST_CASE(multiway_phase5_p56_artifact_producer_is_repeatable) {
    const auto first = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    const auto second = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    EXPECT_EQ(texas::serialize_multiway_future_bucket_artifact(first),
              texas::serialize_multiway_future_bucket_artifact(second));
}

TEST_CASE(multiway_phase5_p56_artifact_uses_configured_flop_count) {
    const auto artifact = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    EXPECT_EQ(artifact.registry().tables().front().bucket_count(), 2U);
}

TEST_CASE(multiway_phase5_p56_artifact_lookup_is_in_range) {
    const auto artifact = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    EXPECT_TRUE(artifact.lookup(texas::Street::Flop, {0U, 5U, 10U}, {1U, 2U}) < 2U);
}

TEST_CASE(multiway_phase5_p56_artifact_round_trips_profile) {
    const auto artifact = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    const auto restored = texas::deserialize_multiway_future_bucket_artifact(
        texas::serialize_multiway_future_bucket_artifact(artifact));
    EXPECT_EQ(restored.profile().seed, artifact.profile().seed);
}

TEST_CASE(multiway_phase5_p56_artifact_round_trips_identity) {
    const auto artifact = texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile());
    const auto restored = texas::deserialize_multiway_future_bucket_artifact(
        texas::serialize_multiway_future_bucket_artifact(artifact));
    EXPECT_EQ(restored.registry().identity(), artifact.registry().identity());
}

TEST_CASE(multiway_phase5_p56_artifact_rejects_empty_board_set) {
    EXPECT_THROW(texas::build_multiway_future_bucket_artifact(
        future_identity(), {}, small_profile()), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_artifact_rejects_noncanonical_board) {
    EXPECT_THROW(texas::build_multiway_future_bucket_artifact(
        future_identity(), {{texas::Street::Flop, {10U, 0U, 5U}}}, small_profile()), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_artifact_rejects_corrupt_magic) {
    auto bytes = texas::serialize_multiway_future_bucket_artifact(
        texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile()));
    bytes[0] = 0U;
    EXPECT_THROW(texas::deserialize_multiway_future_bucket_artifact(bytes), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_artifact_rejects_corrupt_schema) {
    auto bytes = texas::serialize_multiway_future_bucket_artifact(
        texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile()));
    bytes[4] = static_cast<std::uint8_t>(texas::MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION + 1U);
    EXPECT_THROW(texas::deserialize_multiway_future_bucket_artifact(bytes), std::invalid_argument);
}

TEST_CASE(multiway_phase5_p56_artifact_rejects_truncated_payload) {
    auto bytes = texas::serialize_multiway_future_bucket_artifact(
        texas::build_multiway_future_bucket_artifact(future_identity(), flop_boards(), small_profile()));
    bytes.resize(47U);
    EXPECT_THROW(texas::deserialize_multiway_future_bucket_artifact(bytes), std::invalid_argument);
}
