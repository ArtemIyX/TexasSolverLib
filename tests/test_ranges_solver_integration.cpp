#include "games/hunl_flat_graph.hpp"
#include "games/hunl_solver.hpp"
#include "core/lib.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_sampled_solver.hpp"
#include "solver/hunl_sampled_range.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {

core::HUNLRangeInput single_hand_range(std::uint8_t first, std::uint8_t second, double weight = 1.0) {
    core::HUNLRangeInput range;
    range.hand_weights.push_back({{first, second}, weight});
    return range;
}

core::HUNLConfig range_contract_config() {
    auto config = core::default_tiny_subgame();
    config.initial_hole_cards = std::nullopt;
    config.initial_ranges[0] = single_hand_range(core::card_to_int(14, 1), core::card_to_int(13, 3));
    config.initial_ranges[1] = single_hand_range(core::card_to_int(12, 1), core::card_to_int(11, 3));
    config.range_policy = core::HUNLRangePolicy::RequireExplicit;
    return config;
}

struct InjectedLeafContext {
    std::uint32_t calls = 0;
    bool received_public_state = true;
    bool received_private_deal = true;
};

bool injected_zero_sum_leaf(
    void* raw_context,
    const core::HUNLLeafEvaluationRequest* requests,
    core::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    auto& context = *static_cast<InjectedLeafContext*>(raw_context);
    for (std::size_t index = 0; index < count; ++index) {
        ++context.calls;
        context.received_public_state = context.received_public_state &&
            !requests[index].public_state.hole_cards.has_value() &&
            requests[index].bucket_reach[0].size() == 1U &&
            requests[index].bucket_reach[1].size() == 1U;
        context.received_private_deal = context.received_private_deal &&
            core::are_valid_and_distinct_cards(requests[index].private_hole_cards[0].data(), 2U) &&
            core::are_valid_and_distinct_cards(requests[index].private_hole_cards[1].data(), 2U) &&
            requests[index].scope == core::HUNLLeafEvaluationScope::DealConditional;
        results[index].values = {0.0, 0.0};
        results[index].units = requests[index].units;
    }
    return true;
}

bool rejecting_leaf(
    void*,
    const core::HUNLLeafEvaluationRequest*,
    core::HUNLLeafEvaluationResult*,
    std::size_t) {
    return false;
}

bool mismatched_unit_leaf(
    void*,
    const core::HUNLLeafEvaluationRequest*,
    core::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        results[index].values = {0.0, 0.0};
        results[index].units = core::HUNLLeafValueUnits::BigBlinds;
    }
    return true;
}

struct DeadlineAwareLeafContext {
    bool received_deadline = false;
};

