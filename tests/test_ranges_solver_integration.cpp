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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

texas::HUNLRangeInput single_hand_range(std::uint8_t first, std::uint8_t second, double weight = 1.0) {
    texas::HUNLRangeInput range;
    range.hand_weights.push_back({{first, second}, weight});
    return range;
}
texas::HUNLConfig range_contract_config() {
    auto config = texas::default_tiny_subgame();
    config.initial_hole_cards = std::nullopt;
    config.initial_ranges[0] = single_hand_range(texas::card_to_int(14, 1), texas::card_to_int(13, 3));
    config.initial_ranges[1] = single_hand_range(texas::card_to_int(12, 1), texas::card_to_int(11, 3));
    config.range_policy = texas::HUNLRangePolicy::RequireExplicit;
    return config;
}

struct InjectedLeafContext {
    std::uint32_t calls = 0;
    std::uint32_t batch_calls = 0;
    std::size_t largest_batch = 0U;
    bool received_public_state = true;
    bool received_private_deal = true;
    bool received_provenance = true;
};

bool injected_zero_sum_leaf(
    void* raw_context,
    const texas::HUNLLeafEvaluationRequest* requests,
    texas::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    auto& context = *static_cast<InjectedLeafContext*>(raw_context);
    ++context.batch_calls;
    context.largest_batch = std::max(context.largest_batch, count);
    for (std::size_t index = 0; index < count; ++index) {
        ++context.calls;
        context.received_public_state = context.received_public_state &&
            !requests[index].public_state.hole_cards.has_value() &&
            requests[index].bucket_reach[0].size() == 1U &&
            requests[index].bucket_reach[1].size() == 1U;
        context.received_private_deal = context.received_private_deal &&
            texas::are_valid_and_distinct_cards(requests[index].private_hole_cards[0].data(), 2U) &&
            texas::are_valid_and_distinct_cards(requests[index].private_hole_cards[1].data(), 2U) &&
            requests[index].scope == texas::HUNLLeafEvaluationScope::DealConditional;
        context.received_provenance = context.received_provenance &&
            requests[index].abstraction_version == "blueprint-v1" &&
            requests[index].model_version == "leaf-v1";
        results[index].values = {0.0, 0.0};
        results[index].units = requests[index].units;
        results[index].scope = requests[index].scope;
        results[index].populated = true;
        results[index].abstraction_version = requests[index].abstraction_version;
        results[index].model_version = requests[index].model_version;
    }
    return true;
}

bool rejecting_leaf(
    void*,
    const texas::HUNLLeafEvaluationRequest*,
    texas::HUNLLeafEvaluationResult*,
    std::size_t) {
    return false;
}

bool mismatched_unit_leaf(
    void*,
    const texas::HUNLLeafEvaluationRequest*,
    texas::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        results[index].values = {0.0, 0.0};
        results[index].units = texas::HUNLLeafValueUnits::BigBlinds;
    }
    return true;
}

bool mismatched_provenance_leaf(
    void*,
    const texas::HUNLLeafEvaluationRequest* requests,
    texas::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        results[index].values = {0.0, 0.0};
        results[index].units = requests[index].units;
        results[index].scope = requests[index].scope;
        results[index].populated = true;
        results[index].abstraction_version = requests[index].abstraction_version;
        results[index].model_version = "wrong-model";
    }
    return true;
}

bool non_zero_sum_leaf(
    void*,
    const texas::HUNLLeafEvaluationRequest* requests,
    texas::HUNLLeafEvaluationResult* results,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        results[index].values = {1.0, 0.0};
        results[index].units = requests[index].units;
        results[index].scope = requests[index].scope;
        results[index].populated = true;
        results[index].abstraction_version = requests[index].abstraction_version;
        results[index].model_version = requests[index].model_version;
    }
    return true;
}

struct DeadlineAwareLeafContext {
    bool received_deadline = false;
};

