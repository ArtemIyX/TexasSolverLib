#include "solver/multiway_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

core::MultiwayBlueprintSnapshot snapshot() {
    core::MultiwayBlueprintConfig config;
    core::MultiwayBlueprintSnapshot result;
    result.identity = core::make_multiway_model_identity(config);
    result.public_state = {71U};
    result.infoset = {{71U}, 0};
    result.trajectories = 12U;
    result.training.trajectories = 12U;
    result.training.deterministic_seed = 9U;
    result.actions = {{{core::MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
    result.validate();
    return result;
}

std::filesystem::path artifact_path(const char* name) {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_") + name + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
}

}  // namespace

TEST_CASE(multiway_artifact_rejects_checkpoint_integrity_mismatch) {
    const auto expected = snapshot();
    const auto path = artifact_path("integrity");
    core::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.put('\0');
    }
    EXPECT_THROW(core::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_rejects_mismatched_identity) {
    const auto expected = snapshot();
    const auto path = artifact_path("identity");
    core::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    auto different_config = core::MultiwayBlueprintConfig{};
    ++different_config.resolver_schema_version;
    EXPECT_THROW(
        core::MultiwayBlueprintArtifacts::load_verified(path, core::make_multiway_model_identity(different_config)),
        std::invalid_argument);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_selects_known_good_fallback) {
    const auto expected = snapshot();
    const auto primary = artifact_path("primary");
    const auto fallback = artifact_path("fallback");
    core::MultiwayBlueprintArtifacts::save_atomic(fallback, expected);

    const auto loaded = core::MultiwayBlueprintArtifacts::load_with_fallback(
        primary, fallback, expected.identity);
    EXPECT_EQ(loaded.source, core::MultiwayArtifactSource::KnownGoodFallback);
    EXPECT_EQ(loaded.snapshot.identity, expected.identity);
    std::filesystem::remove(fallback);
    std::filesystem::remove(fallback.string() + ".manifest");
}

TEST_CASE(multiway_public_decision_log_redacts_private_resolver_inputs) {
    const auto expected = snapshot();
    core::MultiwayResolverRequest request;
    request.blueprint_identity = expected.identity;
    request.hero_seat = 2;
    request.hero_cards = {12U, 31U};
    request.opponent_ranges = {{1, {{{4U, 5U}, 1.0}}}};

    core::MultiwayResolverResult result;
    result.sampled_action = {core::MultiwayAction::Check, 0U, 0, 17U};
    result.has_sampled_action = true;
    result.policy = {{result.sampled_action, 1.0}};
    result.diagnostics.status = core::MultiwayResolverStatus::Solved;
    result.diagnostics.policy_normalized = true;
    result.diagnostics.resolved_public_state_id = 71U;
    const auto log = core::make_multiway_public_decision_log(request, result, 1U);

    log.validate();
    EXPECT_EQ(log.acting_seat, 2);
    EXPECT_EQ(log.policy.size(), 1U);
    EXPECT_EQ(log.policy.front().probability, 65535U);

    auto malformed = log;
    malformed.policy.front().probability = 0U;
    EXPECT_THROW(malformed.validate(), std::invalid_argument);
}

TEST_CASE(multiway_protected_replay_record_is_deterministic_and_sealed) {
    const auto expected = snapshot();
    auto history = core::MultiwayHandHistory::from_rules(core::MultiwayGameRules::standard_6max(), 0, 44U);
    history.events.push_back({
        core::MultiwayReplayEventKind::Decision,
        {0, core::MultiwayAction::Fold, 0, 93U},
        core::Street::Flop,
        0});

    const auto first = core::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
    const auto second = core::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
    EXPECT_EQ(first.integrity_hash, second.integrity_hash);
    EXPECT_EQ(first.decision_seeds.front(), 93U);

    auto altered = first;
    altered.decision_seeds.front() = 94U;
    EXPECT_THROW(altered.validate(), std::invalid_argument);
}

TEST_CASE(multiway_artifact_fails_closed_for_malformed_manifest) {
    const auto expected = snapshot();
    const auto path = artifact_path("malformed_manifest");
    core::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    {
        std::ofstream out(path.string() + ".manifest", std::ios::binary | std::ios::trunc);
        out.write("broken", 6);
    }
    EXPECT_THROW(core::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}