bool deadline_aware_slow_leaf(
    void* raw_context,
    const core::HUNLLeafEvaluationRequest* requests,
    core::HUNLLeafEvaluationResult*,
    std::size_t count) {
    auto& context = *static_cast<DeadlineAwareLeafContext*>(raw_context);
    if (count != 1U || !requests[0].deadline.has_value()) return false;
    context.received_deadline = true;
    while (std::chrono::steady_clock::now() < *requests[0].deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

}  // namespace

TEST_CASE(ranges_explicit_hand_contract_accepts_no_range_fields) {
    const auto config = core::default_tiny_subgame();
    config.validate();
    core::validate_config(config);
    EXPECT_EQ(core::resolve_range_policy(config), core::HUNLRangePolicy::Uniform);
}

TEST_CASE(ranges_unspecified_policy_with_complete_root_ranges_selects_range_contract) {
    auto config = range_contract_config();
    config.range_policy = core::HUNLRangePolicy::Unspecified;

    config.validate();
    EXPECT_EQ(core::resolve_range_policy(config), core::HUNLRangePolicy::UseInitialRanges);
}

TEST_CASE(ranges_require_explicit_requires_player_zero_range) {
    auto config = range_contract_config();
    config.initial_ranges[0] = std::nullopt;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_require_explicit_requires_player_one_range) {
    auto config = range_contract_config();
    config.initial_ranges[1] = std::nullopt;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_use_initial_ranges_requires_both_players) {
    auto config = range_contract_config();
    config.range_policy = core::HUNLRangePolicy::UseInitialRanges;
    config.initial_ranges[1] = std::nullopt;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_range_contract_rejects_fixed_private_cards) {
    auto config = range_contract_config();
    config.initial_hole_cards = core::default_tiny_subgame().initial_hole_cards;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_joint_normalization_conditions_on_cross_player_blockers) {
    auto config = range_contract_config();
    const auto ace_spades = core::card_to_int(14, 3);
    config.initial_ranges[0]->hand_weights.push_back(
        {{{ace_spades, core::card_to_int(2, 0)}}, 3.0});
    config.initial_ranges[1]->hand_weights.push_back(
        {{{ace_spades, core::card_to_int(3, 0)}}, 9.0});
    const auto deals = core::normalize_hunl_joint_range(config);
    double total = 0.0;
    for (const auto& deal : deals) {
        total += deal.weight;
        EXPECT_TRUE(core::are_valid_and_distinct_cards(deal.hole[0].data(), 2));
        const std::array<std::uint8_t, 4> all = {
            deal.hole[0][0], deal.hole[0][1], deal.hole[1][0], deal.hole[1][1]};
        EXPECT_TRUE(core::are_valid_and_distinct_cards(all.data(), all.size()));
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST_CASE(ranges_joint_normalization_rejects_fully_blocked_cross_ranges) {
    auto config = range_contract_config();
    const auto first = core::card_to_int(11, 0);
    const auto second = core::card_to_int(10, 0);
    config.initial_ranges[0] = single_hand_range(first, second);
    config.initial_ranges[1] = single_hand_range(second, first);
    EXPECT_THROW(core::normalize_hunl_joint_range(config), std::invalid_argument);
}

TEST_CASE(ranges_structured_root_request_validates_versions_units_and_joint_reach) {
    core::HUNLStructuredRootRequest request;
    request.config = range_contract_config();
    request.blueprint_version = "blueprint-v1";
    request.model_version = "value-v1";
    request.value_units = core::HUNLLeafValueUnits::BigBlinds;
    request.validate();
    EXPECT_NEAR(request.normalized_joint_range().front().weight, 1.0, 1e-12);
    request.blueprint_version.clear();
    EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST_CASE(ranges_structured_root_rejects_unknown_units_and_anonymous_depth_leaf_model) {
    core::HUNLStructuredRootRequest request;
    request.config = range_contract_config();
    request.blueprint_version = "blueprint-v1";
    request.value_units = static_cast<core::HUNLLeafValueUnits>(99);
    EXPECT_THROW(request.validate(), std::invalid_argument);

    request.value_units = core::HUNLLeafValueUnits::Chips;
    request.config.depth_limit_plies = 1U;
    EXPECT_THROW(request.validate(), std::invalid_argument);
    request.model_version = "leaf-v1";
    request.validate();
}

TEST_CASE(ranges_structured_root_validation_rejects_blocked_and_accepts_compatible_deals) {
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        core::HUNLStructuredRootRequest request;
        request.config = range_contract_config();
        request.blueprint_version = "blueprint-v1";
        request.model_version = "value-v1";
        const auto first = core::card_to_int(rank, 0);
        const auto second = core::card_to_int(rank == 14 ? 2 : rank + 1, 1);
        request.config.initial_ranges[0] = single_hand_range(first, second);
        request.config.initial_ranges[1] = single_hand_range(second, first);
        EXPECT_THROW(request.validate(), std::invalid_argument);
        request.config = range_contract_config();
        request.validate();
    }
}

TEST_CASE(ranges_structured_root_sampled_positive_work_uses_private_range_trajectories) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    config.max_cached_public_states = 1024;
    config.seed = 0xB10EULL;
    const auto result = core::lib::solve_hunl_postflop_sampled(root, config, 1);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_EQ(result.profile.traversals, 1U);
    EXPECT_TRUE(!result.root_strategy.actions.empty());
}

TEST_CASE(ranges_structured_workers_merge_the_same_deterministic_trajectory_batch) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    core::HUNLSampledSolverConfig serial_config;
    serial_config.minibatch_size = 4U;
    serial_config.workers = 1U;
    serial_config.seed = 0xC0FFEEULL;
    core::HUNLSampledStorage serial_storage;
    core::HUNLSampledProfile serial_profile;
    core::HUNLSampledRangeSession serial(root, serial_config, serial_storage, serial_profile);
    const auto serial_result = serial.resume_batches(1U);

    auto parallel_config = serial_config;
    parallel_config.workers = 2U;
    core::HUNLSampledStorage parallel_storage;
    core::HUNLSampledProfile parallel_profile;
    core::HUNLSampledRangeSession parallel(root, parallel_config, parallel_storage, parallel_profile);
    const auto parallel_result = parallel.resume_batches(1U);

    EXPECT_EQ(serial_result.batches_completed, 1U);
    EXPECT_EQ(parallel_result.batches_completed, 1U);
    EXPECT_EQ(serial_profile.snapshot().traversals, 4U);
    EXPECT_EQ(parallel_profile.snapshot().traversals, 4U);
    EXPECT_EQ(serial_storage.row_count(), parallel_storage.row_count());
    for (std::size_t row_index = 0; row_index < serial_storage.row_count(); ++row_index) {
        const auto serial_row = serial_storage.view(serial_storage.meta()[row_index].id);
        const auto parallel_row = parallel_storage.view(parallel_storage.meta()[row_index].id);
        EXPECT_EQ(serial_row.value_count(), parallel_row.value_count());
        for (std::size_t value = 0; value < serial_row.value_count(); ++value) {
            EXPECT_NEAR(serial_row.regret[value], parallel_row.regret[value], 1e-6);
            EXPECT_NEAR(serial_row.strategy_sum[value], parallel_row.strategy_sum[value], 1e-6);
        }
    }
}

TEST_CASE(ranges_single_joint_deal_matches_fixed_private_hand_sampled_oracle) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    root.value_units = core::HUNLLeafValueUnits::BigBlinds;
    const auto deal = root.normalized_joint_range().front();

    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    config.seed = 0x5EEDULL;

    core::HUNLSampledSolver range_solver(config);
    core::HUNLSampledSolveRequest range_request;
    range_request.structured_root = root;
    const auto range_result = range_solver.run_batches(range_request, 1U);

    auto fixed_config = root.config;
    fixed_config.initial_ranges = {std::nullopt, std::nullopt};
    fixed_config.range_policy = core::HUNLRangePolicy::Uniform;
    fixed_config.initial_hole_cards = deal.hole;
    const auto fixed_root = core::HUNLState::initial(
        std::make_shared<const core::HUNLConfig>(fixed_config));
    core::HUNLSampledSolver fixed_solver(config);
    core::HUNLSampledSolveRequest fixed_request;
    fixed_request.root_state = fixed_root;
    const auto fixed_result = fixed_solver.run_batches(fixed_request, 1U);

    EXPECT_EQ(range_result.batches_completed, fixed_result.batches_completed);
    EXPECT_EQ(range_solver.storage().row_count(), fixed_solver.storage().row_count());
    for (std::size_t row_index = 0; row_index < range_solver.storage().row_count(); ++row_index) {
        const auto range_id = range_solver.storage().meta()[row_index].id;
        const auto fixed_id = fixed_solver.storage().meta()[row_index].id;
        const auto range_row = range_solver.storage().view(range_id);
        const auto fixed_row = fixed_solver.storage().view(fixed_id);
        EXPECT_EQ(range_row.action_count, fixed_row.action_count);
        EXPECT_EQ(range_row.bucket_count, fixed_row.bucket_count);
        for (std::size_t value = 0; value < range_row.value_count(); ++value) {
            EXPECT_NEAR(range_row.regret[value], fixed_row.regret[value], 1e-6);
            EXPECT_NEAR(range_row.strategy_sum[value], fixed_row.strategy_sum[value], 1e-6);
        }
    }
}

