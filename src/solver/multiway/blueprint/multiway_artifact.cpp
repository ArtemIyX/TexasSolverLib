#include "solver/multiway/blueprint/multiway_artifact.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace texas::solver::multiway {
namespace {

namespace portable = texas::core::portable;

constexpr std::array<char, 8> kManifestMagic = {'M', 'W', 'M', 'F', '0', '0', '0', '4'};
constexpr std::array<char, 8> kFullBlueprintMagic = {'M', 'W', 'F', 'B', '0', '0', '0', '2'};
constexpr std::uint64_t kFullBlueprintOperatingBytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
using texas::core::fingerprint::append_u64;
using texas::core::fingerprint::finish;

void write_identity(std::ofstream& out, const MultiwayModelIdentity& identity) {
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
        portable::write_u64(out, field);
    });
}

MultiwayModelIdentity read_identity(std::ifstream& in) {
    MultiwayModelIdentity identity;
    bool valid = true;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
        if (valid) valid = portable::read_u64(in, field);
    });
    if (!valid) {
        throw std::runtime_error("multiway artifact is truncated");
    }
    return identity;
}

void write_training(std::ofstream& out, const MultiwayBlueprintTrainingMetadata& training) {
    portable::write_u64(out, training.batches);
    portable::write_u64(out, training.trajectories);
    portable::write_u64(out, training.deterministic_seed);
    portable::write_u64(out, training.late_window_start_batch);
    portable::write_u64(out, training.schedule_hash);
    portable::write_u64(out, training.pruned_negative_regrets);
    portable::write_u8(out, training.linear_iteration_weighting);
    portable::write_u8(out, training.discounting_enabled);
    portable::write_u8(out, training.negative_regret_pruning_enabled);
    portable::write_u8(out, training.reserved);
}

MultiwayBlueprintTrainingMetadata read_training(std::ifstream& in) {
    MultiwayBlueprintTrainingMetadata training;
    if (!portable::read_u64(in, training.batches) || !portable::read_u64(in, training.trajectories) ||
        !portable::read_u64(in, training.deterministic_seed) || !portable::read_u64(in, training.late_window_start_batch) ||
        !portable::read_u64(in, training.schedule_hash) || !portable::read_u64(in, training.pruned_negative_regrets) ||
        !portable::read_u8(in, training.linear_iteration_weighting) ||
        !portable::read_u8(in, training.discounting_enabled) ||
        !portable::read_u8(in, training.negative_regret_pruning_enabled) ||
        !portable::read_u8(in, training.reserved)) {
        throw std::runtime_error("multiway artifact is truncated");
    }
    return training;
}

void write_infoset(std::ofstream& out, const MultiwayInfosetId& infoset) {
    portable::write_u64(out, infoset.public_state.value);
    portable::write_i32(out, infoset.seat);
}

MultiwayInfosetId read_infoset(std::ifstream& in) {
    MultiwayInfosetId infoset;
    if (!portable::read_u64(in, infoset.public_state.value) || !portable::read_i32(in, infoset.seat)) {
        throw std::runtime_error("multiway artifact is truncated");
    }
    return infoset;
}

void write_action(std::ofstream& out, const MultiwayQuantizedRootAction& action) {
    portable::write_u8(out, static_cast<std::uint8_t>(action.action.action));
    portable::write_u32(out, action.action.action_index);
    portable::write_i32(out, action.action.target_street_contribution);
    portable::write_u64(out, action.action.action_menu_id);
    portable::write_u16(out, action.probability);
}

MultiwayQuantizedRootAction read_action(std::ifstream& in) {
    MultiwayQuantizedRootAction action;
    std::uint8_t kind = 0;
    if (!portable::read_u8(in, kind) || !portable::read_u32(in, action.action.action_index) ||
        !portable::read_i32(in, action.action.target_street_contribution) ||
        !portable::read_u64(in, action.action.action_menu_id) || !portable::read_u16(in, action.probability)) {
        throw std::runtime_error("multiway artifact is truncated");
    }
    action.action.action = static_cast<MultiwayAction>(kind);
    return action;
}

std::filesystem::path manifest_path(const std::filesystem::path& checkpoint_path) {
    return checkpoint_path.string() + ".manifest";
}

void save_manifest_atomic(const std::filesystem::path& path, const MultiwayBlueprintManifest& manifest) {
    const auto temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("multiway manifest temporary file cannot be opened");
    out.write(kManifestMagic.data(), static_cast<std::streamsize>(kManifestMagic.size()));
    portable::write_u32(out, manifest.schema_version);
    write_identity(out, manifest.identity);
    portable::write_u64(out, manifest.snapshot_hash);
    out.close();
    if (!out) throw std::runtime_error("multiway manifest write failed");
    texas::core::publish_atomic_replace(temp, path, "multiway manifest publish failed");
}

