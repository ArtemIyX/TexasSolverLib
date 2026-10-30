#pragma once

/** @file
 * Deterministic runtime blueprint lookup qualification and report output.
 */

#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace texas::solver::multiway
{
	class MultiwayMappedShardedBlueprintProvider;

	/** Lookup outcomes, policy hash, replay fingerprints, and bounded samples. */
	struct MultiwayLookupQualificationReport
	{
		MultiwayEvidenceHeader evidence{
			MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION,
			MultiwayWorkflowProfileKind::Acceptance,
			{}
		};
		std::uint64_t lookup_hits = 0U;
		std::uint64_t missing_infosets = 0U;
		std::uint64_t missing_buckets = 0U;
		std::uint64_t action_menu_mismatches = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t replay_fingerprint = 0U;
		std::uint64_t second_replay_fingerprint = 0U;
		std::size_t diagnostic_sample_limit = 0U;
		std::vector<MultiwayBlueprintLookupDiagnostic> diagnostic_samples;

		/** Checks evidence schema, counters, and diagnostic sample bounds. */
		void validate() const;

		/** Returns whether all required lookup and replay checks passed. */
		[[nodiscard]] bool passed() const noexcept
		{
			return missing_infosets == 0U && missing_buckets == 0U && action_menu_mismatches == 0U && (lookup_hits == 0U || policy_content_hash != 0U) && (second_replay_fingerprint == 0U || replay_fingerprint == second_replay_fingerprint);
		}
	};

	// Replays deterministic sampled trajectories from the supplied initial root.
	// Opponent policy requests must resolve through the immutable full blueprint.
	/** Qualifies required policy lookups against a full in-memory blueprint. */
	[[nodiscard]] MultiwayLookupQualificationReport qualify_multiway_required_lookups(
		const MultiwayBlueprintTrainingConfig& config,
		const MultiwayRootSnapshot& root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayFullBlueprintArtifact& blueprint,
		std::uint64_t trajectory_count,
		MultiwayEvidenceHeader evidence,
		std::size_t diagnostic_sample_limit = 0U);
	/** Qualifies lookups against a verified mapped sharded blueprint provider. */
	[[nodiscard]] MultiwayLookupQualificationReport qualify_multiway_required_lookups(
		const MultiwayBlueprintTrainingConfig& config,
		const MultiwayRootSnapshot& root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayMappedShardedBlueprintProvider& blueprint,
		std::uint64_t trajectory_count,
		MultiwayEvidenceHeader evidence,
		std::size_t diagnostic_sample_limit = 0U);

	/** Validates and atomically writes a lookup qualification report. */
	void save_multiway_lookup_qualification_report_atomic(
		const std::filesystem::path& path,
		const MultiwayLookupQualificationReport& report);

} // namespace texas::solver::multiway