TEST_CASE(ranges_structured_session_resumes_without_replaying_batch_ids) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto first = session.resume_batches(1);
    const auto second = session.resume_batches(1);
    EXPECT_EQ(first.batches_completed, 1U);
    EXPECT_EQ(second.batches_completed, 1U);
    EXPECT_EQ(session.next_batch(), 2U);
    EXPECT_EQ(profile.snapshot().traversals, 2U);
}

TEST_CASE(ranges_structured_session_deadline_preserves_the_next_unpublished_batch) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto expired = session.resume_batches(
        4U, std::chrono::steady_clock::now() - std::chrono::milliseconds{1});
    EXPECT_EQ(expired.batches_completed, 0U);
    EXPECT_TRUE(expired.timed_out);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_deadline_cancels_a_cooperative_slow_leaf_before_publication) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1U;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1U;
    DeadlineAwareLeafContext context;
    const core::HUNLLeafEvaluator evaluator{&context, deadline_aware_slow_leaf};
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    const auto result = session.resume_batches(
        1U, std::chrono::steady_clock::now() + std::chrono::milliseconds{20});
    EXPECT_TRUE(context.received_deadline);
    EXPECT_TRUE(result.timed_out);
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_session_rejects_exhausted_batch_identity_before_replay) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(
        root, config, storage, profile, std::numeric_limits<std::uint64_t>::max());

    EXPECT_THROW(session.resume_batches(1U), std::overflow_error);
    EXPECT_EQ(session.next_batch(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_sampled_solver_retains_structured_deadline_cursor_between_resumes) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    core::HUNLSampledSolver solver(config);

    solver.begin_structured_session(root);
    EXPECT_TRUE(solver.has_structured_session());
    const auto first = solver.resume_structured_batches(1U);
    const auto second = solver.resume_structured_batches(1U);
    EXPECT_EQ(first.batches_completed, 1U);
    EXPECT_EQ(second.batches_completed, 1U);
    EXPECT_EQ(second.profile.traversals, 2U);
}