bool deadline_aware_slow_leaf(
    void* raw_context,
    const texas::HUNLLeafEvaluationRequest* requests,
    texas::HUNLLeafEvaluationResult*,
    std::size_t count) {
    auto& context = *static_cast<DeadlineAwareLeafContext*>(raw_context);
    if (count == 0U || !requests[0].deadline.has_value()) return false;
    context.received_deadline = true;
    while (std::chrono::steady_clock::now() < *requests[0].deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

}  // namespace

TEST_CASE(ranges_explicit_hand_contract_accepts_no_range_fields) {
    const auto config = texas::default_tiny_subgame();
    config.validate();
    texas::validate_config(config);
    EXPECT_EQ(texas::resolve_range_policy(config), texas::HUNLRangePolicy::Uniform);
}

TEST_CASE(ranges_unspecified_policy_with_complete_root_ranges_selects_range_contract) {
    auto config = range_contract_config();
    config.range_policy = texas::HUNLRangePolicy::Unspecified;

    config.validate();
    EXPECT_EQ(texas::resolve_range_policy(config), texas::HUNLRangePolicy::UseInitialRanges);
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
    config.range_policy = texas::HUNLRangePolicy::UseInitialRanges;
    config.initial_ranges[1] = std::nullopt;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_range_contract_rejects_fixed_private_cards) {
    auto config = range_contract_config();
    config.initial_hole_cards = texas::default_tiny_subgame().initial_hole_cards;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_joint_normalization_conditions_on_cross_player_blockers) {
    auto config = range_contract_config();
    const auto ace_spades = texas::card_to_int(14, 3);
    config.initial_ranges[0]->hand_weights.push_back(
        {{{ace_spades, texas::card_to_int(2, 0)}}, 3.0});
    config.initial_ranges[1]->hand_weights.push_back(
        {{{ace_spades, texas::card_to_int(3, 0)}}, 9.0});
    const auto deals = texas::normalize_hunl_joint_range(config);
    double total = 0.0;
    for (const auto& deal : deals) {
        total += deal.weight;
        EXPECT_TRUE(texas::are_valid_and_distinct_cards(deal.hole[0].data(), 2));
        const std::array<std::uint8_t, 4> all = {
            deal.hole[0][0], deal.hole[0][1], deal.hole[1][0], deal.hole[1][1]};
        EXPECT_TRUE(texas::are_valid_and_distinct_cards(all.data(), all.size()));
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST_CASE(ranges_joint_normalization_rejects_fully_blocked_cross_ranges) {
    auto config = range_contract_config();
    const auto first = texas::card_to_int(11, 0);
    const auto second = texas::card_to_int(10, 0);
    config.initial_ranges[0] = single_hand_range(first, second);
    config.initial_ranges[1] = single_hand_range(second, first);
    EXPECT_THROW(texas::normalize_hunl_joint_range(config), std::invalid_argument);
}

TEST_CASE(ranges_structured_root_request_validates_versions_units_and_joint_reach) {
    texas::HUNLStructuredRootRequest request;
    request.config = range_contract_config();
    request.blueprint_version = "blueprint-v1";
    request.model_version = "value-v1";
    request.value_units = texas::HUNLLeafValueUnits::BigBlinds;
    request.validate();
    EXPECT_NEAR(request.normalized_joint_range().front().weight, 1.0, 1e-12);
    request.blueprint_version.clear();
    EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST_CASE(ranges_structured_root_requires_selected_hero_for_multi_hand_export_and_keeps_wide_diagnostics_separate) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.initial_ranges[0]->hand_weights.push_back(
        {{texas::card_to_int(10, 0), texas::card_to_int(9, 2)}, 1.0});
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root.hero_selection = texas::HUNLStructuredRootRequest::HeroSelection{
        1, {texas::card_to_int(12, 1), texas::card_to_int(11, 3)}, 0U};
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root.hero_selection = texas::HUNLStructuredRootRequest::HeroSelection{
        0, {texas::card_to_int(10, 0), texas::card_to_int(9, 2)}, 0U};
    root.include_range_wide_root_diagnostics = true;
    root.validate();

    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1U;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile);
    EXPECT_EQ(storage.row_count(), 2U);
    const auto selected = storage.view_mut(storage.meta()[0].id);
    const auto other = storage.view_mut(storage.meta()[1].id);
    EXPECT_TRUE(selected.action_count >= 2U);
    for (std::size_t action = 0; action < selected.action_count; ++action) {
        selected.strategy_sum[texas::HUNLSampledStorage::value_index(
            selected.layout, selected.bucket_count, selected.action_count, 0U, action)] =
            action == 0U ? 1.0F : 0.0F;
        other.strategy_sum[texas::HUNLSampledStorage::value_index(
            other.layout, other.bucket_count, other.action_count, 0U, action)] =
            action == 1U ? 1.0F : 0.0F;
    }
    const auto result = session.resume_batches(0U);
    EXPECT_NEAR(result.root_strategy.actions[0].probability, 1.0, 1e-12);
    EXPECT_NEAR(result.root_strategy.actions[1].probability, 0.0, 1e-12);
    EXPECT_TRUE(result.range_wide_root_strategy.has_value());
    EXPECT_EQ(result.root_strategy.actions.size(), result.range_wide_root_strategy->actions.size());
    EXPECT_NEAR(result.range_wide_root_strategy->actions[0].probability, 0.5, 1e-12);
    EXPECT_NEAR(result.range_wide_root_strategy->actions[1].probability, 0.5, 1e-12);
}

TEST_CASE(ranges_structured_root_rejects_unknown_units_and_anonymous_depth_leaf_model) {
    texas::HUNLStructuredRootRequest request;
    request.config = range_contract_config();
    request.blueprint_version = "blueprint-v1";
    request.value_units = static_cast<texas::HUNLLeafValueUnits>(99);
    EXPECT_THROW(request.validate(), std::invalid_argument);

    request.value_units = texas::HUNLLeafValueUnits::Chips;
    request.config.depth_limit_plies = 1U;
    EXPECT_THROW(request.validate(), std::invalid_argument);
    request.model_version = "leaf-v1";
    request.validate();
}

TEST_CASE(ranges_structured_root_validation_rejects_blocked_and_accepts_compatible_deals) {
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        texas::HUNLStructuredRootRequest request;
        request.config = range_contract_config();
        request.blueprint_version = "blueprint-v1";
        request.model_version = "value-v1";
        const auto first = texas::card_to_int(rank, 0);
        const auto second = texas::card_to_int(rank == 14 ? 2 : rank + 1, 1);
        request.config.initial_ranges[0] = single_hand_range(first, second);
        request.config.initial_ranges[1] = single_hand_range(second, first);
        EXPECT_THROW(request.validate(), std::invalid_argument);
        request.config = range_contract_config();
        request.validate();
    }
}

TEST_CASE(ranges_structured_root_sampled_positive_work_uses_private_range_trajectories) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    config.max_cached_public_states = 1024;
    config.seed = 0xB10EULL;
    const auto result = texas::lib::solve_hunl_postflop_sampled(root, config, 1);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_EQ(result.profile.traversals, 1U);
    EXPECT_TRUE(!result.root_strategy.actions.empty());
}

