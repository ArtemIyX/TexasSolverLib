#pragma once

#include "games/multiway_fixed.hpp"
#include "solver/multiway_continuation_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

// Bounded continuation leaf evaluation. All storage is caller-owned so each
// worker can retain one context and scratch instance without shared mutation.
inline constexpr std::size_t MULTIWAY_ROLLOUT_MAX_ACTIONS = 8U;
inline constexpr std::size_t MULTIWAY_ROLLOUT_MAX_BOARD_CARDS = 5U;
inline constexpr std::size_t MULTIWAY_ROLLOUT_DECK_CARDS = 52U;

enum class MultiwayRolloutStatus : std::uint8_t {
    Complete,
    CappedFallback,
    InvalidContext,
};

struct MultiwayRolloutLimits {
    std::uint32_t max_betting_actions = 64U;
    // At most this many exact all-in runouts are enumerated. Larger runout
    // spaces use the supplied common-random-number seeds instead.
    std::uint32_t max_exact_runouts = 1'200U;
};

struct MultiwayRolloutActionMenu {
    std::array<MultiwayActionDescriptor, MULTIWAY_ROLLOUT_MAX_ACTIONS> actions{};
    std::array<Probability, MULTIWAY_ROLLOUT_MAX_ACTIONS> blueprint{};
    std::uint8_t count = 0;
};

// The provider returns the blueprint menu at one exact betting state. It must
// only return legal, unique actions and must not retain `board`.
using MultiwayRolloutActionProviderFn = bool (*) (
    const MultiwayFixedState& state,
    const std::uint8_t* board,
    std::uint8_t board_count,
    MultiwayRolloutActionMenu* output,
    const void* context) noexcept;

// Converts the existing dynamic leaf request to a fixed continuation state.
// Holes are ordered by seat and include folded seats to preserve settlement.
struct MultiwayRolloutInput {
    const MultiwayFixedState* state = nullptr;
    const std::array<std::array<std::uint8_t, 2>, kMultiwayFixedMaxSeats>* holes = nullptr;
    const std::uint8_t* board = nullptr;
    std::uint8_t board_count = 0;
    // First active seat on future streets, usually supplied from the public
    // hand state rather than inferred from a lossy betting snapshot.
    PlayerId next_street_first_player = 0;
    PlayerId odd_chip_first_seat = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
};

using MultiwayRolloutInputProviderFn = bool (*) (
    const MultiwayLeafEvaluationRequest& request,
    MultiwayRolloutInput* output,
    const void* context) noexcept;

struct MultiwayRolloutScratch {
    MultiwayFixedState state{};
    std::array<std::array<std::uint8_t, 2>, kMultiwayFixedMaxSeats> holes{};
    std::array<std::uint8_t, MULTIWAY_ROLLOUT_MAX_BOARD_CARDS> board{};
    std::array<bool, 64U> used{};
    MultiwayFixedTerminalScratch terminal_scratch{};
    MultiwayFixedTerminalResult terminal_result{};
    std::array<Probability, MULTIWAY_ROLLOUT_MAX_ACTIONS> policy{};
};

struct MultiwayRolloutProfileResult {
    std::array<Value, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
    MultiwayRolloutStatus status = MultiwayRolloutStatus::InvalidContext;
    std::uint32_t seed_count = 0;
    std::uint32_t exact_runouts = 0;
    std::uint32_t betting_actions = 0;
};

struct MultiwayRolloutLeafContext {
    MultiwayRolloutInputProviderFn provide_input = nullptr;
    const void* input_context = nullptr;
    MultiwayRolloutActionProviderFn provide_actions = nullptr;
    const void* action_context = nullptr;
    const std::uint64_t* seeds = nullptr;
    std::size_t seed_count = 0;
    MultiwayRolloutLimits limits{};
    Probability bias_factor = 5.0;
    MultiwayContinuationPolicyKind selected_policy = MultiwayContinuationPolicyKind::Blueprint;
    // One scratch instance per concurrent caller. It is overwritten during
    // evaluation and must outlive the synchronous evaluator invocation.
    MultiwayRolloutScratch* scratch = nullptr;
};

// Evaluates every fixed policy with the same seed batch. Complete all-in
// runouts are exactly enumerated whenever their bounded space fits the cap.
[[nodiscard]] bool evaluate_multiway_rollout_profiles(
    const MultiwayLeafEvaluationRequest& request,
    const MultiwayRolloutLeafContext& context,
    MultiwayRolloutProfileResult* output) noexcept;

// Adapter retaining the existing MultiwayLeafEvaluator callback boundary.
// It returns NaN for invalid contexts and otherwise selects `policy` from the
// common-random-number profile result.
[[nodiscard]] Value evaluate_multiway_rollout_leaf(
    const MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept;
[[nodiscard]] MultiwayLeafEvaluator make_multiway_rollout_leaf_evaluator(
    const MultiwayRolloutLeafContext* context) noexcept;

}  // namespace core
