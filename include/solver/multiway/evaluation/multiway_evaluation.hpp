#pragma once

/** @file
 * Candidate cross-play, best-response, off-tree, and AIVAT evaluation APIs.
 */

#include "games/multiway_replay.hpp"
#include "solver/multiway/engine/multiway_cfr.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	inline constexpr std::size_t kMultiwayEvaluationMaxSeats = 6U;
	inline constexpr std::size_t kMultiwayEvaluationMaxCandidates = 64U;
	inline constexpr std::size_t kMultiwayEvaluationMaxScenarios = 256U;
	inline constexpr std::uint64_t kMultiwayEvaluationMaxDuplicateDeals = 1'000'000U;
	inline constexpr std::uint32_t MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION = 1U;

	// The evaluator intentionally exchanges compact ids instead of owning policy
	// objects. Callers keep the candidate registry and callback context alive for
	// the duration of evaluate().
	/** Compact identity for a policy candidate supplied by the caller. */
#pragma pack(push, 8)
	struct MultiwayEvaluationCandidate
	{
		std::uint64_t id = 0;
	};
#pragma pack(pop)

	/** Deterministic deal identity used by a duplicate-deal evaluation sample. */
#pragma pack(push, 8)
	struct MultiwayEvaluationDeal
	{
		std::uint64_t duplicate_index = 0;
		std::uint64_t seed = 0;
	};
#pragma pack(pop)

	/** Fixed-capacity per-seat value vector and its active length. */
#pragma pack(push, 8)
	struct MultiwayEvaluationSeatValues
	{
		std::array<Value, kMultiwayEvaluationMaxSeats> values = {};
		std::uint8_t seat_count = 0;

		/** Checks the active seat count and finite value entries. */
		void validate() const;
	};
#pragma pack(pop)

	// One deterministic evaluation sample. Values and best responses use the same
	// units. The scalar measurements are the per-sample diagnostic observations.
	/** One match evaluation with profile values and quality measurements. */
#pragma pack(push, 8)
	struct MultiwayEvaluationSample
	{
		MultiwayEvaluationSeatValues profile_values{};
		MultiwayEvaluationSeatValues best_response_values{};
		std::uint64_t elapsed_nanoseconds = 0;
		std::uint64_t resident_memory_bytes = 0;
		double normalization_error = 0.0;
		double leaf_variance = 0.0;
		std::uint64_t pruned_negative_regrets = 0;
		double worker_imbalance = 0.0;

		/** Checks both seat vectors and scalar diagnostic values. */
		void validate() const;
	};
#pragma pack(pop)

	// Protected, cold-path input for an external AIVAT implementation. It is not
	// emitted in public decision logs and is never consumed by runtime traversal.
	/** Probability and estimated action value for one protected AIVAT decision. */
#pragma pack(push, 8)
	struct MultiwayAivatActionValue
	{
		MultiwayActionDescriptor action{};
		double probability = 0.0;
		Value estimated_value = 0.0;
	};
#pragma pack(pop)

	/** Sampled action, seed, and alternatives recorded for one decision. */
#pragma pack(push, 8)
	struct MultiwayAivatDecisionRecord
	{
		std::uint64_t decision_index = 0U;
		PlayerId acting_seat = -1;
		MultiwayActionDescriptor sampled_action{};
		std::uint64_t decision_seed = 0U;
		std::vector<MultiwayAivatActionValue> action_values;
	};
#pragma pack(pop)

	/** Integrity-sealed history and decision data for external AIVAT tooling. */
#pragma pack(push, 8)
	struct MultiwayAivatEvaluationRecord
	{
		std::uint32_t schema_version = MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayHandHistory public_history{};
		MultiwayEvaluationSeatValues raw_chip_outcome{};
		std::vector<MultiwayAivatDecisionRecord> decisions;
		std::uint64_t integrity_hash = 0U;

		/** Recomputes the record integrity hash. */
		void seal() noexcept;
		/** Checks schema, history, action data, and integrity hash. */
		void validate() const;
	};
#pragma pack(pop)

	/** Corrected per-seat means, uncertainty, and confidence intervals. */