TEST_CASE(ranges_structured_workers_merge_the_same_deterministic_trajectory_batch) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    texas::HUNLSampledSolverConfig serial_config;
    serial_config.minibatch_size = 4U;
    serial_config.workers = 1U;
    serial_config.seed = 0xC0FFEEULL;
    texas::HUNLSampledStorage serial_storage;
    texas::HUNLSampledProfile serial_profile;
    texas::HUNLSampledRangeSession serial(root, serial_config, serial_storage, serial_profile);
    const auto serial_result = serial.resume_batches(1U);

    auto parallel_config = serial_config;
    parallel_config.workers = 2U;
    texas::HUNLSampledStorage parallel_storage;
    texas::HUNLSampledProfile parallel_profile;
    texas::HUNLSampledRangeSession parallel(root, parallel_config, parallel_storage, parallel_profile);
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

TEST_CASE(ranges_structured_memory_preflight_and_session_guard_count_retained_range_state) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1U;
    config.max_cached_public_states = 1U;

    const auto range_memory = texas::estimate_hunl_sampled_range_memory(root, config);
    EXPECT_TRUE(range_memory.joint_deal_bytes >= sizeof(texas::HUNLJointRangeDeal));
    EXPECT_TRUE(range_memory.infoset_lookup_bytes > 0U);
    EXPECT_TRUE(range_memory.peak_bytes() >= range_memory.retained_bytes);

    texas::HUNLSampledSolveRequest request;
    request.structured_root = root;
    texas::HUNLSampledSolver solver(config);
    const auto preflight = solver.preflight(request);
    EXPECT_TRUE(preflight.estimate.structured_joint_deal_bytes >= sizeof(texas::HUNLJointRangeDeal));
    EXPECT_TRUE(preflight.estimate.structured_infoset_lookup_bytes > 0U);
    EXPECT_TRUE(preflight.estimate.structured_session_bytes > 0U);

    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    EXPECT_THROW(
        texas::HUNLSampledRangeSession(root, config, storage, profile, 0U, nullptr, 1U),
        std::runtime_error);
}

