#include "solver/multiway_artifact.hpp"

#include "solver/multiway_checkpoint.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace texas::solver::multiway {
namespace {

constexpr std::array<char, 8> kManifestMagic = {'M', 'W', 'M', 'F', '0', '0', '0', '3'};
constexpr std::array<char, 8> kFullBlueprintMagic = {'M', 'W', 'F', 'B', '0', '0', '0', '1'};
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kFullBlueprintOperatingBytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

std::uint64_t finish(std::uint64_t hash) noexcept { return hash == 0U ? 1U : hash; }

template <class T>
void write_value(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!out) throw std::runtime_error("multiway manifest write failed");
}

template <class T>
T read_value(std::ifstream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in) throw std::runtime_error("multiway manifest is truncated");
    return value;
}

std::filesystem::path manifest_path(const std::filesystem::path& checkpoint_path) {
    return checkpoint_path.string() + ".manifest";
}

void save_manifest_atomic(const std::filesystem::path& path, const MultiwayBlueprintManifest& manifest) {
    const auto temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("multiway manifest temporary file cannot be opened");
    out.write(kManifestMagic.data(), static_cast<std::streamsize>(kManifestMagic.size()));
    write_value(out, manifest.schema_version);
    write_value(out, manifest.identity);
    write_value(out, manifest.snapshot_hash);
    out.close();
    if (!out) throw std::runtime_error("multiway manifest write failed");
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temp, path, error);
        if (error) throw std::runtime_error("multiway manifest publish failed");
    }
}

MultiwayBlueprintManifest load_manifest(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("multiway manifest cannot be opened");
    std::array<char, kManifestMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kManifestMagic) throw std::runtime_error("multiway manifest schema is invalid");
    MultiwayBlueprintManifest manifest;
    manifest.schema_version = read_value<std::uint32_t>(in);
    manifest.identity = read_value<MultiwayModelIdentity>(in);
    manifest.snapshot_hash = read_value<std::uint64_t>(in);
    if (in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("multiway manifest has trailing data");
    }
    manifest.validate();
    return manifest;
}

void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept {
    append_u64(hash, identity.rules_hash);
    append_u64(hash, identity.rules_schema_hash);
    append_u64(hash, identity.action_abstraction_hash);
    append_u64(hash, identity.bucket_model_hash);
    append_u64(hash, identity.terminal_model_hash);
    append_u64(hash, identity.resolver_schema_hash);
    append_u64(hash, identity.code_schema_hash);
    append_u64(hash, identity.range_semantics_hash);
    append_u64(hash, identity.future_bucket_model_hash);
    append_u64(hash, identity.off_tree_policy_hash);
    append_u64(hash, identity.continuation_policy_hash);
    append_u64(hash, identity.runtime_search_schema_hash);
    append_u64(hash, identity.combined_hash);
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
    auto hash = kFnvOffset;
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
    auto hash = kFnvOffset;
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
    write_value(out, sealed.schema_version);
    write_value(out, sealed.identity);
    write_value(out, sealed.training);
    const auto rows = static_cast<std::uint64_t>(sealed.rows.size());
    write_value(out, rows);
    for (const auto& row : sealed.rows) {
        write_value(out, row.infoset);
        write_value(out, row.bucket);
        write_value(out, row.action_menu_id);
        const auto actions = static_cast<std::uint32_t>(row.actions.size());
        write_value(out, actions);
        for (const auto& action : row.actions) write_value(out, action);
    }
    write_value(out, sealed.payload_hash);
    out.close();
    if (!out) throw std::runtime_error("multiway full blueprint write failed");
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) throw std::runtime_error("multiway full blueprint publish failed");
    }
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
    artifact.schema_version = read_value<std::uint32_t>(in);
    artifact.identity = read_value<MultiwayModelIdentity>(in);
    artifact.training = read_value<MultiwayBlueprintTrainingMetadata>(in);
    const auto row_count = read_value<std::uint64_t>(in);
    if (row_count > 100000000U) throw std::runtime_error("multiway full blueprint row count is invalid");
    const auto row_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayBlueprintRow));
    const auto action_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayQuantizedRootAction));
    if (row_count > (kFullBlueprintOperatingBytes - file_bytes) /
            std::max<std::uint64_t>(1U, row_storage_bytes + action_storage_bytes)) {
        throw std::length_error("multiway full blueprint rows exceed the operating memory guardrail");
    }
    artifact.rows.resize(static_cast<std::size_t>(row_count));
    for (auto& row : artifact.rows) {
        row.infoset = read_value<MultiwayInfosetId>(in);
        row.bucket = read_value<std::uint32_t>(in);
        row.action_menu_id = read_value<std::uint64_t>(in);
        const auto action_count = read_value<std::uint32_t>(in);
        if (action_count == 0U || action_count > 64U) throw std::runtime_error("multiway full blueprint action count is invalid");
        row.actions.resize(action_count);
        for (auto& action : row.actions) action = read_value<MultiwayQuantizedRootAction>(in);
    }
    artifact.payload_hash = read_value<std::uint64_t>(in);
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
    auto hash = kFnvOffset;
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
    MultiwayCheckpoint::save_atomic(checkpoint_path, snapshot);
    save_manifest_atomic(manifest_path(checkpoint_path), manifest);
}

MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_verified(
    const std::filesystem::path& checkpoint_path,
    const MultiwayModelIdentity& expected_identity) {
    MultiwayVerifiedBlueprintArtifact artifact;
    artifact.snapshot = MultiwayCheckpoint::load(checkpoint_path);
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
