#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"

#include "core/atomic_publish.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"

#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		MultiwayLookupQualificationReport qualify_with_provider(
			const MultiwayBlueprintTrainingConfig& config,
			const MultiwayRootSnapshot& root,
			const MultiwayBucketRegistry& buckets,
			const MultiwayBlueprintPolicyProvider& provider,
			const MultiwayModelIdentity& blueprint_identity,
			std::uint64_t policy_content_hash,
			std::uint64_t trajectory_count,
			MultiwayEvidenceHeader evidence)
		{
			config.validate();
			root.validate();
			const auto expected_identity = config.identity();
			const auto base_model_identity = make_multiway_model_identity(config.blueprint);
			const auto buckets_match = buckets.identity() == base_model_identity || multiway_policy_identity_compatible(buckets.identity(), expected_identity);
			if (trajectory_count == 0U || !multiway_policy_identity_compatible(blueprint_identity, expected_identity) || !buckets_match || policy_content_hash == 0U)
				throw std::invalid_argument("multiway lookup qualification inputs are incompatible");
			if (config.limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
				throw std::invalid_argument("ExternalCompactInt32 is not ready for lookup qualification");
			MultiwaySolveRequest request(root, config.cfr, config.limits);
			MultiwaySolverCoordinator coordinator(request);
			MultiwayActionAbstraction action_abstraction(config.action_abstraction);
			MultiwayRootExternalSamplingTraversal traversal(
				coordinator, root, action_abstraction, buckets, nullptr,
				config.max_decision_depth, config.max_public_chance_depth, &provider);
			MultiwayWorkerDeltaStream stream(0U, config.limits.regret_delta_capacity());
			MultiwayBlueprintLookupAudit audit;
			for (std::uint64_t trajectory = 0U; trajectory < trajectory_count; ++trajectory)
			{
				stream.rewind(0U);
				if (!traversal.run(
						traversal.traverser_for_trajectory(trajectory), trajectory,
						multiway_deterministic_trajectory_seed(config.deterministic_seed, trajectory),
						stream, 1.0, nullptr, nullptr, 0U, &audit))
					throw std::runtime_error("multiway lookup qualification discarded a trajectory");
			}
			MultiwayLookupQualificationReport report;
			report.evidence = evidence;
			report.lookup_hits = audit.lookup_hits;
			report.missing_infosets = audit.missing_infosets;
			report.missing_buckets = audit.missing_buckets;
			report.action_menu_mismatches = audit.action_menu_mismatches;
			report.policy_content_hash = policy_content_hash;
			report.replay_fingerprint = audit.fingerprint();
			return report;
		}
	} // namespace

	void MultiwayLookupQualificationReport::validate() const
	{
		evidence.validate(MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION);
		if (second_replay_fingerprint != 0U && replay_fingerprint != second_replay_fingerprint)
		{
			throw std::invalid_argument("multiway lookup replay fingerprints differ");
		}
		if (lookup_hits != 0U && policy_content_hash == 0U)
			throw std::invalid_argument("multiway lookup qualification lacks policy content hash");
	}

	MultiwayLookupQualificationReport qualify_multiway_required_lookups(
		const MultiwayBlueprintTrainingConfig& config,
		const MultiwayRootSnapshot& root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayFullBlueprintArtifact& blueprint,
		std::uint64_t trajectory_count,
		MultiwayEvidenceHeader evidence)
	{
		config.validate();
		root.validate();
		blueprint.validate();
		const auto expected_identity = config.identity();
		const auto base_model_identity = make_multiway_model_identity(config.blueprint);
		const auto buckets_match = buckets.identity() == base_model_identity || multiway_policy_identity_compatible(buckets.identity(), expected_identity);
		if (trajectory_count == 0U || !multiway_policy_identity_compatible(blueprint.identity, expected_identity) || !buckets_match)
			throw std::invalid_argument("multiway lookup qualification inputs are incompatible");
		if (config.limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
			throw std::invalid_argument("ExternalCompactInt32 is not ready for lookup qualification");
		MultiwayBlueprintStore store(blueprint.identity, blueprint.rows);
		MultiwayBlueprintPolicyProvider provider(store);
		return qualify_with_provider(config, root, buckets, provider, blueprint.identity,
			MultiwayFullBlueprintArtifacts::policy_content_hash(blueprint), trajectory_count,
			evidence);
	}

	MultiwayLookupQualificationReport qualify_multiway_required_lookups(
		const MultiwayBlueprintTrainingConfig& config,
		const MultiwayRootSnapshot& root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayMappedShardedBlueprintProvider& blueprint,
		std::uint64_t trajectory_count,
		MultiwayEvidenceHeader evidence)
	{
		MultiwayBlueprintPolicyProvider provider(blueprint);
		return qualify_with_provider(config, root, buckets, provider, blueprint.identity(),
			blueprint.policy_content_hash(), trajectory_count, evidence);
	}

	void save_multiway_lookup_qualification_report_atomic(
		const std::filesystem::path& path, const MultiwayLookupQualificationReport& report)
	{
		report.validate();
		const auto temporary = path.string() + ".tmp";
		std::ofstream output(temporary, std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot open lookup qualification report");
		output << "{\n  \"evidence\": " << serialize_multiway_evidence_header(report.evidence)
			   << ",\n  \"lookup_hits\": " << report.lookup_hits
			   << ",\n  \"missing_infosets\": " << report.missing_infosets
			   << ",\n  \"missing_buckets\": " << report.missing_buckets
			   << ",\n  \"action_menu_mismatches\": " << report.action_menu_mismatches
			   << ",\n  \"policy_content_hash\": " << report.policy_content_hash
			   << ",\n  \"replay_fingerprint\": " << report.replay_fingerprint
			   << ",\n  \"second_replay_fingerprint\": " << report.second_replay_fingerprint
			   << ",\n  \"passed\": " << (report.passed() ? "true" : "false") << "\n}\n";
		output.close();
		core::publish_atomic_replace(temporary, path, "cannot publish lookup qualification report");
	}
} // namespace texas::solver::multiway
