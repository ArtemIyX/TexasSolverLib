#pragma once

/** @file
 * Isolated resolver adapter for host-driven candidate evaluation.
 */

#include "solver/multiway/resolver/multiway_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	// Candidate modes deliberately select existing resolver fallback/search paths.
	// Match scheduling remains owned by the evaluation host.
	/** Resolver fallback/search path selected for an evaluation candidate. */
	enum class MultiwayResolverEvaluationCandidateKind : std::uint8_t
	{
		StaticLegal,
		BlueprintOnly,
		SearchDisabled,
		SearchEnabled,
		FrozenPrior,
	};

	/** Stable candidate id paired with its resolver path. */
#pragma pack(push, 8)
	struct MultiwayResolverEvaluationCandidate
	{
		std::uint64_t id = 0U;
		MultiwayResolverEvaluationCandidateKind kind =
			MultiwayResolverEvaluationCandidateKind::StaticLegal;
	};
#pragma pack(pop)

	/** Resolver settings and candidate registry for the evaluation adapter. */
#pragma pack(push, 8)
	struct MultiwayResolverEvaluationAdapterConfig
	{
		MultiwayResolverConfig resolver{};
		std::vector<MultiwayResolverEvaluationCandidate> candidates;

		/** Checks candidate ids, modes, and resolver configuration. */
		void validate() const;
	};
#pragma pack(pop)

	// One decision record returned to a callback-owned match host. It retains
	// public resolver diagnostics but no request cards or ranges.
	/** Candidate decision result and seed used by one evaluation host call. */
#pragma pack(push, 8)
	struct MultiwayResolverEvaluationDecision
	{
		std::uint64_t candidate_id = 0U;
		std::uint64_t sampling_seed = 0U;
		MultiwayResolverResult result{};
	};
#pragma pack(pop)

	// Cold adapter for evaluation hosts. Every resolve call creates a fresh
	// resolver, so stable-root state and runtime search sessions are request-local.
	/** Runs isolated resolver decisions for host-supplied candidate identities. */
	class MultiwayResolverEvaluationAdapter
	{
	public:
		/** Creates an adapter after validating config and candidate ids. */
		explicit MultiwayResolverEvaluationAdapter(MultiwayResolverEvaluationAdapterConfig config);

		/** Resolves one decision using the selected candidate path and index. */
		[[nodiscard]] MultiwayResolverEvaluationDecision resolve(
			std::uint64_t candidate_id,
			const MultiwayResolverRequest& request,
			std::uint64_t decision_index) const;

	private:
		[[nodiscard]] const MultiwayResolverEvaluationCandidate& candidate(std::uint64_t id) const;

		MultiwayResolverEvaluationAdapterConfig config_{};
	};

} // namespace texas::solver::multiway
