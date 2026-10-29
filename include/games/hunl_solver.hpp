#pragma once

/** @file
 * @brief Declares postflop HUNL solve requests, results and validation entry points.
 */

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/generic/parallel_dcfr.hpp"
#include "solver/hunl/hunl_leaf_evaluator.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Metric used to report HUNL solve quality. */
	enum class HUNLQualityMetric : std::uint8_t
	{
		PerPlayerExploitability, ///< Exploitability reported per player.
	};

	// Immutable public resolver boundary. The state contains exact chips, actor,
	// pending call, raise state, board, and canonical betting history. The
	// request's range/configuration metadata is rebound before private deals are
	// attached, so callers cannot mutate a live state after admission.
	/** @brief Immutable public-game snapshot used as a structured resolver root. */
	struct HUNLLiveRootSnapshot
	{
		HUNLState public_state; ///< Public state with private cards omitted.
		std::vector<ActionId> legal_actions; ///< Legal actions captured at the root.
		std::string canonical_public_history; ///< Canonical history expected for the state.
		std::string state_version; ///< Caller-supplied version identifying this snapshot.

		/** @brief Checks that snapshot state and metadata agree with config.
		 * @throws std::invalid_argument If the public state or metadata is inconsistent.
		 */
		void validate(const HUNLConfig& config) const;
		/** @brief Returns a state rebound to the supplied immutable configuration.
		 * @throws std::invalid_argument If config is null or incompatible with the snapshot.
		 */
		[[nodiscard]] HUNLState bind_config(std::shared_ptr<const HUNLConfig> config) const;
	};

	/** @brief Configuration, public root and private-hand contract for a structured solve. */
	struct HUNLStructuredRootRequest
	{
		HUNLConfig config; ///< Game and solver configuration.
		// Absent roots retain the legacy config-derived balanced entry. Unequal
		// contributions and live/off-tree roots require this full snapshot.
		std::optional<HUNLLiveRootSnapshot> live_root = std::nullopt; ///< Optional full public state for live or off-tree resolving.
		// Traversal normalizes terminal values from its internal big-blind unit at
		// the root boundary before comparing them with leaf values.
		HUNLLeafValueUnits value_units = HUNLLeafValueUnits::Chips; ///< Units used by leaf values at the root boundary.
		// Provenance for the externally selected blueprint. The sampled solver
		// starts unseeded (uniform regret matching); it never silently treats a
		// version string as a prior strategy or leaf value table.
		std::string blueprint_version; ///< Provenance label for the selected external blueprint.
		std::string model_version; ///< Provenance label for the selected external model.
		// The private hand whose acting-root strategy is exported. Bucket zero is
		// the only current structured-range bucket; keeping it explicit prevents
		// a future abstraction from silently changing the export contract.
		struct HeroSelection
		{
			PlayerId player = -1; ///< Acting player whose strategy is returned.
			std::array<std::uint8_t, 2> hole_cards = { 0, 0 }; ///< Hero's private cards.
			std::uint32_t bucket = 0; ///< Export bucket; current contract supports bucket zero.
		};
		std::optional<HeroSelection> hero_selection = std::nullopt; ///< Optional explicit hero hand and bucket selection.
		// Optional diagnostic only. It never replaces `root_strategy`.
		bool include_range_wide_root_diagnostics = false; ///< Include diagnostics alongside the selected root strategy.

		/** @brief Normalizes configured private ranges after blocker conditioning. */
		[[nodiscard]] std::vector<HUNLJointRangeDeal> normalized_joint_range() const;
		/** @brief Builds the public root state using the supplied immutable configuration. */
		[[nodiscard]] HUNLState public_root_state(std::shared_ptr<const HUNLConfig> config) const;
		/** @brief Returns the explicit hero selection or derives the effective default. */
		[[nodiscard]] HeroSelection effective_hero_selection() const;
		/** @brief Validates the structured root, ranges and hero selection.
		 * @throws std::invalid_argument If any request component is inconsistent or unsupported.
		 */
		void validate() const;
	};

	/** @brief Solver metrics, quality values and average strategy output. */
	struct HUNLSolveOutput
	{
		std::unordered_map<std::string, std::vector<double>> average_strategy; ///< Average action probabilities by infoset key.
		double exploitability = 0.0; ///< Per-player exploitability value reported by the solver.
		double total_nash_conv = 0.0; ///< Total Nash-conv value reported by the solver.
		HUNLQualityMetric quality_metric = HUNLQualityMetric::PerPlayerExploitability; ///< Interpretation of quality fields.
		double game_value = 0.0; ///< Player-zero game value.
		std::uint32_t iterations = 0; ///< Completed solver iterations.
		double wallclock_seconds = 0.0; ///< Total solve wrapper time.
		double traversal_seconds = 0.0; ///< Time spent in traversal.
		double solver_finalize_seconds = 0.0; ///< Time spent finalizing solver output.
		double wrapper_postprocess_seconds = 0.0; ///< Time spent in wrapper postprocessing.
		std::uint32_t infoset_count = 0; ///< Number of output information sets.
		bool used_parallel = false; ///< Whether the parallel solver path ran.
		SolveProfile profile; ///< Solver and traversal profile counters.
	};

	/** @brief Structured error categories used by postflop HUNL validation. */
	enum class HUNLSolveError
	{
		PreflopNotSupported = 0, ///< Requested start street is preflop.
		BoardLengthMismatch = 1, ///< Board size does not match the postflop street.
		RakeNonZero = 2, ///< Current backend does not accept non-zero rake.
		InvalidConfig = 3, ///< Configuration is otherwise invalid for the solver.
	};

	/** @brief Selects the recursive or flat graph solver backend. */
	enum class HUNLBackendSelection
	{
		Auto = 0, ///< Choose a backend from graph size and configuration.
		Recursive = 1, ///< Use the recursive tree backend.
		Flat = 2, ///< Use the flat graph backend.
	};

	/** @brief Solves a supported postflop configuration using DCFR.
	 * @param config Postflop game configuration.
	 * @param iterations Number of solver iterations.
	 * @param alpha DCFR positive-regret discount exponent.
	 * @param beta DCFR negative-regret discount exponent.
	 * @param gamma DCFR average-strategy weighting exponent.
	 * @param workers Worker count requested for the parallel backend.
	 * @param frontier_multiplier Parallel traversal frontier sizing multiplier.
	 * @param force_parallel Requests flat or parallel processing when backend selection allows it.
	 * @param backend Requested graph backend.
	 * @return Strategy and solve quality/profile output.
	 * @throws std::invalid_argument If the solve configuration or parameters are invalid.
	 */
	HUNLSolveOutput solve_hunl_postflop(
		const HUNLConfig& config,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma,
		std::size_t workers = 1,
		std::size_t frontier_multiplier = 8,
		bool force_parallel = false,
		HUNLBackendSelection backend = HUNLBackendSelection::Auto);

	/** @brief Checks that config is accepted by the postflop solver.
	 * @throws std::invalid_argument If the start street, board, rake or settings are unsupported.
	 */
	void validate_config(const HUNLConfig& config);
	/** @brief Checks a structured root request using the solver's admission rules.
	 * @throws std::invalid_argument If the request fails validation.
	 */
	void validate_structured_root_request(const HUNLStructuredRootRequest& request);

} // namespace texas::solver::hunl