MultiwayBlueprintManifest load_manifest(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("multiway manifest cannot be opened");
    std::array<char, kManifestMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kManifestMagic) throw std::runtime_error("multiway manifest schema is invalid");
    MultiwayBlueprintManifest manifest;
    if (!portable::read_u32(in, manifest.schema_version)) throw std::runtime_error("multiway manifest is truncated");
    manifest.identity = read_identity(in);
    if (!portable::read_u64(in, manifest.snapshot_hash)) throw std::runtime_error("multiway manifest is truncated");
    if (in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("multiway manifest has trailing data");
    }
    manifest.validate();
    return manifest;
}

void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept {
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
        append_u64(hash, field);
    });
}

void append_action(std::uint64_t& hash, const MultiwayActionDescriptor& action) noexcept {
    append_u64(hash, static_cast<std::uint64_t>(action.action));
    append_u64(hash, action.action_index);
    append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(action.target_street_contribution)));
    append_u64(hash, action.action_menu_id);
}

void append_initial_config(std::uint64_t& hash, const MultiwayGameConfig& config) noexcept {
    append_u64(hash, config.starting_stacks.size());
    for (const auto value : config.starting_stacks) {
        append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }
    append_u64(hash, config.initial_contributions.size());
    for (const auto value : config.initial_contributions) {
        append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }
    append_u64(hash, config.initial_street_contributions.size());
    for (const auto value : config.initial_street_contributions) {
        append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }
    append_u64(hash, static_cast<std::uint64_t>(config.first_player));
    append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(config.big_blind)));
    append_u64(hash, static_cast<std::uint64_t>(config.street));
    append_u64(hash, config.rake_policy.identity());
}

std::uint16_t quantize_probability(double probability) {
    if (!std::isfinite(probability) || probability < 0.0) {
        throw std::invalid_argument("multiway decision log has invalid probability");
    }
    const auto scaled = probability * std::numeric_limits<std::uint16_t>::max();
    if (scaled > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
        throw std::invalid_argument("multiway decision log probability exceeds one");
    }
    return static_cast<std::uint16_t>(std::floor(scaled));
}

std::uint64_t replay_hash(const MultiwayProtectedReplayRecord& record) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, record.schema_version);
    append_identity(hash, record.identity);
    append_u64(hash, record.public_history.schema_version);
    append_u64(hash, record.public_history.hand_seed);
    append_initial_config(hash, record.public_history.initial_config);
    append_u64(hash, record.public_history.events.size());
    for (const auto& event : record.public_history.events) {
        append_u64(hash, static_cast<std::uint64_t>(event.kind));
        append_u64(hash, static_cast<std::uint64_t>(event.decision.acting_seat));
        append_u64(hash, static_cast<std::uint64_t>(event.decision.action));
        append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(event.decision.target_street_contribution)));
        append_u64(hash, event.decision.decision_seed);
        append_u64(hash, static_cast<std::uint64_t>(event.next_street));
        append_u64(hash, static_cast<std::uint64_t>(event.first_player));
    }
    append_u64(hash, record.decision_seeds.size());
    for (const auto seed : record.decision_seeds) append_u64(hash, seed);
    return finish(hash);
}

}  // namespace

void MultiwayFullBlueprintArtifact::validate() const {
    if (schema_version != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION || payload_hash == 0U) {
        throw std::invalid_argument("multiway full blueprint has invalid schema or hash");
    }
    identity.validate();
    const MultiwayBlueprintStore store(identity, rows);
    (void)store;
    if (payload_hash != MultiwayFullBlueprintArtifacts::payload_hash(*this)) {
        throw std::invalid_argument("multiway full blueprint payload hash does not match");
    }
}

std::uint64_t MultiwayFullBlueprintArtifacts::payload_hash(
    const MultiwayFullBlueprintArtifact& artifact) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, artifact.schema_version);
    append_identity(hash, artifact.identity);
    append_u64(hash, artifact.training.batches);
    append_u64(hash, artifact.training.trajectories);
    append_u64(hash, artifact.training.deterministic_seed);
    append_u64(hash, artifact.training.late_window_start_batch);
    append_u64(hash, artifact.training.schedule_hash);
    append_u64(hash, artifact.training.pruned_negative_regrets);
    append_u64(hash, artifact.training.linear_iteration_weighting);
    append_u64(hash, artifact.training.discounting_enabled);
    append_u64(hash, artifact.training.negative_regret_pruning_enabled);
    append_u64(hash, artifact.rows.size());
    for (const auto& row : artifact.rows) {
        append_u64(hash, row.infoset.public_state.value);
        append_u64(hash, static_cast<std::uint64_t>(row.infoset.seat));
        append_u64(hash, row.bucket);
        append_u64(hash, row.action_menu_id);
        append_u64(hash, row.actions.size());
        for (const auto& action : row.actions) {
            append_action(hash, action.action);
            append_u64(hash, action.probability);
        }
    }
    return finish(hash);
}