TEST_CASE(ranges_single_joint_deal_runs_through_structured_sampled_engine) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    root.value_units = texas::HUNLLeafValueUnits::BigBlinds;
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 2;
    config.workers = 2;
    config.seed = 0x5EEDULL;

    texas::HUNLSampledSolver range_solver(config);
    texas::HUNLSampledSolveRequest range_request;
    range_request.structured_root = root;
    const auto range_result = range_solver.run_batches(range_request, 1U);
    EXPECT_EQ(range_result.batches_completed, 1U);
    EXPECT_TRUE(range_solver.storage().row_count() > 0U);
}

TEST_CASE(ranges_structured_session_resumes_without_replaying_batch_ids) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto first = session.resume_batches(1);
    const auto second = session.resume_batches(1);
    EXPECT_EQ(first.batches_completed, 1U);
    EXPECT_EQ(second.batches_completed, 1U);
    EXPECT_EQ(session.next_batch(), 2U);
    EXPECT_EQ(profile.snapshot().traversals, 2U);
}

TEST_CASE(ranges_structured_session_move_retains_its_private_storage_lifetime) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1U;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession original(root, config, storage, profile);
    texas::HUNLSampledRangeSession moved(std::move(original));

    const auto result = moved.resume_batches(1U);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_EQ(moved.next_batch(), 1U);
    EXPECT_EQ(profile.snapshot().traversals, 1U);
}

TEST_CASE(ranges_structured_session_deadline_preserves_the_next_unpublished_batch) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto expired = session.resume_batches(
        4U, std::chrono::steady_clock::now() - std::chrono::milliseconds{1});
    EXPECT_EQ(expired.batches_completed, 0U);
    EXPECT_TRUE(expired.timed_out);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_expired_export_deadline_preserves_the_last_clean_root_policy) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile);
    const auto clean = session.resume_batches(0U);

    const auto expired = session.resume_batches(
        0U, std::chrono::steady_clock::now() - std::chrono::milliseconds{1});
    EXPECT_TRUE(expired.timed_out);
    EXPECT_EQ(expired.batches_completed, 0U);
    EXPECT_EQ(expired.root_strategy.actions.size(), clean.root_strategy.actions.size());
    for (std::size_t action = 0; action < clean.root_strategy.actions.size(); ++action) {
        EXPECT_EQ(expired.root_strategy.actions[action].action_id, clean.root_strategy.actions[action].action_id);
        EXPECT_NEAR(
            expired.root_strategy.actions[action].probability,
            clean.root_strategy.actions[action].probability,
            1e-12);
    }
}