TEST_CASE(ranges_structured_depth_limit_uses_typed_injected_leaf_evaluator) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";

    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    InjectedLeafContext context;
    const core::HUNLLeafEvaluator evaluator{&context, injected_zero_sum_leaf};
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    const auto result = session.resume_batches(1);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_TRUE(context.calls > 0U);
    EXPECT_TRUE(context.received_public_state);
    EXPECT_TRUE(context.received_private_deal);
}

TEST_CASE(ranges_structured_depth_limit_rejects_missing_typed_leaf_evaluator) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolverConfig config;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;

    EXPECT_THROW(core::HUNLSampledRangeSession(root, config, storage, profile), std::invalid_argument);
}

TEST_CASE(ranges_structured_facade_passes_typed_leaf_evaluator_to_positive_work) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    InjectedLeafContext context;
    const core::HUNLLeafEvaluator evaluator{&context, injected_zero_sum_leaf};

    const auto result = core::lib::solve_hunl_postflop_sampled(root, config, 1U, &evaluator);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_TRUE(context.calls > 0U);
}

TEST_CASE(ranges_structured_solver_session_rejects_missing_leaf_before_it_is_retained) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolver solver;

    EXPECT_THROW(solver.begin_structured_session(root), std::invalid_argument);
    EXPECT_TRUE(!solver.has_structured_session());
}

TEST_CASE(ranges_structured_leaf_callback_failure_aborts_the_current_batch) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    const core::HUNLLeafEvaluator evaluator{nullptr, rejecting_leaf};
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    EXPECT_THROW(session.resume_batches(1U), std::runtime_error);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_leaf_callback_rejects_a_value_unit_mismatch) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    const core::HUNLLeafEvaluator evaluator{nullptr, mismatched_unit_leaf};
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    EXPECT_THROW(session.resume_batches(1U), std::runtime_error);
    EXPECT_EQ(session.next_batch(), 0U);
}

TEST_CASE(ranges_structured_zero_batch_session_exports_typed_uniform_root_actions) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    core::HUNLSampledSolverConfig config;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto result = session.resume_batches(0U);
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_TRUE(!result.root_strategy.actions.empty());
    double probability_sum = 0.0;
    for (const auto& action : result.root_strategy.actions) {
        EXPECT_TRUE(action.action_id >= core::ACTION_FOLD);
        EXPECT_TRUE(action.action_menu_id != 0U);
        probability_sum += action.probability;
    }
    EXPECT_NEAR(probability_sum, 1.0, 1e-12);
}

