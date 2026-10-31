#pragma once

/** @file
 * Public-only resolver and traversal baseline capture and comparison APIs.
 */

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway/resolver/multiway_resolver.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace texas::solver::multiway
{

	// P0.1 fixture categories. They tag reports only; callers own the matching
	// public request and must not persist its private inputs in the report.
	/** Fixture category used to label baseline measurements. */
	enum class MultiwayBaselineFixtureKind : std::uint8_t
	{
		Valid,
		Invalid,
		OffTree,
		NoArtifact,
		DeadlineExhausted,
		MaxRows,
	};

	/** Fallback source selected by a resolver baseline invocation. */
	enum class MultiwayResolverFallbackKind : std::uint8_t
	{
		None,
		LatestStableRoot,
		Blueprint,
		StaticLegal,
	};

	// The recorder fills elapsed_nanoseconds and, when available, process resident
	// memory. A supplied nonzero observed_memory_bytes takes precedence.
	/** Elapsed time, CPU, resident-memory, and allocation observations. */
#pragma pack(push, 8)
	struct MultiwayBaselineMeasurements
	{
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
#pragma pack(pop)

	// Fixture inputs may contain private request data while they are executing.
	// MultiwayResolverBaselineFixtureHarness never retains them after run returns;
	// its reports are safe for public baseline output.
	/** Resolver request and measurement inputs for one isolated fixture run. */
#pragma pack(push, 8)
	struct MultiwayResolverBaselineFixture
	{
		MultiwayBaselineFixtureKind kind = MultiwayBaselineFixtureKind::Valid;
		MultiwayResolverRequest request{};
		MultiwayBaselineMeasurements measurements{};
	};
#pragma pack(pop)

	// This report intentionally omits hero cards, opponent ranges, and seeds.
	// It contains only the public action menu, normalized policy, and diagnostics
	// needed to compare the current resolver implementation with a future path.
	/** Public-only policy, fallback, counts, and measurement results. */
#pragma pack(push, 8)
	struct MultiwayResolverBaselineReport
	{
		MultiwayBaselineFixtureKind fixture = MultiwayBaselineFixtureKind::Valid;
		MultiwayResolverStatus status = MultiwayResolverStatus::InvalidRequest;
		MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
		MultiwayResolverEngine search_engine = MultiwayResolverEngine::NoRuntimeSearch;
		std::uint64_t search_engine_version = MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION;
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
#pragma pack(pop)

	/** Batch-run counts, search profile, and timing measurements. */
#pragma pack(push, 8)
	struct MultiwayTraversalBaselineReport
	{
		MultiwayBaselineFixtureKind fixture = MultiwayBaselineFixtureKind::Valid;
		MultiwayRootBatchResult batch{};
		std::uint64_t public_states_admitted = 0;
		std::uint64_t sparse_rows_admitted = 0;
		std::uint64_t worker_delta_entries_merged = 0;
		std::uint64_t minimum_worker_trajectories = 0;
		std::uint64_t maximum_worker_trajectories = 0;
		MultiwaySearchProfileSnapshot search_profile{};
		MultiwayBaselineMeasurements measurements{};
	};
#pragma pack(pop)

	inline constexpr double MULTIWAY_BASELINE_POLICY_ABSOLUTE_TOLERANCE = 1e-12;

	// Builds an isolated resolver for every fixture execution so the retained
	// stable-root fallback from one fixture cannot affect another one.
	/** Runs resolver fixtures with isolated fallback state. */
	class MultiwayResolverBaselineFixtureHarness
	{
	public:
		/** Creates a fixture runner with resolver defaults and limits. */
		explicit MultiwayResolverBaselineFixtureHarness(MultiwayResolverConfig config = {});

		/** Resolves one fixture and returns a public-only stable report. */
		[[nodiscard]] MultiwayResolverBaselineReport run(
			const MultiwayResolverBaselineFixture& fixture) const;

	private:
		MultiwayResolverConfig config_{};
	};

	// Reporting-only process measurements. Zero means the metric is unavailable.
	[[nodiscard]] std::uint64_t observed_multiway_process_peak_memory_bytes() noexcept;
	[[nodiscard]] std::uint64_t observed_multiway_process_cpu_nanoseconds() noexcept;

	// Captures existing public paths without changing resolver or traversal
	// behavior. The request is borrowed for the call and is never retained.
	/** Captures the current resolver path and its public policy result. */
	[[nodiscard]] MultiwayResolverBaselineReport record_multiway_resolver_baseline(
		MultiwayBaselineFixtureKind fixture,
		const MultiwayResolver& resolver,
		const MultiwayResolverRequest& request,
		MultiwayBaselineMeasurements measurements = {});

	/** Runs and records one root batch for deterministic baseline comparison. */
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
	/** Compares deterministic resolver outputs within probability tolerance. */
	[[nodiscard]] bool equivalent_multiway_resolver_baseline(
		const MultiwayResolverBaselineReport& lhs,
		const MultiwayResolverBaselineReport& rhs,
		double policy_absolute_tolerance = MULTIWAY_BASELINE_POLICY_ABSOLUTE_TOLERANCE) noexcept;

	/** Compares deterministic traversal outputs, excluding environmental metrics. */
	[[nodiscard]] bool equivalent_multiway_traversal_baseline(
		const MultiwayTraversalBaselineReport& lhs,
		const MultiwayTraversalBaselineReport& rhs) noexcept;

	// Stable serialization excludes environment-dependent measurements. It is
	// intended for fixture golden files and contains no private request data.
	/** Serializes stable resolver results without environment-dependent measurements. */
	[[nodiscard]] std::string serialize_multiway_resolver_baseline(
		const MultiwayResolverBaselineReport& report);
	/** Serializes stable traversal results without environment-dependent measurements. */
	[[nodiscard]] std::string serialize_multiway_traversal_baseline(
		const MultiwayTraversalBaselineReport& report);

} // namespace texas::solver::multiway