TEST_CASE(ranges_structured_deadline_cancels_a_cooperative_slow_leaf_before_publication) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1U;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1U;
    DeadlineAwareLeafContext context;
    const texas::HUNLLeafEvaluator evaluator{&context, deadline_aware_slow_leaf};
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    const auto result = session.resume_batches(
        1U, std::chrono::steady_clock::now() + std::chrono::milliseconds{20});
    EXPECT_TRUE(context.received_deadline);
    EXPECT_TRUE(result.timed_out);
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_session_rejects_exhausted_batch_identity_before_replay) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(
        root, config, storage, profile, std::numeric_limits<std::uint64_t>::max());

    EXPECT_THROW(session.resume_batches(1U), std::overflow_error);
    EXPECT_EQ(session.next_batch(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_sampled_solver_retains_structured_deadline_cursor_between_resumes) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    texas::HUNLSampledSolver solver(config);

    solver.begin_structured_session(root);
    EXPECT_TRUE(solver.has_structured_session());
    const auto first = solver.resume_structured_batches(1U);
    const auto second = solver.resume_structured_batches(1U);
    EXPECT_EQ(first.batches_completed, 1U);
    EXPECT_EQ(second.batches_completed, 1U);
    EXPECT_EQ(second.profile.traversals, 2U);
}

TEST_CASE(ranges_structured_depth_limit_uses_typed_injected_leaf_evaluator) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";

    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 2;
    config.workers = 2;
    InjectedLeafContext context;
    const texas::HUNLLeafEvaluator evaluator{&context, injected_zero_sum_leaf};
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    const auto result = session.resume_batches(1);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_TRUE(context.calls > 0U);
    EXPECT_EQ(context.batch_calls, 1U);
    EXPECT_TRUE(context.largest_batch > 1U);
    EXPECT_TRUE(context.received_public_state);
    EXPECT_TRUE(context.received_private_deal);
    EXPECT_TRUE(context.received_provenance);
}

TEST_CASE(ranges_structured_depth_limit_rejects_missing_typed_leaf_evaluator) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;

    EXPECT_THROW(texas::HUNLSampledRangeSession(root, config, storage, profile), std::invalid_argument);
}

TEST_CASE(ranges_structured_facade_passes_typed_leaf_evaluator_to_positive_work) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    InjectedLeafContext context;
    const texas::HUNLLeafEvaluator evaluator{&context, injected_zero_sum_leaf};

    const auto result = texas::lib::solve_hunl_postflop_sampled(root, config, 1U, &evaluator);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_TRUE(context.calls > 0U);
}

TEST_CASE(ranges_structured_solver_session_rejects_missing_leaf_before_it_is_retained) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolver solver;

    EXPECT_THROW(solver.begin_structured_session(root), std::invalid_argument);
    EXPECT_TRUE(!solver.has_structured_session());
}

TEST_CASE(ranges_structured_leaf_callback_failure_aborts_the_current_batch) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    const texas::HUNLLeafEvaluator evaluator{nullptr, rejecting_leaf};
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    EXPECT_THROW(session.resume_batches(1U), std::runtime_error);
    EXPECT_EQ(session.next_batch(), 0U);
    EXPECT_EQ(profile.snapshot().traversals, 0U);
}

TEST_CASE(ranges_structured_leaf_callback_rejects_a_value_unit_mismatch) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    const texas::HUNLLeafEvaluator evaluator{nullptr, mismatched_unit_leaf};
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile, 0U, &evaluator);

    EXPECT_THROW(session.resume_batches(1U), std::runtime_error);
    EXPECT_EQ(session.next_batch(), 0U);
}