#pragma pack(push, 8)
	struct MultiwayAivatEstimate
	{
		std::uint64_t samples = 0U;
		std::uint8_t seat_count = 0U;
		std::array<Value, kMultiwayEvaluationMaxSeats> means = {};
		std::array<Value, kMultiwayEvaluationMaxSeats> standard_errors = {};
		double confidence_level = 0.95;
		std::array<Value, kMultiwayEvaluationMaxSeats> confidence_interval_half_widths = {};

		/** Checks seat count, sample count, confidence, and finite values. */
		void validate() const;
	};
#pragma pack(pop)

	// Callback boundary for host-owned protected storage and external AIVAT
	// tooling. False means the host rejected the record; no estimator is invoked.
	/** Host callback that stores one validated protected evaluation record. */
	using MultiwayAivatEvaluationRecordSinkFn = bool (*)(
		const MultiwayAivatEvaluationRecord& record,
		const void* context) noexcept;

	/** Validates a record before forwarding it to the host-owned sink. */
	[[nodiscard]] bool publish_multiway_aivat_evaluation_record(
		const MultiwayAivatEvaluationRecord& record,
		MultiwayAivatEvaluationRecordSinkFn sink,
		const void* context);

	// Independent cold-path AIVAT consumer. Records are validated before use and
	// all records must share one model identity and seat count. The returned
	// means are corrected raw chip outcomes in the original value units.
	/** Estimates corrected outcomes from records sharing identity and seat count. */
	[[nodiscard]] MultiwayAivatEstimate estimate_multiway_aivat(
		const std::vector<MultiwayAivatEvaluationRecord>& records,
		double confidence_level = 0.95);

	/** Candidate ids and seat rotation for one match callback invocation. */
#pragma pack(push, 8)
	struct MultiwayEvaluationMatchRequest
	{
		const MultiwayEvaluationDeal* deal = nullptr;
		const std::uint64_t* candidate_ids_by_seat = nullptr;
		std::uint8_t seat_count = 0;
		std::uint8_t rotation = 0;
	};
#pragma pack(pop)

	// Cold-path plain callback boundary. Return false only for an expected sample
	// rejection; malformed output is rejected by the evaluator.
	/** Callback that evaluates one deal and candidate assignment. */
	using MultiwayEvaluationMatchFn = bool (*)(
		const MultiwayEvaluationMatchRequest& request,
		MultiwayEvaluationSample* output,
		const void* context) noexcept;

	/** Candidate, seat, and minimum improvement for one response scenario. */
#pragma pack(push, 8)
	struct MultiwayLocalBestResponseScenario
	{
		std::uint64_t id = 0;
		std::uint64_t candidate_id = 0;
		PlayerId seat = -1;
		Value minimum_improvement = 0.0;
	};
#pragma pack(pop)

	/** Deal and response scenario passed to a best-response callback. */
#pragma pack(push, 8)
	struct MultiwayLocalBestResponseRequest
	{
		MultiwayEvaluationDeal deal{};
		MultiwayLocalBestResponseScenario scenario{};
	};
#pragma pack(pop)

	/** Profile and response values returned for one best-response sample. */
#pragma pack(push, 8)
	struct MultiwayLocalBestResponseResult
	{
		Value profile_value = 0.0;
		Value response_value = 0.0;
	};
#pragma pack(pop)

	/** Callback that measures one local best-response scenario. */
	using MultiwayLocalBestResponseFn = bool (*)(
		const MultiwayLocalBestResponseRequest& request,
		MultiwayLocalBestResponseResult* output,
		const void* context) noexcept;

	/** Public-state and observed-action input for one off-tree check. */
#pragma pack(push, 8)
	struct MultiwayOffTreeGauntlet
	{
		std::uint64_t id = 0;
		MultiwayPublicStateDescriptor public_state{};
		MultiwayActionDescriptor observed_action{};
	};
#pragma pack(pop)

	/** Sampled policy action and normalization from an off-tree callback. */
#pragma pack(push, 8)
	struct MultiwayOffTreeGauntletResult
	{
		MultiwayActionDescriptor sampled_action{};
		bool has_sampled_action = false;
		double policy_total = 0.0;
	};
