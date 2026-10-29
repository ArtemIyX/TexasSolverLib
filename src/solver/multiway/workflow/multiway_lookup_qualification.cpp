#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"

#include "core/atomic_publish.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"

#include <fstream>
#include <array>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		template <typename T, std::size_t Size>
		void write_diagnostic_array(
			std::ostream& output, const std::array<T, Size>& values, std::size_t count)
		{
			output << '[';
			for (std::size_t index = 0U; index < count; ++index)
			{
				if (index != 0U)
					output << ',';
				output << +values[index];
			}
			output << ']';
		}

		void write_diagnostic_action(std::ostream& output, const MultiwayActionDescriptor& action)
		{
			output << "{\"action\":" << static_cast<unsigned int>(action.action)
				   << ",\"action_index\":" << action.action_index
				   << ",\"target_street_contribution\":" << action.target_street_contribution
				   << ",\"action_menu_id\":" << action.action_menu_id << '}';
		}

		void write_lookup_diagnostic(
			std::ostream& output, const MultiwayBlueprintLookupDiagnostic& sample)
		{
			output << "{\"status\":\"missing_infoset\",\"trajectory_id\":" << sample.trajectory_id
				   << ",\"replay_lookup_ordinal\":" << sample.replay_lookup_ordinal
				   << ",\"key\":{\"public_state_id\":" << sample.infoset.public_state.value
				   << ",\"reconstructed_public_state_id\":" << sample.reconstructed_public_state_id
				   << ",\"seat\":" << sample.infoset.seat
				   << ",\"bucket\":" << sample.bucket
				   << ",\"action_menu_id\":" << sample.action_menu_id << "}"
				   << ",\"identity_context_complete\":" << (sample.identity_context_complete ? "true" : "false")
				   << ",\"street\":" << static_cast<unsigned int>(sample.street)
				   << ",\"board_card_ids\":";
			write_diagnostic_array(output, sample.board, sample.board_count);
			output << ",\"canonical_history_id\":" << sample.canonical_history_id
				   << ",\"betting\":{\"seat_count\":" << static_cast<unsigned int>(sample.seat_count)
				   << ",\"stacks\":";
			write_diagnostic_array(output, sample.stacks, sample.seat_count);
			output << ",\"contributions\":";
			write_diagnostic_array(output, sample.contributions, sample.seat_count);
			output << ",\"street_contributions\":";
			write_diagnostic_array(output, sample.street_contributions, sample.seat_count);
			output << ",\"folded\":";
			write_diagnostic_array(output, sample.folded, sample.seat_count);
			output << ",\"all_in\":";
			write_diagnostic_array(output, sample.all_in, sample.seat_count);
			output << ",\"may_raise\":";
			write_diagnostic_array(output, sample.may_raise, sample.seat_count);
			output << ",\"pending\":";
			write_diagnostic_array(output, sample.pending, sample.seat_count);
			output << ",\"has_acted\":";
			write_diagnostic_array(output, sample.has_acted, sample.seat_count);
			output << ",\"bet_faced_when_acted\":";
			write_diagnostic_array(output, sample.bet_faced_when_acted, sample.seat_count);
			output << ",\"current_player\":" << sample.current_player
				   << ",\"last_aggressor\":" << sample.last_aggressor
				   << ",\"current_bet\":" << sample.current_bet
				   << ",\"last_full_raise_size\":" << sample.last_full_raise_size
				   << ",\"big_blind\":" << sample.big_blind << "}"
				   << ",\"history_count\":" << sample.history_count
				   << ",\"history_truncated\":" << (sample.history_count != sample.history_stored_count ? "true" : "false")
				   << ",\"public_history\":[";
			for (std::size_t index = 0U; index < sample.history_stored_count; ++index)
			{
				if (index != 0U)
					output << ',';
				const auto& entry = sample.history[index];
				output << "{\"actor\":" << entry.actor << ",\"action\":";
				write_diagnostic_action(output, entry.action);
				output << '}';
			}
			output << "],\"legal_actions\":[";
			for (std::size_t index = 0U; index < sample.legal_action_count; ++index)
			{
				if (index != 0U)
					output << ',';
				write_diagnostic_action(output, sample.legal_actions[index]);
			}
			output << "]}";
		}

		MultiwayLookupQualificationReport qualify_with_provider(
			const MultiwayBlueprintTrainingConfig& config,
			const MultiwayRootSnapshot& root,
			const MultiwayBucketRegistry& buckets,
			const MultiwayBlueprintPolicyProvider& provider,
			const MultiwayModelIdentity& blueprint_identity,
			std::uint64_t policy_content_hash,
			std::uint64_t trajectory_count,
			MultiwayEvidenceHeader evidence,
			std::size_t diagnostic_sample_limit)
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
			audit.set_diagnostic_sample_limit(diagnostic_sample_limit, trajectory_count);
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
			report.diagnostic_sample_limit = diagnostic_sample_limit;
			report.diagnostic_samples = audit.diagnostic_samples();
			return report;
		}
	} // namespace

	void MultiwayLookupQualificationReport::validate() const
	{
		evidence.validate(MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION);
		if (diagnostic_sample_limit > MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_SAMPLES
			|| diagnostic_samples.size() > diagnostic_sample_limit)
			throw std::invalid_argument("multiway lookup diagnostic samples exceed their bound");
		for (const auto& sample : diagnostic_samples)
		{
			if (sample.status != MultiwayBlueprintLookupStatus::Missing
				|| sample.infoset.public_state.value == 0U
				|| sample.infoset.seat < 0
				|| sample.action_menu_id == 0U
				|| sample.history_stored_count > sample.history.size()
				|| sample.legal_action_count > sample.legal_actions.size()
				|| sample.board_count > sample.board.size()
				|| sample.seat_count > sample.stacks.size())
				throw std::invalid_argument("multiway lookup diagnostic sample is invalid");
		}
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
		MultiwayEvidenceHeader evidence,
		std::size_t diagnostic_sample_limit)
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
			evidence, diagnostic_sample_limit);
	}

	MultiwayLookupQualificationReport qualify_multiway_required_lookups(
		const MultiwayBlueprintTrainingConfig& config,
		const MultiwayRootSnapshot& root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayMappedShardedBlueprintProvider& blueprint,
		std::uint64_t trajectory_count,
		MultiwayEvidenceHeader evidence,
		std::size_t diagnostic_sample_limit)
	{
		MultiwayBlueprintPolicyProvider provider(blueprint);
		return qualify_with_provider(config, root, buckets, provider, blueprint.identity(),
			blueprint.policy_content_hash(), trajectory_count, evidence, diagnostic_sample_limit);
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
			   << ",\n  \"second_replay_fingerprint\": " << report.second_replay_fingerprint;
		if (report.diagnostic_sample_limit != 0U)
		{
			output << ",\n  \"lookup_diagnostics\": {\"sample_limit\": " << report.diagnostic_sample_limit
				   << ", \"selection\": \"stable_hash_priority_over_distinct_keys\""
				   << ", \"maximum_history_entries_per_sample\": " << MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_HISTORY
				   << ", \"private_cards_included\": false, \"samples\": [";
			for (std::size_t index = 0U; index < report.diagnostic_samples.size(); ++index)
			{
				if (index != 0U)
					output << ',';
				write_lookup_diagnostic(output, report.diagnostic_samples[index]);
			}
			output << "]}";
		}
		output << ",\n  \"passed\": " << (report.passed() ? "true" : "false") << "\n}\n";
		output.close();
		core::publish_atomic_replace(temporary, path, "cannot publish lookup qualification report");
	}
} // namespace texas::solver::multiway
