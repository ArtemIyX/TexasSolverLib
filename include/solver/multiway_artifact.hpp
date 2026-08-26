#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "games/multiway_replay.hpp"
#include "solver/multiway_export.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_resolver.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION = 4U;
inline constexpr std::uint32_t MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION = 3U;
inline constexpr std::uint32_t MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION = 1U;

// Sidecar for a compact root-only checkpoint. It binds the artifact bytes to
// the exact model identity without adding row storage to deployment exports.
struct MultiwayBlueprintManifest {
    std::uint32_t schema_version = MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION;
    MultiwayModelIdentity identity{};
    std::uint64_t snapshot_hash = 0;

    void validate() const;
};

enum class MultiwayArtifactSource : std::uint8_t {
    Primary,
    KnownGoodFallback,
};

struct MultiwayVerifiedBlueprintArtifact {
    MultiwayBlueprintSnapshot snapshot{};
    MultiwayBlueprintManifest manifest{};
    MultiwayArtifactSource source = MultiwayArtifactSource::Primary;

    void validate(const MultiwayModelIdentity& expected_identity) const;
};

inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION = 2U;

// Full runtime lookup payload. It is independent of the compact root
// snapshot so hosts can retain the latter as a compatible fallback.
struct MultiwayFullBlueprintArtifact {
    std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION;
    MultiwayModelIdentity identity{};
    MultiwayBlueprintTrainingMetadata training{};
    std::vector<MultiwayBlueprintRow> rows;
    std::uint64_t payload_hash = 0U;

    void validate() const;
};

class MultiwayFullBlueprintArtifacts {
public:
    static void save_atomic(const std::filesystem::path& path, const MultiwayFullBlueprintArtifact& artifact);
    [[nodiscard]] static MultiwayFullBlueprintArtifact load_verified(
        const std::filesystem::path& path,
        const MultiwayModelIdentity& expected_identity);
    [[nodiscard]] static std::uint64_t payload_hash(const MultiwayFullBlueprintArtifact& artifact) noexcept;
};

class MultiwayBlueprintArtifacts {
public:
    // Writes the compact checkpoint and its manifest. A partially published
    // pair fails validation on load rather than being accepted optimistically.
    static void save_atomic(
        const std::filesystem::path& checkpoint_path,
        const MultiwayBlueprintSnapshot& snapshot);
    [[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_verified(
        const std::filesystem::path& checkpoint_path,
        const MultiwayModelIdentity& expected_identity);
    [[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_with_fallback(
        const std::filesystem::path& primary_checkpoint_path,
        const std::filesystem::path& known_good_checkpoint_path,
        const MultiwayModelIdentity& expected_identity);
    [[nodiscard]] static std::uint64_t snapshot_hash(
        const MultiwayBlueprintSnapshot& snapshot) noexcept;
};

// Public audit record. It intentionally has no cards, ranges, raw seeds, or
// worker state. Probabilities are quantized to preserve a stable log boundary.
struct MultiwayPublicDecisionPolicy {
    MultiwayActionDescriptor action{};
    std::uint16_t probability = 0;
};

struct MultiwayPublicDecisionLog {
    std::uint32_t schema_version = MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION;
    MultiwayModelIdentity identity{};
    std::uint64_t public_state_id = 0;
    std::uint64_t decision_index = 0;
    PlayerId acting_seat = -1;
    MultiwayActionDescriptor sampled_action{};
    MultiwayResolverStatus resolver_status = MultiwayResolverStatus::InvalidRequest;
    MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
    MultiwayResolverEngine search_engine = MultiwayResolverEngine::NoRuntimeSearch;
    std::uint64_t search_engine_version = MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION;
    bool used_fallback = false;
    std::vector<MultiwayPublicDecisionPolicy> policy;

    void validate() const;
};

[[nodiscard]] MultiwayPublicDecisionLog make_multiway_public_decision_log(
    const MultiwayResolverRequest& request,
    const MultiwayResolverResult& result,
    std::uint64_t decision_index);

// Sealed, public-only replay envelope. It carries the per-decision seeds from
// the public replay history while excluding hero cards and opponent ranges.
struct MultiwayProtectedReplayRecord {
    std::uint32_t schema_version = MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION;
    MultiwayModelIdentity identity{};
    MultiwayHandHistory public_history{};
    std::vector<std::uint64_t> decision_seeds;
    std::uint64_t integrity_hash = 0;

    [[nodiscard]] static MultiwayProtectedReplayRecord from_history(
        const MultiwayModelIdentity& identity,
        const MultiwayHandHistory& public_history);
    void seal() noexcept;
    void validate() const;
};

}  // namespace texas::solver::multiway
