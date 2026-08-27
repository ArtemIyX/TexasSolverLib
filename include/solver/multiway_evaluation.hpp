#pragma once

#include "games/multiway_replay.hpp"
#include "solver/multiway_cfr.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::size_t kMultiwayEvaluationMaxSeats = 6U;
inline constexpr std::size_t kMultiwayEvaluationMaxCandidates = 64U;
inline constexpr std::size_t kMultiwayEvaluationMaxScenarios = 256U;
inline constexpr std::uint64_t kMultiwayEvaluationMaxDuplicateDeals = 1'000'000U;
inline constexpr std::uint32_t MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION = 1U;

// The evaluator intentionally exchanges compact ids instead of owning policy
// objects. Callers keep the candidate registry and callback context alive for
// the duration of evaluate().
struct MultiwayEvaluationCandidate {
    std::uint64_t id = 0;
};

struct MultiwayEvaluationDeal {
    std::uint64_t duplicate_index = 0;
    std::uint64_t seed = 0;
};

struct MultiwayEvaluationSeatValues {
    std::array<Value, kMultiwayEvaluationMaxSeats> values = {};
    std::uint8_t seat_count = 0;

    void validate() const;
};

// One deterministic evaluation sample. Values and best responses use the same
// units. The scalar measurements are the per-sample diagnostic observations.
struct MultiwayEvaluationSample {
    MultiwayEvaluationSeatValues profile_values{};
    MultiwayEvaluationSeatValues best_response_values{};
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t resident_memory_bytes = 0;
    double normalization_error = 0.0;
    double leaf_variance = 0.0;
    std::uint64_t pruned_negative_regrets = 0;
    double worker_imbalance = 0.0;

    void validate() const;
};

// Protected, cold-path input for an external AIVAT implementation. It is not
// emitted in public decision logs and is never consumed by runtime traversal.
struct MultiwayAivatActionValue {
    MultiwayActionDescriptor action{};
    double probability = 0.0;
    Value estimated_value = 0.0;
};

struct MultiwayAivatDecisionRecord {
    std::uint64_t decision_index = 0U;
    PlayerId acting_seat = -1;
    MultiwayActionDescriptor sampled_action{};
    std::uint64_t decision_seed = 0U;
    std::vector<MultiwayAivatActionValue> action_values;
};

struct MultiwayAivatEvaluationRecord {
    std::uint32_t schema_version = MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION;
    MultiwayModelIdentity identity{};
    MultiwayHandHistory public_history{};
    MultiwayEvaluationSeatValues raw_chip_outcome{};
    std::vector<MultiwayAivatDecisionRecord> decisions;
    std::uint64_t integrity_hash = 0U;

    void seal() noexcept;
    void validate() const;
};

struct MultiwayAivatEstimate {
    std::uint64_t samples = 0U;
    std::uint8_t seat_count = 0U;
    std::array<Value, kMultiwayEvaluationMaxSeats> means = {};
    std::array<Value, kMultiwayEvaluationMaxSeats> standard_errors = {};
    double confidence_level = 0.95;
    std::array<Value, kMultiwayEvaluationMaxSeats> confidence_interval_half_widths = {};

    void validate() const;
};

// Callback boundary for host-owned protected storage and external AIVAT
// tooling. False means the host rejected the record; no estimator is invoked.
using MultiwayAivatEvaluationRecordSinkFn = bool (*) (
    const MultiwayAivatEvaluationRecord& record,
    const void* context) noexcept;

[[nodiscard]] bool publish_multiway_aivat_evaluation_record(
    const MultiwayAivatEvaluationRecord& record,
    MultiwayAivatEvaluationRecordSinkFn sink,
    const void* context);

// Independent cold-path AIVAT consumer. Records are validated before use and
// all records must share one model identity and seat count. The returned
// means are corrected raw chip outcomes in the original value units.
[[nodiscard]] MultiwayAivatEstimate estimate_multiway_aivat(
    const std::vector<MultiwayAivatEvaluationRecord>& records,
    double confidence_level = 0.95);

struct MultiwayEvaluationMatchRequest {
    const MultiwayEvaluationDeal* deal = nullptr;
    const std::uint64_t* candidate_ids_by_seat = nullptr;
    std::uint8_t seat_count = 0;
    std::uint8_t rotation = 0;
};

// Cold-path plain callback boundary. Return false only for an expected sample
// rejection; malformed output is rejected by the evaluator.
using MultiwayEvaluationMatchFn = bool (*) (
    const MultiwayEvaluationMatchRequest& request,
    MultiwayEvaluationSample* output,
    const void* context) noexcept;

struct MultiwayLocalBestResponseScenario {
    std::uint64_t id = 0;
    std::uint64_t candidate_id = 0;
    PlayerId seat = -1;
    Value minimum_improvement = 0.0;
};

struct MultiwayLocalBestResponseRequest {
    MultiwayEvaluationDeal deal{};
    MultiwayLocalBestResponseScenario scenario{};
};

struct MultiwayLocalBestResponseResult {
    Value profile_value = 0.0;
    Value response_value = 0.0;
};

using MultiwayLocalBestResponseFn = bool (*) (
    const MultiwayLocalBestResponseRequest& request,
    MultiwayLocalBestResponseResult* output,
    const void* context) noexcept;

struct MultiwayOffTreeGauntlet {
    std::uint64_t id = 0;
    MultiwayPublicStateDescriptor public_state{};
    MultiwayActionDescriptor observed_action{};
};

struct MultiwayOffTreeGauntletResult {
    MultiwayActionDescriptor sampled_action{};
    bool has_sampled_action = false;
    double policy_total = 0.0;
};

using MultiwayOffTreeGauntletFn = bool (*) (
    const MultiwayOffTreeGauntlet& request,
    MultiwayOffTreeGauntletResult* output,
    const void* context) noexcept;

enum class MultiwayEvaluationFailure : std::uint8_t {
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

struct MultiwayEvaluationMetrics {
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

struct MultiwayCrossPlayCell {
    std::uint64_t focal_candidate_id = 0;
    std::uint64_t opponent_candidate_id = 0;
    std::uint64_t samples = 0;
    Value mean_focal_value = 0.0;
};

struct MultiwayLocalBestResponseReport {
    std::uint64_t scenario_id = 0;
    std::uint64_t samples = 0;
    Value mean_improvement = 0.0;
    bool passed = false;
};

struct MultiwayOffTreeGauntletReport {
    std::uint64_t scenario_id = 0;
    bool passed = false;
    MultiwayEvaluationFailure failure = MultiwayEvaluationFailure::None;
};

struct MultiwayEvaluationConfig {
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

    void validate() const;
};

struct MultiwayEvaluationResult {
    std::vector<MultiwayCrossPlayCell> cross_play;
    MultiwayNashConv reduced_game_nash_conv{};
    std::vector<MultiwayLocalBestResponseReport> local_best_response;
    std::vector<MultiwayOffTreeGauntletReport> off_tree_gauntlets;
    MultiwayEvaluationMetrics metrics{};
    std::vector<MultiwayEvaluationFailure> failures;

    [[nodiscard]] bool passed() const noexcept { return failures.empty(); }
};

// Runs every duplicate deal under every cyclic seat rotation. Cross-play cells
// use one focal candidate and one shared opposing candidate, which makes the
// matrix stable for two through six players without inventing team semantics.
[[nodiscard]] MultiwayEvaluationResult evaluate_multiway_candidates(
    const MultiwayEvaluationConfig& config);

}  // namespace texas::solver::multiway