void MultiwayFullBlueprintArtifacts::save_atomic(
    const std::filesystem::path& path,
    const MultiwayFullBlueprintArtifact& artifact) {
    auto sealed = artifact;
    sealed.payload_hash = payload_hash(sealed);
    sealed.validate();
    const auto temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("multiway full blueprint temporary file cannot be opened");
    out.write(kFullBlueprintMagic.data(), static_cast<std::streamsize>(kFullBlueprintMagic.size()));
    portable::write_u32(out, sealed.schema_version);
    write_identity(out, sealed.identity);
    write_training(out, sealed.training);
    const auto rows = static_cast<std::uint64_t>(sealed.rows.size());
    portable::write_u64(out, rows);
    for (const auto& row : sealed.rows) {
        write_infoset(out, row.infoset);
        portable::write_u32(out, row.bucket);
        portable::write_u64(out, row.action_menu_id);
        const auto actions = static_cast<std::uint32_t>(row.actions.size());
        portable::write_u32(out, actions);
        for (const auto& action : row.actions) write_action(out, action);
    }
    portable::write_u64(out, sealed.payload_hash);
    out.close();
    if (!out) throw std::runtime_error("multiway full blueprint write failed");
    texas::core::publish_atomic_replace(temporary, path, "multiway full blueprint publish failed");
}

MultiwayFullBlueprintArtifact MultiwayFullBlueprintArtifacts::load_verified(
    const std::filesystem::path& path,
    const MultiwayModelIdentity& expected_identity) {
    expected_identity.validate();
    std::error_code file_error;
    const auto file_bytes = std::filesystem::file_size(path, file_error);
    if (file_error) throw std::runtime_error("multiway full blueprint size cannot be determined");
    if (file_bytes > kFullBlueprintOperatingBytes) {
        throw std::length_error("multiway full blueprint exceeds the operating memory guardrail");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("multiway full blueprint cannot be opened");
    std::array<char, kFullBlueprintMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kFullBlueprintMagic) throw std::runtime_error("multiway full blueprint schema is invalid");
    MultiwayFullBlueprintArtifact artifact;
    if (!portable::read_u32(in, artifact.schema_version)) throw std::runtime_error("multiway full blueprint is truncated");
    artifact.identity = read_identity(in);
    artifact.training = read_training(in);
    std::uint64_t row_count = 0;
    if (!portable::read_u64(in, row_count)) throw std::runtime_error("multiway full blueprint is truncated");
    if (row_count > 100000000U) throw std::runtime_error("multiway full blueprint row count is invalid");
    const auto row_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayBlueprintRow));
    const auto action_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayQuantizedRootAction));
    constexpr std::uint64_t max_actions_per_row = 64U;
    const auto action_payload_bytes = action_storage_bytes >
            std::numeric_limits<std::uint64_t>::max() / max_actions_per_row
        ? std::numeric_limits<std::uint64_t>::max()
        : action_storage_bytes * max_actions_per_row;
    const auto estimated_row_bytes = row_storage_bytes >
            std::numeric_limits<std::uint64_t>::max() - action_payload_bytes
        ? std::numeric_limits<std::uint64_t>::max()
        : row_storage_bytes + action_payload_bytes;
    if (estimated_row_bytes == 0U ||
        row_count > (kFullBlueprintOperatingBytes - file_bytes) / estimated_row_bytes) {
        throw std::length_error("multiway full blueprint rows exceed the operating memory guardrail");
    }
    artifact.rows.resize(static_cast<std::size_t>(row_count));
    for (auto& row : artifact.rows) {
        row.infoset = read_infoset(in);
        if (!portable::read_u32(in, row.bucket) || !portable::read_u64(in, row.action_menu_id)) {
            throw std::runtime_error("multiway full blueprint is truncated");
        }
        std::uint32_t action_count = 0;
        if (!portable::read_u32(in, action_count)) throw std::runtime_error("multiway full blueprint is truncated");
        if (action_count == 0U || action_count > 64U) throw std::runtime_error("multiway full blueprint action count is invalid");
        row.actions.resize(action_count);
        for (auto& action : row.actions) action = read_action(in);
    }
    if (!portable::read_u64(in, artifact.payload_hash)) throw std::runtime_error("multiway full blueprint is truncated");
    if (in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("multiway full blueprint has trailing data");
    artifact.validate();
    if (artifact.identity != expected_identity) {
        throw std::invalid_argument("multiway full blueprint identity does not match");
    }
    return artifact;
}