#pragma pack(pop)

	/** Callback that evaluates a policy at one off-tree public state. */
	using MultiwayOffTreeGauntletFn = bool (*)(
		const MultiwayOffTreeGauntlet& request,
		MultiwayOffTreeGauntletResult* output,
		const void* context) noexcept;

	/** Failure category retained when an evaluation check rejects a sample. */
	enum class MultiwayEvaluationFailure : std::uint8_t
	{
		None,
		InvalidConfiguration,
		MatchCallbackRejected,
		InvalidSample,
		InvalidConfidence,
		LocalBestResponseRejected,
		LocalBestResponseRegression,
		OffTreeCallbackRejected,
		OffTreeObservedActionMissing,
		OffTreeIllegalAction,
		OffTreeNonNormalizedPolicy,
	};

	/** Aggregate uncertainty, timing, memory, and stability measurements. */
#pragma pack(push, 8)
	struct MultiwayEvaluationMetrics
	{
		std::uint64_t samples = 0;
		double confidence_level = 0.95;
		double confidence_interval_half_width = 0.0;
		double mean_elapsed_nanoseconds = 0.0;
		std::uint64_t peak_resident_memory_bytes = 0;
		double max_normalization_error = 0.0;
		double mean_leaf_variance = 0.0;
		std::uint64_t total_pruned_negative_regrets = 0;
		double max_worker_imbalance = 0.0;
	};
#pragma pack(pop)

	/** Mean focal-candidate value against one shared opponent candidate. */
#pragma pack(push, 8)
	struct MultiwayCrossPlayCell
	{
		std::uint64_t focal_candidate_id = 0;
		std::uint64_t opponent_candidate_id = 0;
		std::uint64_t samples = 0;
		Value mean_focal_value = 0.0;
	};
#pragma pack(pop)

	/** Sample count and threshold outcome for one response scenario. */
#pragma pack(push, 8)
	struct MultiwayLocalBestResponseReport
	{
		std::uint64_t scenario_id = 0;
		std::uint64_t samples = 0;
		Value mean_improvement = 0.0;
		bool passed = false;
	};
#pragma pack(pop)

	/** Pass/failure result for one off-tree legality and policy check. */
#pragma pack(push, 8)
	struct MultiwayOffTreeGauntletReport
	{
		std::uint64_t scenario_id = 0;
		bool passed = false;
		MultiwayEvaluationFailure failure = MultiwayEvaluationFailure::None;
	};
#pragma pack(pop)

	/** Samples, candidates, checks, units, and callbacks for evaluation. */
#pragma pack(push, 8)
	struct MultiwayEvaluationConfig
	{
		std::uint8_t seat_count = 6;
		std::uint64_t duplicate_deals = 1;
		std::uint64_t seed = 1;
		double confidence_level = 0.95;
		MultiwayMetricMethod metric_method = MultiwayMetricMethod::SampledEstimate;
		MultiwayValueUnits value_units = MultiwayValueUnits::BigBlinds;
		std::vector<MultiwayEvaluationCandidate> candidates;
		std::vector<MultiwayLocalBestResponseScenario> local_best_response_scenarios;
		std::vector<MultiwayOffTreeGauntlet> off_tree_gauntlets;
		MultiwayEvaluationMatchFn evaluate_match = nullptr;
		MultiwayLocalBestResponseFn evaluate_local_best_response = nullptr;
		MultiwayOffTreeGauntletFn evaluate_off_tree = nullptr;
		const void* callback_context = nullptr;

		/** Checks bounds, required callbacks, and scenario identities. */
		void validate() const;
	};
#pragma pack(pop)

	/** Cross-play matrices, reduced-game quality, reports, and failures. */
#pragma pack(push, 8)
	struct MultiwayEvaluationResult
	{
		std::vector<MultiwayCrossPlayCell> cross_play;
		MultiwayNashConv reduced_game_nash_conv{};
		std::vector<MultiwayLocalBestResponseReport> local_best_response;
		std::vector<MultiwayOffTreeGauntletReport> off_tree_gauntlets;
		MultiwayEvaluationMetrics metrics{};
		std::vector<MultiwayEvaluationFailure> failures;

		/** Returns true when no validation or evaluation check failed. */
		[[nodiscard]] bool passed() const noexcept { return failures.empty(); }
	};
#pragma pack(pop)

	// Runs every duplicate deal under every cyclic seat rotation. Cross-play cells
	// use one focal candidate and one shared opposing candidate, which makes the
	// matrix stable for two through six players without inventing team semantics.
	/** Evaluates candidate policies over cross-play and configured checks. */
	[[nodiscard]] MultiwayEvaluationResult evaluate_multiway_candidates(
		const MultiwayEvaluationConfig& config);

} // namespace texas::solver::multiway
