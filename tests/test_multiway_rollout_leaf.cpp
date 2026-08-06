#include "solver/multiway_rollout_leaf.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace {

struct RolloutFixture {
    core::MultiwayFixedState state{};
    std::array<std::array<std::uint8_t, 2>, core::kMultiwayFixedMaxSeats> holes{};
    std::array<std::uint8_t, 5> board = {0U, 5U, 10U, 0U, 0U};
    core::MultiwayRolloutScratch scratch{};
    std::array<std::uint64_t, 3> seeds = {17U, 29U, 41U};

    RolloutFixture() {
        state.seat_count = 2U;
        state.stacks[0] = 0;
        state.stacks[1] = 0;
        state.contributions[0] = 100;
        state.contributions[1] = 100;
        state.street_contributions[0] = 100;
        state.street_contributions[1] = 100;
        state.folded[0] = false;
        state.folded[1] = false;
        state.all_in[0] = true;
        state.all_in[1] = true;
        state.current_player = -1;
        state.last_aggressor = -1;
        state.current_bet = 100;
        state.last_full_raise_size = 100;
        state.big_blind = 100;
        state.street = core::Street::Flop;
        holes[0] = {1U, 2U};
        holes[1] = {3U, 4U};
    }
};

bool provide_input(const core::MultiwayLeafEvaluationRequest&, core::MultiwayRolloutInput* output,
                   const void* context) noexcept {
    const auto& fixture = *static_cast<const RolloutFixture*>(context);
    output->state = &fixture.state;
    output->holes = &fixture.holes;
    output->board = fixture.board.data();
    output->board_count = 3U;
    output->next_street_first_player = 0;
    output->odd_chip_first_seat = 0;
    return true;
}

bool unused_actions(const core::MultiwayFixedState&, const std::uint8_t*, std::uint8_t,
                    core::MultiwayRolloutActionMenu*, const void*) noexcept {
    return false;
}

core::MultiwayRolloutLeafContext make_context(RolloutFixture& fixture) {
    core::MultiwayRolloutLeafContext context;
    context.provide_input = provide_input;
    context.input_context = &fixture;
    context.provide_actions = unused_actions;
    context.seeds = fixture.seeds.data();
    context.seed_count = fixture.seeds.size();
    context.scratch = &fixture.scratch;
    context.limits.max_exact_runouts = 1'000U;
    return context;
}

}  // namespace

TEST_CASE(multiway_rollout_leaf_exact_all_in_is_deterministic_for_all_profiles) {
    RolloutFixture fixture;
    const auto context = make_context(fixture);
    core::MultiwayRolloutProfileResult first;
    core::MultiwayRolloutProfileResult second;
    const core::MultiwayLeafEvaluationRequest request = {nullptr, nullptr, 0};

    EXPECT_TRUE(core::evaluate_multiway_rollout_profiles(request, context, &first));
    EXPECT_TRUE(core::evaluate_multiway_rollout_profiles(request, context, &second));
    EXPECT_EQ(first.status, core::MultiwayRolloutStatus::Complete);
    EXPECT_EQ(first.exact_runouts, 2'970U);
    EXPECT_EQ(first.seed_count, static_cast<std::uint32_t>(fixture.seeds.size()));
    for (std::size_t profile = 0; profile < first.values.size(); ++profile) {
        EXPECT_TRUE(std::isfinite(first.values[profile]));
        EXPECT_NEAR(first.values[profile], second.values[profile], 1e-12);
        EXPECT_NEAR(first.values[profile], first.values[0], 1e-12);
    }
}

TEST_CASE(multiway_rollout_leaf_uses_finite_seeded_fallback_when_exact_runouts_are_capped) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    context.limits.max_exact_runouts = 1U;
    core::MultiwayRolloutProfileResult result;

    EXPECT_TRUE(core::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));
    EXPECT_EQ(result.status, core::MultiwayRolloutStatus::CappedFallback);
    for (const auto value : result.values) EXPECT_TRUE(std::isfinite(value));
}

TEST_CASE(multiway_rollout_leaf_rejects_invalid_caller_contexts) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    core::MultiwayRolloutProfileResult result;
    context.seed_count = 0U;
    EXPECT_TRUE(!core::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));

    context = make_context(fixture);
    fixture.board[1] = fixture.board[0];
    EXPECT_TRUE(!core::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));

    EXPECT_TRUE(!core::make_multiway_rollout_leaf_evaluator(nullptr).valid());
}

TEST_CASE(multiway_rollout_leaf_adapter_retains_leaf_callback_compatibility) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    context.selected_policy = core::MultiwayContinuationPolicyKind::RaiseBiased;
    const auto evaluator = core::make_multiway_rollout_leaf_evaluator(&context);
    const auto value = evaluator({nullptr, nullptr, 0});
    EXPECT_TRUE(evaluator.valid());
    EXPECT_TRUE(std::isfinite(value));
}
