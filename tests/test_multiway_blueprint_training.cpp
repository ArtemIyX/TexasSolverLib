#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <cstddef>
#include <utility>
#include <limits>

TEST_CASE(multiway_training_config_binds_default_rules_and_artifacts) {
    texas::MultiwayBlueprintTrainingConfig config;
    config.validate();
    EXPECT_EQ(config.max_decision_depth, texas::MULTIWAY_MAX_DECISION_DEPTH);
    EXPECT_EQ(config.max_public_chance_depth, texas::MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH);
    const auto identity = config.identity();
    EXPECT_TRUE(identity.combined_hash != 0U);

    config.action_abstraction.first_bet_basis_points[0] = 3400U;
    EXPECT_TRUE(config.identity() != identity);

    config.blueprint.turn_bucket_count = 127U;
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_identity_binds_storage_backend) {
    texas::MultiwayBlueprintTrainingConfig reference;
    auto compact = reference;
    compact.limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
    EXPECT_TRUE(reference.identity() != compact.identity());
}

TEST_CASE(multiway_initial_blueprint_root_is_six_player_preflop_state) {
    texas::MultiwayPrivateConfig ranges;
    ranges.ranges.resize(6U);
    for (std::size_t seat = 0; seat < 6U; ++seat) {
        ranges.ranges[seat] = {{{texas::card_to_int(static_cast<std::uint8_t>(14U - seat), 0),
            texas::card_to_int(static_cast<std::uint8_t>(13U - seat), 1)}, 1.0}};
    }
    texas::MultiwayActionAbstraction abstraction;
    const auto root = texas::make_multiway_initial_blueprint_root(
        texas::MultiwayGameRules::standard_6max(), std::move(ranges), abstraction, 1U, 1U);
    EXPECT_EQ(root.public_state.betting.street, texas::Street::Preflop);
    EXPECT_EQ(root.seat_order.size(), std::size_t{6});
    EXPECT_EQ(root.root_infoset.seat, root.public_state.betting.current_player);
    EXPECT_TRUE(!root.public_state.legal_actions.empty());
}

TEST_CASE(multiway_training_schedule_controls_linear_weighting_and_pruning) {
    texas::MultiwayBlueprintIterationSchedule schedule;
    EXPECT_EQ(schedule.strategy_weight(1U), 1.0);
    EXPECT_EQ(schedule.strategy_weight(3U), 3.0);

    schedule.linear_iteration_weighting = false;
    EXPECT_EQ(schedule.strategy_weight(3U), 1.0);
    schedule.prune_negative_regrets = true;
    schedule.pruning_warmup_batches = 4U;
    schedule.validate();
    EXPECT_TRUE(schedule.identity() != 0U);
}

TEST_CASE(multiway_training_schedule_rejects_invalid_pruning_controls) {
    texas::MultiwayBlueprintIterationSchedule schedule;
    schedule.pruning_threshold = 0.1;
    EXPECT_THROW(schedule.validate(), std::invalid_argument);
    schedule.pruning_threshold = 0.0;
    schedule.pruning_exploration_probability = 1.1;
    EXPECT_THROW(schedule.validate(), std::invalid_argument);
    schedule.pruning_exploration_probability = 0.05;
    schedule.pruning_action_probability_threshold = 0.25;
    schedule.validate();
}

TEST_CASE(multiway_traversal_pruning_policy_covers_warmup_recovery_and_exemptions) {
    texas::MultiwayTraversalPruningConfig policy;
    policy.enabled = true;
    policy.warmup_batches = 4U;
    policy.recovery_interval_batches = 20U;
    EXPECT_TRUE(!policy.should_explore_action(4U, true, false, false));
    EXPECT_TRUE(policy.should_explore_action(5U, true, false, false));
    EXPECT_TRUE(!policy.should_explore_action(5U, false, false, false));
    EXPECT_TRUE(policy.recovery_batch(20U));
    EXPECT_TRUE(policy.should_explore_action(20U, false, false, false));
    EXPECT_TRUE(!policy.should_explore_action(21U, true, true, false));
    EXPECT_TRUE(!policy.should_explore_action(21U, true, false, true));
}

TEST_CASE(multiway_checkpoint_resume_identity_rejects_different_artifacts) {
    texas::MultiwayBlueprintTrainingConfig config;
    const auto expected = config.identity();
    auto snapshot = texas::MultiwayBlueprintSnapshot{};
    snapshot.identity = expected;
    snapshot.public_state = {7U};
    snapshot.infoset = {{7U}, 0};
    snapshot.bucket = 0U;
    snapshot.trajectories = 3U;
    snapshot.training.trajectories = 3U;
    snapshot.actions = {{{texas::MultiwayAction::Check, 0U, 0, 11U}, 65535U}};

    texas::MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, expected);
    ++config.blueprint.bucket_model_version;
    EXPECT_THROW(
        texas::MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, config.identity()),
        std::invalid_argument);
}

TEST_CASE(multiway_full_checkpoint_rejects_mismatched_sparse_value_shapes) {
    texas::MultiwayBlueprintTrainingCheckpoint checkpoint;
    texas::MultiwayBlueprintConfig config;
    checkpoint.identity = texas::make_multiway_model_identity(config);
    checkpoint.coordinator.storage.shapes.push_back({{{1U}, 0}, 2U, 2U});
    checkpoint.coordinator.storage.regrets.resize(3U);
    checkpoint.coordinator.storage.strategy_sums.resize(3U);
    EXPECT_THROW(checkpoint.validate(), std::invalid_argument);
}

TEST_CASE(multiway_full_checkpoint_rejects_non_finite_accumulators) {
    texas::MultiwayBlueprintTrainingCheckpoint checkpoint;
    texas::MultiwayBlueprintConfig config;
    checkpoint.identity = texas::make_multiway_model_identity(config);
    checkpoint.coordinator.storage.shapes.push_back({{{1U}, 0}, 1U, 1U});
    checkpoint.coordinator.storage.regrets.push_back(std::numeric_limits<double>::quiet_NaN());
    checkpoint.coordinator.storage.strategy_sums.push_back(0.0);
    EXPECT_THROW(checkpoint.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_metadata_persists_policy_variant_contract) {
    texas::MultiwayBlueprintTrainingMetadata metadata;
    metadata.trajectories = 11U;
    metadata.batches = 2U;
    metadata.deterministic_seed = 9U;
    metadata.schedule_hash = 1U;

    texas::MultiwayBlueprintTrainingConfig config;
    texas::MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = config.identity();
    snapshot.public_state = {3U};
    snapshot.infoset = {{3U}, 0};
    snapshot.trajectories = metadata.trajectories;
    snapshot.policy_kind = texas::MultiwayBlueprintPolicyKind::LateWindowAverage;
    snapshot.training = metadata;
    snapshot.actions = {{{texas::MultiwayAction::Check, 0U, 0, 5U}, 65535U}};
    snapshot.validate();
    EXPECT_EQ(snapshot.policy_kind, texas::MultiwayBlueprintPolicyKind::LateWindowAverage);
}
