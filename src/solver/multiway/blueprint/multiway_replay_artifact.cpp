#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "core/fingerprint.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		using texas::core::fingerprint::append_u64;
		using texas::core::fingerprint::finish;
		void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
		}
		void append_initial_config(std::uint64_t& hash, const MultiwayGameConfig& config) noexcept
		{
			append_u64(hash, config.starting_stacks.size());
			for (const auto value : config.starting_stacks)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, config.initial_contributions.size());
			for (const auto value : config.initial_contributions)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, config.initial_street_contributions.size());
			for (const auto value : config.initial_street_contributions)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, static_cast<std::uint64_t>(config.first_player));
			append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(config.big_blind)));
			append_u64(hash, static_cast<std::uint64_t>(config.street));
			append_u64(hash, config.rake_policy.identity());
		}

		std::uint16_t quantize_probability(double probability)
		{
			if (!std::isfinite(probability) || probability < 0.0)
			{
				throw std::invalid_argument("multiway decision log has invalid probability");
			}
			const auto scaled = probability * std::numeric_limits<std::uint16_t>::max();
			if (scaled > static_cast<double>(std::numeric_limits<std::uint16_t>::max()))
			{
				throw std::invalid_argument("multiway decision log probability exceeds one");
			}
			return static_cast<std::uint16_t>(std::floor(scaled));
		}

		std::uint64_t replay_hash(const MultiwayProtectedReplayRecord& record) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, record.schema_version);
			append_identity(hash, record.identity);
			append_u64(hash, record.public_history.schema_version);
			append_u64(hash, record.public_history.hand_seed);
			append_initial_config(hash, record.public_history.initial_config);
			append_u64(hash, record.public_history.events.size());
			for (const auto& event : record.public_history.events)
			{
				append_u64(hash, static_cast<std::uint64_t>(event.kind));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.acting_seat));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.action));
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(event.decision.target_street_contribution)));
				append_u64(hash, event.decision.decision_seed);
				append_u64(hash, static_cast<std::uint64_t>(event.next_street));
				append_u64(hash, static_cast<std::uint64_t>(event.first_player));
			}
			append_u64(hash, record.decision_seeds.size());
			for (const auto seed : record.decision_seeds)
				append_u64(hash, seed);
			return finish(hash);
		}

	} // namespace

	void MultiwayPublicDecisionLog::validate() const
	{
		identity.validate();
		if (schema_version != MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION || public_state_id == 0U || decision_index == 0U || acting_seat < 0 || policy.empty() || sampled_action.action_menu_id == 0U || policy_provenance == MultiwayPolicyProvenance::None || search_engine_version == 0U || (resolver_status != MultiwayResolverStatus::Solved && resolver_status != MultiwayResolverStatus::Partial && resolver_status != MultiwayResolverStatus::DeadlineFallback && resolver_status != MultiwayResolverStatus::ArtifactMismatch && resolver_status != MultiwayResolverStatus::BucketUnavailable && resolver_status != MultiwayResolverStatus::ResourceExhausted && resolver_status != MultiwayResolverStatus::RejectedByBudget))
		{
			throw std::invalid_argument("multiway public decision log has invalid metadata");
		}
		const bool runtime_search = policy_provenance == MultiwayPolicyProvenance::RuntimeSearch;
		const bool fallback = policy_provenance == MultiwayPolicyProvenance::StableRootFallback || policy_provenance == MultiwayPolicyProvenance::BlueprintFallback || policy_provenance == MultiwayPolicyProvenance::StaticLegalFallback;
		if ((runtime_search && (search_engine != MultiwayResolverEngine::RootExternalSamplingMCCFR || search_engine_version != MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION)) || (fallback && (search_engine != MultiwayResolverEngine::NoRuntimeSearch || search_engine_version != MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION)))
		{
			throw std::invalid_argument("multiway public decision log has inconsistent provenance");
		}
		std::uint64_t total = 0;
		bool found_sample = false;
		for (std::size_t index = 0; index < policy.size(); ++index)
		{
			if (policy[index].action.action_index != index || policy[index].action.action_menu_id == 0U)
			{
				throw std::invalid_argument("multiway public decision log has invalid action order");
			}
			total += policy[index].probability;
			found_sample = found_sample || policy[index].action == sampled_action;
		}
		if (total != std::numeric_limits<std::uint16_t>::max() || !found_sample)
		{
			throw std::invalid_argument("multiway public decision log has invalid policy");
		}
	}

	MultiwayPublicDecisionLog make_multiway_public_decision_log(
		const MultiwayResolverRequest& request,
		const MultiwayResolverResult& result,
		std::uint64_t decision_index)
	{
		request.blueprint_identity.validate();
		if (decision_index == 0U || !result.has_sampled_action || !result.diagnostics.policy_normalized || result.policy.empty())
		{
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
		for (std::size_t index = 0; index < result.policy.size(); ++index)
		{
			auto probability = index + 1U == result.policy.size()
				? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
				: quantize_probability(result.policy[index].probability);
			assigned += probability;
			log.policy.push_back({ result.policy[index].action, probability });
		}
		log.validate();
		return log;
	}

	MultiwayProtectedReplayRecord MultiwayProtectedReplayRecord::from_history(
		const MultiwayModelIdentity& record_identity,
		const MultiwayHandHistory& history)
	{
		record_identity.validate();
		history.validate();
		MultiwayProtectedReplayRecord record;
		record.identity = record_identity;
		record.public_history = history;
		record.decision_seeds.reserve(history.events.size());
		for (const auto& event : history.events)
		{
			if (event.kind == MultiwayReplayEventKind::Decision)
			{
				record.decision_seeds.push_back(event.decision.decision_seed);
			}
		}
		record.seal();
		record.validate();
		return record;
	}

	void MultiwayProtectedReplayRecord::seal() noexcept
	{
		integrity_hash = replay_hash(*this);
	}

	void MultiwayProtectedReplayRecord::validate() const
	{
		if (schema_version != MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION || integrity_hash == 0U)
		{
			throw std::invalid_argument("multiway protected replay has invalid schema or integrity hash");
		}
		identity.validate();
		public_history.validate();
		std::size_t decision_index = 0;
		for (const auto& event : public_history.events)
		{
			if (event.kind == MultiwayReplayEventKind::Decision)
			{
				if (decision_index >= decision_seeds.size() || decision_seeds[decision_index] != event.decision.decision_seed)
				{
					throw std::invalid_argument("multiway protected replay seed record is inconsistent");
				}
				++decision_index;
			}
		}
		if (decision_index != decision_seeds.size() || integrity_hash != replay_hash(*this))
		{
			throw std::invalid_argument("multiway protected replay integrity check failed");
		}
	}


} // namespace texas::solver::multiway
