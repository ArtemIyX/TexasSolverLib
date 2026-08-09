#pragma once

#include "solver/multiway_resolver.hpp"
#include "solver/multiway_traversal.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core {

// P0.1 fixture categories. They tag reports only; callers own the matching
// public request and must not persist its private inputs in the report.
enum class MultiwayBaselineFixtureKind : std::uint8_t {
    Valid,
    Invalid,
    OffTree,
    NoArtifact,
    DeadlineExhausted,
    MaxRows,
};

enum class MultiwayResolverFallbackKind : std::uint8_t {
    None,
    LatestStableRoot,
    Blueprint,
    StaticLegal,
};

// The recorder fills elapsed_nanoseconds and, when available, process resident
// memory. A supplied nonzero observed_memory_bytes takes precedence.
struct MultiwayBaselineMeasurements {
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t process_cpu_nanoseconds = 0;
    // Current RSS is a snapshot; peak RSS is process lifetime on supported
    // platforms. Neither is an allocation counter.
    std::uint64_t observed_memory_bytes = 0;
    std::uint64_t peak_resident_memory_bytes = 0;
    bool peak_resident_memory_available = false;
    // The library has no allocator hook. This remains explicitly unavailable
    // rather than treating RSS growth as allocation volume.
    std::uint64_t allocated_bytes = 0;
    bool allocation_bytes_available = false;
};

// Fixture inputs may contain private request data while they are executing.
// MultiwayResolverBaselineFixtureHarness never retains them after run returns;
// its reports are safe for public baseline output.
struct MultiwayResolverBaselineFixture {
    MultiwayBaselineFixtureKind kind = MultiwayBaselineFixtureKind::Valid;
    MultiwayResolverRequest request{};
    MultiwayBaselineMeasurements measurements{};
};

// This report intentionally omits hero cards, opponent ranges, and seeds.
// It contains only the public action menu, normalized policy, and diagnostics
// needed to compare the current resolver implementation with a future path.
struct MultiwayResolverBaselineReport {
    MultiwayBaselineFixtureKind fixture = MultiwayBaselineFixtureKind::Valid;
    MultiwayResolverStatus status = MultiwayResolverStatus::InvalidRequest;
    MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
    MultiwayResolverEngine search_engine = MultiwayResolverEngine::LegacyDeterministicAdjustment;
    std::uint64_t search_engine_version = 0;
    MultiwayModelIdentity artifact_identity{};
    bool has_artifact_identity = false;
    MultiwayResolverFallbackKind fallback = MultiwayResolverFallbackKind::None;
    bool has_sampled_action = false;
    MultiwayActionDescriptor sampled_action{};
    std::vector<MultiwayResolverActionProbability> policy;
    std::uint64_t completed_batches = 0;
    std::uint64_t completed_trajectories = 0;
    bool deadline_expired = false;
    bool policy_normalized = false;
    std::uint32_t root_bucket = 0;
    std::uint32_t root_menu_size = 0;
    std::uint64_t resolved_public_state_id = 0;
    MultiwayBaselineMeasurements measurements{};
};

struct MultiwayTraversalBaselineReport {
    MultiwayBaselineFixtureKind fixture = MultiwayBaselineFixtureKind::Valid;
    MultiwayRootBatchResult batch{};
    std::uint64_t public_states_admitted = 0;
    std::uint64_t sparse_rows_admitted = 0;
    std::uint64_t worker_delta_entries_merged = 0;
    std::uint64_t minimum_worker_trajectories = 0;
    std::uint64_t maximum_worker_trajectories = 0;
    MultiwayBaselineMeasurements measurements{};
};

inline constexpr double MULTIWAY_BASELINE_POLICY_ABSOLUTE_TOLERANCE = 1e-12;

// Builds an isolated resolver for every fixture execution so the retained
// stable-root fallback from one fixture cannot affect another one.
class MultiwayResolverBaselineFixtureHarness {
public:
    explicit MultiwayResolverBaselineFixtureHarness(MultiwayResolverConfig config = {});

    [[nodiscard]] MultiwayResolverBaselineReport run(
        const MultiwayResolverBaselineFixture& fixture) const;

private:
    MultiwayResolverConfig config_{};
};

// Returns the current process resident set when the platform exposes a safe
// source. Zero means the metric is unavailable. This is reporting-only.
[[nodiscard]] std::uint64_t observed_multiway_process_memory_bytes() noexcept;
[[nodiscard]] std::uint64_t observed_multiway_process_peak_memory_bytes() noexcept;
[[nodiscard]] std::uint64_t observed_multiway_process_cpu_nanoseconds() noexcept;

// Captures existing public paths without changing resolver or traversal
// behavior. The request is borrowed for the call and is never retained.
[[nodiscard]] MultiwayResolverBaselineReport record_multiway_resolver_baseline(
    MultiwayBaselineFixtureKind fixture,
    const MultiwayResolver& resolver,
    const MultiwayResolverRequest& request,
    MultiwayBaselineMeasurements measurements = {});

[[nodiscard]] MultiwayTraversalBaselineReport record_multiway_traversal_baseline(
    MultiwayBaselineFixtureKind fixture,
    MultiwayRootBatchRunner& runner,
    MultiwaySolverCoordinator& coordinator,
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t seed,
    double iteration_weight = 1.0,
    MultiwayBaselineMeasurements measurements = {});

// Deterministic equality intentionally excludes elapsed time, CPU time, and
// observed memory because those are environmental measurements. Policy values
// compare using MULTIWAY_BASELINE_POLICY_ABSOLUTE_TOLERANCE.
[[nodiscard]] bool equivalent_multiway_resolver_baseline(
    const MultiwayResolverBaselineReport& lhs,
    const MultiwayResolverBaselineReport& rhs,
    double policy_absolute_tolerance = MULTIWAY_BASELINE_POLICY_ABSOLUTE_TOLERANCE) noexcept;

[[nodiscard]] bool equivalent_multiway_traversal_baseline(
    const MultiwayTraversalBaselineReport& lhs,
    const MultiwayTraversalBaselineReport& rhs) noexcept;

// Stable serialization excludes environment-dependent measurements. It is
// intended for fixture golden files and contains no private request data.
[[nodiscard]] std::string serialize_multiway_resolver_baseline(
    const MultiwayResolverBaselineReport& report);
[[nodiscard]] std::string serialize_multiway_traversal_baseline(
    const MultiwayTraversalBaselineReport& report);

}  // namespace core