TEST_CASE(ranges_structured_leaf_callback_rejects_wrong_provenance_and_non_zero_sum_values) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.config.depth_limit_plies = 1;
    root.blueprint_version = "blueprint-v1";
    root.model_version = "leaf-v1";
    texas::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;

    texas::HUNLSampledStorage provenance_storage;
    texas::HUNLSampledProfile provenance_profile;
    const texas::HUNLLeafEvaluator bad_provenance{nullptr, mismatched_provenance_leaf};
    texas::HUNLSampledRangeSession provenance_session(
        root, config, provenance_storage, provenance_profile, 0U, &bad_provenance);
    EXPECT_THROW(provenance_session.resume_batches(1U), std::runtime_error);

    texas::HUNLSampledStorage value_storage;
    texas::HUNLSampledProfile value_profile;
    const texas::HUNLLeafEvaluator bad_values{nullptr, non_zero_sum_leaf};
    texas::HUNLSampledRangeSession value_session(
        root, config, value_storage, value_profile, 0U, &bad_values);
    EXPECT_THROW(value_session.resume_batches(1U), std::runtime_error);
}

TEST_CASE(ranges_structured_zero_batch_session_exports_typed_uniform_root_actions) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";
    texas::HUNLSampledSolverConfig config;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, config, storage, profile);

    const auto result = session.resume_batches(0U);
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_TRUE(!result.root_strategy.actions.empty());
    double probability_sum = 0.0;
    for (const auto& action : result.root_strategy.actions) {
        EXPECT_TRUE(action.action_id >= texas::ACTION_FOLD);
        EXPECT_TRUE(action.action_menu_id != 0U);
        probability_sum += action.probability;
    }
    EXPECT_NEAR(probability_sum, 1.0, 1e-12);
}

TEST_CASE(ranges_structured_root_allows_low_spr_balanced_pots_but_requires_a_live_snapshot_for_facing_bets) {
    texas::HUNLStructuredRootRequest request;
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
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    auto config = std::make_shared<const texas::HUNLConfig>(root.config);
    texas::HUNLLiveRootSnapshot live;
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
    EXPECT_TRUE(std::find(live.legal_actions.begin(), live.legal_actions.end(), texas::ACTION_FOLD) !=
                live.legal_actions.end());
    EXPECT_TRUE(std::find(live.legal_actions.begin(), live.legal_actions.end(), texas::ACTION_CALL) !=
                live.legal_actions.end());

    texas::HUNLSampledSolverConfig solver_config;
    solver_config.minibatch_size = 1U;
    texas::HUNLSampledStorage storage;
    texas::HUNLSampledProfile profile;
    texas::HUNLSampledRangeSession session(root, solver_config, storage, profile);
    const auto result = session.resume_batches(1U);
    EXPECT_EQ(result.batches_completed, 1U);
    EXPECT_EQ(result.root_strategy.actions[0].action_id, texas::ACTION_FOLD);
    EXPECT_EQ(result.root_strategy.actions[1].action_id, texas::ACTION_CALL);
}

TEST_CASE(ranges_sampled_request_requires_a_structured_root_contract) {
    texas::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";

    texas::HUNLSampledSolveRequest request{root, nullptr};

    texas::HUNLSampledSolver solver;
    EXPECT_THROW(solver.run_batches(request, 0), std::invalid_argument);
}

TEST_CASE(ranges_uniform_policy_rejects_initial_ranges) {
    auto config = texas::default_tiny_subgame();
    config.initial_ranges[0] = single_hand_range(texas::card_to_int(2, 0), texas::card_to_int(3, 1));
    config.range_policy = texas::HUNLRangePolicy::Uniform;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(ranges_recursive_postflop_entrypoint_rejects_range_contract_before_solving) {
    const auto config = range_contract_config();

    EXPECT_THROW(texas::solve_hunl_postflop(config, 0, 1.5, 0.0, 2.0, 1, 8, false), std::invalid_argument);
}

TEST_CASE(ranges_flat_postflop_entrypoint_rejects_range_contract_before_solving) {
    auto config = range_contract_config();
    config.starting_street = texas::Street::Turn;
    config.initial_board.pop_back();

    EXPECT_THROW(texas::solve_hunl_postflop(config, 0, 1.5, 0.0, 2.0, 2, 8, true), std::invalid_argument);
}