void MultiwayBlueprintManifest::validate() const {
    if (schema_version != MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION || snapshot_hash == 0U) {
        throw std::invalid_argument("multiway blueprint manifest has invalid schema or hash");
    }
    identity.validate();
}

void MultiwayVerifiedBlueprintArtifact::validate(const MultiwayModelIdentity& expected_identity) const {
    snapshot.validate();
    manifest.validate();
    expected_identity.validate();
    if (snapshot.identity != manifest.identity || snapshot.identity != expected_identity ||
        manifest.snapshot_hash != MultiwayBlueprintArtifacts::snapshot_hash(snapshot)) {
        throw std::invalid_argument("multiway blueprint artifact identity or integrity mismatch");
    }
}

std::uint64_t MultiwayBlueprintArtifacts::snapshot_hash(const MultiwayBlueprintSnapshot& snapshot) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_identity(hash, snapshot.identity);
    append_u64(hash, snapshot.public_state.value);
    append_u64(hash, snapshot.infoset.public_state.value);
    append_u64(hash, static_cast<std::uint64_t>(snapshot.infoset.seat));
    append_u64(hash, snapshot.bucket);
    append_u64(hash, snapshot.trajectories);
    append_u64(hash, static_cast<std::uint64_t>(snapshot.policy_kind));
    append_u64(hash, snapshot.training.batches);
    append_u64(hash, snapshot.training.trajectories);
    append_u64(hash, snapshot.training.deterministic_seed);
    append_u64(hash, snapshot.training.late_window_start_batch);
    append_u64(hash, snapshot.training.schedule_hash);
    append_u64(hash, snapshot.training.pruned_negative_regrets);
    append_u64(hash, snapshot.training.linear_iteration_weighting);
    append_u64(hash, snapshot.training.discounting_enabled);
    append_u64(hash, snapshot.training.negative_regret_pruning_enabled);
    append_u64(hash, snapshot.actions.size());
    for (const auto& action : snapshot.actions) {
        append_action(hash, action.action);
        append_u64(hash, action.probability);
    }
    return finish(hash);
}

void MultiwayBlueprintArtifacts::save_atomic(
    const std::filesystem::path& checkpoint_path,
    const MultiwayBlueprintSnapshot& snapshot) {
    snapshot.validate();
    MultiwayBlueprintManifest manifest;
    manifest.identity = snapshot.identity;
    manifest.snapshot_hash = snapshot_hash(snapshot);
    manifest.validate();
    MultiwayRootPolicyArtifact::save_atomic(checkpoint_path, snapshot);
    save_manifest_atomic(manifest_path(checkpoint_path), manifest);
}

MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_verified(
    const std::filesystem::path& checkpoint_path,
    const MultiwayModelIdentity& expected_identity) {
    MultiwayVerifiedBlueprintArtifact artifact;
    artifact.snapshot = MultiwayRootPolicyArtifact::load(checkpoint_path);
    artifact.manifest = load_manifest(manifest_path(checkpoint_path));
    artifact.validate(expected_identity);
    return artifact;
}

MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_with_fallback(
    const std::filesystem::path& primary_checkpoint_path,
    const std::filesystem::path& known_good_checkpoint_path,
    const MultiwayModelIdentity& expected_identity) {
    try {
        return load_verified(primary_checkpoint_path, expected_identity);
    } catch (const std::exception&) {
        auto fallback = load_verified(known_good_checkpoint_path, expected_identity);
        fallback.source = MultiwayArtifactSource::KnownGoodFallback;
        return fallback;
    }
}

