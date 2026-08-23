#include "solver/multiway_blueprint_trainer.hpp"
#include "solver/multiway_checkpoint.hpp"
#include "test_harness.hpp"

#include <stdexcept>

TEST_CASE(multiway_training_config_binds_default_rules_and_artifacts) {
    texas::MultiwayBlueprintTrainingConfig config;
    config.validate();
    const auto identity = config.identity();
    EXPECT_TRUE(identity.combined_hash != 0U);

    config.blueprint.turn_bucket_count = 127U;
    EXPECT_THROW(config.validate(), std::invalid_argument);
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