TEST_CASE(ranges_structured_root_allows_low_spr_balanced_pots_but_requires_a_live_snapshot_for_facing_bets) {
    core::HUNLStructuredRootRequest request;
    request.config = range_contract_config();
    request.config.starting_stack = 100;
    request.config.initial_pot = 1000;
    request.config.initial_contributions = {500, 500};
    request.blueprint_version = "blueprint-v1";
    request.validate();

    request.config.initial_contributions = {400, 600};
    EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST_CASE(ranges_structured_live_root_accepts_an_off_tree_facing_bet_and_preserves_the_actor) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    auto config = std::make_shared<const core::HUNLConfig>(root.config);
    core::HUNLLiveRootSnapshot live;
    live.public_state.board = root.config.initial_board;
    live.public_state.street = root.config.starting_street;
    live.public_state.contributions = {500, 700};
    live.public_state.stacks = {500, 300};
    live.public_state.street_aggressor = 1;
    live.public_state.street_num_raises = 1U;
    live.public_state.to_call = 200;
    live.public_state.cur_player = 0;
    live.public_state.config = config;
    // 200 is not in the configured 33/75/100/150/200% opening menu at this pot.
    live.public_state.current_street_tokens = {"b200"};
    live.public_state.current_street_history_codes = {-200};
    live.canonical_public_history = live.public_state.format_history();
    live.legal_actions = live.public_state.legal_actions();
    live.state_version = "live-state-v1";
    root.live_root = live;

    root.validate();
    const auto public_root = root.public_root_state(config);
    EXPECT_EQ(public_root.current_player(), 0);
    EXPECT_EQ(public_root.to_call, 200);
    EXPECT_EQ(public_root.contributions[0], 500);
    EXPECT_EQ(public_root.contributions[1], 700);
    EXPECT_TRUE(std::find(live.legal_actions.begin(), live.legal_actions.end(), core::ACTION_FOLD) !=
                live.legal_actions.end());
    EXPECT_TRUE(std::find(live.legal_actions.begin(), live.legal_actions.end(), core::ACTION_CALL) !=
                live.legal_actions.end());

    core::HUNLSampledSolverConfig solver_config;
    solver_config.minibatch_size = 1U;
    core::HUNLSampledStorage storage;
    core::HUNLSampledProfile profile;
    core::HUNLSampledRangeSession session(root, solver_config, storage, profile);
    const auto result = session.resume_batches(1U);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_EQ(result.root_strategy.actions[0].action_id, core::ACTION_FOLD);
    EXPECT_EQ(result.root_strategy.actions[1].action_id, core::ACTION_CALL);
}

TEST_CASE(ranges_sampled_request_rejects_ambiguous_fixed_and_range_roots) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";

    core::HUNLSampledSolveRequest request;
    request.root_state = core::HUNLState::initial(
        std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame()));
    request.structured_root = root;

    core::HUNLSampledSolver solver;
    EXPECT_THROW(solver.run_batches(request, 0), std::invalid_argument);
}

TEST_CASE(ranges_uniform_policy_rejects_initial_ranges) {
    auto config = core::default_tiny_subgame();
    config.initial_ranges[0] = single_hand_range(core::card_to_int(2, 0), core::card_to_int(3, 1));
    config.range_policy = core::HUNLRangePolicy::Uniform;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_legacy_player_ranges_are_rejected_by_config_validation) {
    auto config = core::default_tiny_subgame();
    config.player_ranges[0] = single_hand_range(core::card_to_int(2, 0), core::card_to_int(3, 1));

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_recursive_postflop_entrypoint_rejects_range_contract_before_solving) {
    const auto config = range_contract_config();

    EXPECT_THROW(core::solve_hunl_postflop(config, 0, 1.5, 0.0, 2.0, 1, 8, false), std::invalid_argument);
}

TEST_CASE(ranges_flat_postflop_entrypoint_rejects_range_contract_before_solving) {
    auto config = range_contract_config();
    config.starting_street = core::Street::Turn;
    config.initial_board.pop_back();

    EXPECT_THROW(core::solve_hunl_postflop(config, 0, 1.5, 0.0, 2.0, 2, 8, true), std::invalid_argument);
}

TEST_CASE(ranges_direct_flat_backend_rejects_legacy_bucket_priors) {
    auto valid = core::default_tiny_subgame();
    auto graph = core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(valid));
    valid.player_ranges[0] = single_hand_range(core::card_to_int(2, 0), core::card_to_int(3, 1));
    // Install the invalid solve contract after graph construction so this test
    // reaches the flat backend's own fail-closed guard.
    graph.config = std::make_shared<const core::HUNLConfig>(valid);

    EXPECT_THROW(
        core::HUNLFlatDCFR(
            graph,
            {1, 1},
            core::HUNLFlatSolveMode::ExplicitHand,
            core::HUNLFlatValueLayout::InfosetHandAction,
            1,
            1.5,
            0.0,
            2.0),
        std::invalid_argument);
}