void MultiwayPublicDecisionLog::validate() const {
    identity.validate();
    if (schema_version != MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION || public_state_id == 0U ||
        decision_index == 0U || acting_seat < 0 || policy.empty() ||
        sampled_action.action_menu_id == 0U || policy_provenance == MultiwayPolicyProvenance::None ||
        search_engine_version == 0U ||
        (resolver_status != MultiwayResolverStatus::Solved &&
         resolver_status != MultiwayResolverStatus::Partial &&
         resolver_status != MultiwayResolverStatus::DeadlineFallback &&
         resolver_status != MultiwayResolverStatus::ArtifactMismatch &&
         resolver_status != MultiwayResolverStatus::BucketUnavailable &&
         resolver_status != MultiwayResolverStatus::ResourceExhausted &&
         resolver_status != MultiwayResolverStatus::RejectedByBudget)) {
        throw std::invalid_argument("multiway public decision log has invalid metadata");
    }
    const bool runtime_search = policy_provenance == MultiwayPolicyProvenance::RuntimeSearch;
    const bool fallback = policy_provenance == MultiwayPolicyProvenance::StableRootFallback ||
        policy_provenance == MultiwayPolicyProvenance::BlueprintFallback ||
        policy_provenance == MultiwayPolicyProvenance::StaticLegalFallback;
    if ((runtime_search && (search_engine != MultiwayResolverEngine::RootExternalSamplingMCCFR ||
                            search_engine_version != MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION)) ||
        (fallback && (search_engine != MultiwayResolverEngine::NoRuntimeSearch ||
                      search_engine_version != MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION))) {
        throw std::invalid_argument("multiway public decision log has inconsistent provenance");
    }
    std::uint64_t total = 0;
    bool found_sample = false;
    for (std::size_t index = 0; index < policy.size(); ++index) {
        if (policy[index].action.action_index != index || policy[index].action.action_menu_id == 0U) {
            throw std::invalid_argument("multiway public decision log has invalid action order");
        }
        total += policy[index].probability;
        found_sample = found_sample || policy[index].action == sampled_action;
    }
    if (total != std::numeric_limits<std::uint16_t>::max() || !found_sample) {
        throw std::invalid_argument("multiway public decision log has invalid policy");
    }
}

MultiwayPublicDecisionLog make_multiway_public_decision_log(
    const MultiwayResolverRequest& request,
    const MultiwayResolverResult& result,
    std::uint64_t decision_index) {
    request.blueprint_identity.validate();
    if (decision_index == 0U || !result.has_sampled_action || !result.diagnostics.policy_normalized ||
        result.policy.empty()) {
        throw std::invalid_argument("multiway decision log requires a normalized resolver result");
    }
    MultiwayPublicDecisionLog log;
    log.identity = request.blueprint_identity;
    log.public_state_id = result.diagnostics.resolved_public_state_id;
    log.decision_index = decision_index;
    log.acting_seat = request.hero_seat;
    log.sampled_action = result.sampled_action;
    log.resolver_status = result.diagnostics.status;
    log.policy_provenance = result.diagnostics.policy_provenance;
    log.search_engine = result.diagnostics.search_engine;
    log.search_engine_version = result.diagnostics.search_engine_version;
    log.used_fallback = result.diagnostics.used_fallback;
    log.policy.reserve(result.policy.size());
    std::uint32_t assigned = 0;
    for (std::size_t index = 0; index < result.policy.size(); ++index) {
        auto probability = index + 1U == result.policy.size()
            ? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
            : quantize_probability(result.policy[index].probability);
        assigned += probability;
        log.policy.push_back({result.policy[index].action, probability});
    }
    log.validate();
    return log;
}

MultiwayProtectedReplayRecord MultiwayProtectedReplayRecord::from_history(
    const MultiwayModelIdentity& record_identity,
    const MultiwayHandHistory& history) {
    record_identity.validate();
    history.validate();
    MultiwayProtectedReplayRecord record;
    record.identity = record_identity;
    record.public_history = history;
    record.decision_seeds.reserve(history.events.size());
    for (const auto& event : history.events) {
        if (event.kind == MultiwayReplayEventKind::Decision) {
            record.decision_seeds.push_back(event.decision.decision_seed);
        }
    }
    record.seal();
    record.validate();
    return record;
}

void MultiwayProtectedReplayRecord::seal() noexcept { integrity_hash = replay_hash(*this); }

void MultiwayProtectedReplayRecord::validate() const {
    if (schema_version != MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION || integrity_hash == 0U) {
        throw std::invalid_argument("multiway protected replay has invalid schema or integrity hash");
    }
    identity.validate();
    public_history.validate();
    std::size_t decision_index = 0;
    for (const auto& event : public_history.events) {
        if (event.kind == MultiwayReplayEventKind::Decision) {
            if (decision_index >= decision_seeds.size() ||
                decision_seeds[decision_index] != event.decision.decision_seed) {
                throw std::invalid_argument("multiway protected replay seed record is inconsistent");
            }
            ++decision_index;
        }
    }
    if (decision_index != decision_seeds.size() || integrity_hash != replay_hash(*this)) {
        throw std::invalid_argument("multiway protected replay integrity check failed");
    }
}

}  // namespace texas::solver::multiway
