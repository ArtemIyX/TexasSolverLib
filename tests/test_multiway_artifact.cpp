#include "core/atomic_publish.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

texas::MultiwayBlueprintSnapshot snapshot() {
    texas::MultiwayBlueprintConfig config;
    texas::MultiwayBlueprintSnapshot result;
    result.identity = texas::make_multiway_model_identity(config);
    result.public_state = {71U};
    result.infoset = {{71U}, 0};
    result.trajectories = 12U;
    result.training.trajectories = 12U;
    result.training.deterministic_seed = 9U;
    result.actions = {{{texas::MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
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
    texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.put('\0');
    }
    EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_full_blueprint_artifact_round_trips_and_rejects_corruption) {
    const auto expected = snapshot();
    const auto path = artifact_path("full_blueprint");
    texas::MultiwayFullBlueprintArtifact artifact;
    artifact.identity = expected.identity;
    artifact.training = expected.training;
    artifact.rows = {{{{31U}, 0}, 0U, 17U, {{{texas::MultiwayAction::Check, 0U, 0, 17U}, 65535U}}}};
    texas::MultiwayFullBlueprintArtifacts::save_atomic(path, artifact);
    const auto loaded = texas::MultiwayFullBlueprintArtifacts::load_verified(path, expected.identity);
    EXPECT_EQ(loaded.rows.size(), std::size_t{1U});
    EXPECT_EQ(loaded.rows.front().infoset.public_state.value, 31U);

    {
        std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(-1, std::ios::end);
        out.put('\0');
    }
    EXPECT_THROW(texas::MultiwayFullBlueprintArtifacts::load_verified(path, expected.identity), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_artifact_rejects_mismatched_identity) {
    const auto expected = snapshot();
    const auto path = artifact_path("identity");
    texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    auto different_config = texas::MultiwayBlueprintConfig{};
    ++different_config.resolver_schema_version;
    EXPECT_THROW(
        texas::MultiwayBlueprintArtifacts::load_verified(path, texas::make_multiway_model_identity(different_config)),
        std::invalid_argument);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_manifest_rejects_each_semantic_identity_mutation) {
    const auto expected = snapshot();
    const auto path = artifact_path("all_identity_inputs");
    texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    const auto expect_rejected = [&path](const auto& mutate) {
        texas::MultiwayBlueprintConfig config;
        mutate(config);
        EXPECT_THROW(
            texas::MultiwayBlueprintArtifacts::load_verified(
                path, texas::make_multiway_model_identity(config)),
            std::invalid_argument);
    };

    expect_rejected([](auto& config) { --config.player_count; });
    expect_rejected([](auto& config) { ++config.initial_stack_chips; });
    expect_rejected([](auto& config) { ++config.small_blind_chips; });
    expect_rejected([](auto& config) { ++config.big_blind_chips; });
    expect_rejected([](auto& config) { ++config.ante_chips; });
    expect_rejected([](auto& config) {
        config.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
        config.rake_policy.basis_points = 100U;
        config.rake_policy.cap = 100;
    });
    expect_rejected([](auto& config) { ++config.flop_bucket_count; });
    expect_rejected([](auto& config) { ++config.turn_bucket_count; });
    expect_rejected([](auto& config) { ++config.river_bucket_count; });
    expect_rejected([](auto& config) { ++config.action_abstraction_version; });
    expect_rejected([](auto& config) { ++config.bucket_model_version; });
    expect_rejected([](auto& config) { ++config.terminal_model_version; });
    expect_rejected([](auto& config) { ++config.rules_profile_version; });
    expect_rejected([](auto& config) { ++config.resolver_schema_version; });
    expect_rejected([](auto& config) { ++config.code_schema_version; });
    expect_rejected([](auto& config) { ++config.range_semantics_version; });
    expect_rejected([](auto& config) { ++config.future_bucket_model_version; });
    expect_rejected([](auto& config) { ++config.off_tree_policy_version; });
    expect_rejected([](auto& config) { ++config.continuation_policy_version; });
    expect_rejected([](auto& config) { ++config.runtime_search_schema_version; });
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_selects_known_good_fallback) {
    const auto expected = snapshot();
    const auto primary = artifact_path("primary");
    const auto fallback = artifact_path("fallback");
    texas::MultiwayBlueprintArtifacts::save_atomic(fallback, expected);

    const auto loaded = texas::MultiwayBlueprintArtifacts::load_with_fallback(
        primary, fallback, expected.identity);
    EXPECT_EQ(loaded.source, texas::MultiwayArtifactSource::KnownGoodFallback);
    EXPECT_EQ(loaded.snapshot.identity, expected.identity);
    std::filesystem::remove(fallback);
    std::filesystem::remove(fallback.string() + ".manifest");
}

TEST_CASE(multiway_public_decision_log_redacts_private_resolver_inputs) {
    const auto expected = snapshot();
    texas::MultiwayResolverRequest request;
    request.blueprint_identity = expected.identity;
    request.hero_seat = 2;
    request.hero_cards = {12U, 31U};
    request.opponent_ranges = {{1, {{{4U, 5U}, 1.0}}}};

    texas::MultiwayResolverResult result;
    result.sampled_action = {texas::MultiwayAction::Check, 0U, 0, 17U};
    result.has_sampled_action = true;
    result.policy = {{result.sampled_action, 1.0}};
    result.diagnostics.status = texas::MultiwayResolverStatus::Solved;
    result.diagnostics.policy_provenance = texas::MultiwayPolicyProvenance::RuntimeSearch;
    result.diagnostics.search_engine_version = texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION;
    result.diagnostics.policy_normalized = true;
    result.diagnostics.resolved_public_state_id = 71U;
    const auto log = texas::make_multiway_public_decision_log(request, result, 1U);

    log.validate();
    EXPECT_EQ(log.acting_seat, 2);
    EXPECT_EQ(log.policy.size(), 1U);
    EXPECT_EQ(log.policy.front().probability, 65535U);
    EXPECT_EQ(
        log.policy_provenance,
        texas::MultiwayPolicyProvenance::RuntimeSearch);
    EXPECT_EQ(
        log.search_engine,
        texas::MultiwayResolverEngine::RootExternalSamplingMCCFR);
    EXPECT_EQ(log.search_engine_version, texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION);

    auto malformed = log;
    malformed.policy.front().probability = 0U;
    EXPECT_THROW(malformed.validate(), std::invalid_argument);

    auto no_provenance = result;
    no_provenance.diagnostics.policy_provenance = texas::MultiwayPolicyProvenance::None;
    EXPECT_THROW(
        texas::make_multiway_public_decision_log(request, no_provenance, 1U),
        std::invalid_argument);
}

TEST_CASE(multiway_public_decision_log_accepts_forward_compatible_delivery_statuses) {
    const auto expected = snapshot();
    texas::MultiwayPublicDecisionLog log;
    log.identity = expected.identity;
    log.public_state_id = expected.public_state.value;
    log.decision_index = 1U;
    log.acting_seat = 0;
    log.sampled_action = expected.actions.front().action;
    log.policy_provenance = texas::MultiwayPolicyProvenance::StaticLegalFallback;
    log.search_engine = texas::MultiwayResolverEngine::RootExternalSamplingMCCFR;
    log.search_engine_version = texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION;
    log.used_fallback = true;
    log.policy = {{log.sampled_action, 65535U}};

    const texas::MultiwayResolverStatus accepted_statuses[] = {
        texas::MultiwayResolverStatus::Solved,
        texas::MultiwayResolverStatus::Partial,
        texas::MultiwayResolverStatus::DeadlineFallback,
        texas::MultiwayResolverStatus::ArtifactMismatch,
        texas::MultiwayResolverStatus::BucketUnavailable,
        texas::MultiwayResolverStatus::ResourceExhausted,
        texas::MultiwayResolverStatus::RejectedByBudget,
    };
    for (const auto status : accepted_statuses) {
        log.resolver_status = status;
        log.validate();
    }

    log.resolver_status = texas::MultiwayResolverStatus::InvalidRequest;
    EXPECT_THROW(log.validate(), std::invalid_argument);
}

TEST_CASE(multiway_protected_replay_record_is_deterministic_and_sealed) {
    const auto expected = snapshot();
    auto history = texas::MultiwayHandHistory::from_rules(texas::MultiwayGameRules::standard_6max(), 0, 44U);
    history.events.push_back({
        texas::MultiwayReplayEventKind::Decision,
        {0, texas::MultiwayAction::Fold, 0, 93U},
        texas::Street::Flop,
        0});

    const auto first = texas::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
    const auto second = texas::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
    EXPECT_EQ(first.integrity_hash, second.integrity_hash);
    EXPECT_EQ(first.decision_seeds.front(), 93U);

    auto altered = first;
    altered.decision_seeds.front() = 94U;
    EXPECT_THROW(altered.validate(), std::invalid_argument);
}

TEST_CASE(multiway_artifact_fails_closed_for_malformed_manifest) {
    const auto expected = snapshot();
    const auto path = artifact_path("malformed_manifest");
    texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    {
        std::ofstream out(path.string() + ".manifest", std::ios::binary | std::ios::trunc);
        out.write("broken", 6);
    }
    EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_atomic_publish_replaces_destination_without_leaving_temporary_files) {
    const auto destination = artifact_path("atomic_destination");
    const auto temporary = artifact_path("atomic_temporary");
    {
        std::ofstream old_value(destination, std::ios::binary);
        old_value << "old";
        std::ofstream new_value(temporary, std::ios::binary);
        new_value << "new";
    }

    texas::core::publish_atomic_replace(temporary, destination, "atomic publish failed");

    std::ifstream published(destination, std::ios::binary);
    std::string value;
    published >> value;
    EXPECT_EQ(value, "new");
    EXPECT_TRUE(!std::filesystem::exists(temporary));
    EXPECT_TRUE(!std::filesystem::exists(destination.string() + ".previous"));
    std::filesystem::remove(destination);

    {
        std::ofstream existing(destination, std::ios::binary);
        existing << "preserved";
    }
    EXPECT_THROW(
        texas::core::publish_atomic_replace(temporary, destination, "atomic publish failed"),
        std::runtime_error);
    std::ifstream restored(destination, std::ios::binary);
    restored >> value;
    EXPECT_EQ(value, "preserved");
    EXPECT_TRUE(!std::filesystem::exists(destination.string() + ".previous"));
    std::filesystem::remove(destination);
}

TEST_CASE(multiway_artifact_rejects_schema_v2_manifest_magic) {
    const auto expected = snapshot();
    const auto path = artifact_path("schema_v2_manifest");
    const auto manifest = path.string() + ".manifest";
    texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
    {
        std::ifstream in(manifest, std::ios::binary);
        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        EXPECT_TRUE(static_cast<bool>(in));
        EXPECT_EQ(magic[7], '3');
    }
    {
        std::fstream out(manifest, std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(7);
        out.put('2');
    }
    EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(manifest);
}
