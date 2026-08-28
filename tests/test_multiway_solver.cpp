#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

texas::MultiwayPublicStateDescriptor root_public_state() {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = 0;
    const auto betting = texas::MultiwayState::initial(game).snapshot();

    const std::vector<texas::MultiwayActionDescriptor> actions = {
        {texas::MultiwayAction::Check, 0, 0, 9001},
        {texas::MultiwayAction::Bet, 1, 100, 9001},
        {texas::MultiwayAction::AllIn, 2, 1000, 9001},
    };
    return texas::MultiwayPublicBuilder::make_root(
        betting, {card(2, 0), card(7, 1), card(9, 2)}, actions);
}

std::vector<texas::MultiwayActionDescriptor> action_descriptors(const texas::MultiwayState& state) {
    std::vector<texas::MultiwayActionDescriptor> descriptors;
    const auto actions = state.legal_actions();
    descriptors.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto action = actions[index];
        const auto seat = static_cast<std::size_t>(state.current_player());
        const auto current = state.street_contributions()[seat];
        const auto target = action == texas::MultiwayAction::Bet || action == texas::MultiwayAction::Raise
            ? state.current_bet() + state.last_full_raise_size()
            : action == texas::MultiwayAction::AllIn
                ? current + state.stacks()[seat]
                : action == texas::MultiwayAction::Call
                    ? std::min(state.current_bet(), current + state.stacks()[seat])
                    : current;
        descriptors.push_back({action, static_cast<std::uint32_t>(index), target, 0U});
    }
    return texas::MultiwayPublicBuilder::canonicalize_action_menu(state.snapshot(), std::move(descriptors));
}

texas::MultiwayPublicStateDescriptor action_child_public_state(
    const texas::MultiwayPublicStateDescriptor& parent,
    std::uint64_t,
    std::uint64_t,
    std::size_t action_index) {
    const auto action = parent.legal_actions.at(action_index);
    const auto state = texas::MultiwayState::from_snapshot(parent.betting)
                           .apply(action.action, action.target_street_contribution);
    return texas::MultiwayPublicBuilder::make_action_child(
        parent, action_index, action_descriptors(state));
}

texas::MultiwayPublicStateDescriptor checked_action_child_public_state() {
    return action_child_public_state(root_public_state(), 2, 102, 0);
}

texas::MultiwayPrivateConfig root_ranges() {
    texas::MultiwayPrivateConfig ranges;
    ranges.board = {card(2, 0), card(7, 1), card(9, 2)};
    texas::MultiwayWeightedHole first;
    first.hole = {card(14, 0), card(13, 0)};
    first.weight = 1.0;
    texas::MultiwayWeightedHole second;
    second.hole = {card(12, 0), card(11, 0)};
    second.weight = 1.0;
    ranges.ranges = {
        {first},
        {second},
    };
    return ranges;
}

texas::MultiwayRootSnapshot valid_root() {
    texas::MultiwayRootSnapshot root;
    root.public_state = root_public_state();
    root.root_infoset = {root.public_state.id, 0};
    root.root_bucket = 0;
    root.seat_order = {0, 1};
    root.odd_chip_first_seat = 0;
    root.private_ranges = root_ranges();
    root.action_abstraction_version = 7;
    root.leaf_model_version = 11;
    return root;
}

texas::MultiwaySolverLimits valid_limits() {
    texas::MultiwaySolverLimits limits;
    limits.worker_count = 2;
    limits.trajectories_per_batch = 8;
    limits.max_public_states = 6;
    limits.max_sparse_rows = 3;
    limits.max_sparse_values = 12;
    limits.max_worker_delta_entries = 4;
    return limits;
}

texas::MultiwaySolveRequest valid_request() {
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    return texas::MultiwaySolveRequest(valid_root(), cfr, valid_limits());
}

texas::MultiwayInfosetId root_infoset() {
    return {root_public_state().id, 0};
}

texas::MultiwaySparseRowShape root_row(std::uint32_t buckets = 1, std::uint8_t actions = 3) {
    return {root_infoset(), buckets, actions};
}

texas::MultiwayWorkerDelta delta(
    std::uint8_t action,
    double regret,
    double strategy_sum,
    std::uint64_t trajectory_id = 0) {
    texas::MultiwayWorkerDelta result;
    result.infoset = root_infoset();
    result.action = action;
    result.regret = regret;
    result.strategy_sum = strategy_sum;
    result.trajectory_id = trajectory_id;
    return result;
}

}  // namespace


TEST_CASE(multiway_solver_request_copies_the_immutable_root_snapshot) {
    auto root = valid_root();
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    const texas::MultiwaySolveRequest request(root, cfr, valid_limits());
    root.public_state.legal_actions[0].action_menu_id = 77;
    root.private_ranges.board[0] = card(3, 0);
    root.public_state.board_runout.remaining_board_cards = 1;
    root.next_street_first_seat = 1;

    EXPECT_EQ(request.root().public_state.legal_actions[0].action_menu_id, valid_root().action_menu_id());
    EXPECT_EQ(request.root().private_ranges.board[0], card(2, 0));
    EXPECT_EQ(request.root().public_state.board_runout.remaining_board_cards, 2U);
    EXPECT_EQ(request.root().next_street_first_seat, 0);
}

TEST_CASE(multiway_solver_request_rejects_wrong_seat_count_cfr) {
    auto cfr = texas::MultiwayCFRConfig{};
    cfr.player_count = 2;
    cfr.player_count = 3;
    EXPECT_THROW(texas::MultiwaySolveRequest(valid_root(), cfr, valid_limits()), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_rejects_inconsistent_board_or_nonacting_root_seat) {
    auto root = valid_root();
    root.private_ranges.board[0] = card(3, 0);
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.root_infoset.seat = 1;
    EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_accepts_a_complete_valid_public_contract) {
    const auto root = valid_root();
    root.validate();

    const auto request = valid_request();
    EXPECT_EQ(request.root().public_state.board.size(), std::size_t{3});
    EXPECT_EQ(request.root().public_state.board_runout.remaining_board_cards, 2U);
    EXPECT_TRUE(!request.root().public_state.board_runout.chance_only_runout);
    EXPECT_EQ(request.root().action_abstraction_identity().menu_id, valid_root().action_menu_id());
    EXPECT_EQ(request.root().action_abstraction_identity().version, 7U);
}

TEST_CASE(multiway_solver_root_rejects_inconsistent_board_runout_metadata) {
    auto root = valid_root();
    root.public_state.board_runout.remaining_board_cards = 1;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.public_state.board_runout.chance_only_runout = true;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.public_state.board.pop_back();
    EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_board_runout_state_compares_const_values) {
    const texas::MultiwayBoardRunoutState turn{1U, false};
    const texas::MultiwayBoardRunoutState same_turn{1U, false};
    const texas::MultiwayBoardRunoutState all_in_turn{1U, true};
    EXPECT_TRUE(turn == same_turn);
    EXPECT_TRUE(!(turn != same_turn));
    EXPECT_TRUE(turn != all_in_turn);
}

TEST_CASE(multiway_action_descriptor_compares_const_values) {
    const texas::MultiwayActionDescriptor call{texas::MultiwayAction::Call, 1U, 2, 9001U};
    const texas::MultiwayActionDescriptor same_call{texas::MultiwayAction::Call, 1U, 2, 9001U};
    const texas::MultiwayActionDescriptor raise{texas::MultiwayAction::Raise, 1U, 4, 9001U};
    EXPECT_TRUE(call == same_call);
    EXPECT_TRUE(!(call != same_call));
    EXPECT_TRUE(call != raise);
}

TEST_CASE(multiway_solver_root_rejects_action_descriptors_not_legal_or_executable_for_snapshot) {
    auto root = valid_root();
    root.public_state.legal_actions[0].action = texas::MultiwayAction::Fold;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.public_state.legal_actions[1].target_street_contribution = 99;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.public_state.legal_actions[2].action_index = 1;
    EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_action_descriptors_require_exact_resulting_contributions) {
    auto root = valid_root();
    root.public_state.legal_actions[0].target_street_contribution = 1;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    EXPECT_THROW(texas::MultiwaySolveRequest(root, cfr, valid_limits()), std::invalid_argument);

    root = valid_root();
    root.public_state.legal_actions[2].target_street_contribution = 999;
    EXPECT_THROW(texas::MultiwaySolveRequest(root, cfr, valid_limits()), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_rejects_invalid_deterministic_next_street_and_odd_chip_metadata) {
    auto root = valid_root();
    root.next_street_first_seat = -1;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.odd_chip_first_seat = 2;
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.odd_chip_rule = static_cast<texas::MultiwayOddChipRule>(255);
    EXPECT_THROW(root.validate(), std::invalid_argument);

    root = valid_root();
    root.seat_order = {0, 0};
    EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_limits_require_all_bounded_capacities) {
    auto limits = valid_limits();
    limits.max_worker_delta_entries = 0;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
    limits = valid_limits();
    limits.max_sparse_rows = 0;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_sparse_admission_is_idempotent_and_action_major) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row(2));
    coordinator.admit_infoset_row(root_row(2));

    EXPECT_EQ(coordinator.storage().row_count(), std::size_t{1});
    EXPECT_EQ(coordinator.storage().value_count(), std::size_t{6});
    const auto* metadata = coordinator.storage().metadata(root_infoset());
    EXPECT_TRUE(metadata != nullptr);
    EXPECT_EQ(metadata->regret_offset, std::size_t{0});
    EXPECT_EQ(metadata->strategy_sum_offset, std::size_t{0});
    EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);

    auto bucket_one = delta(0, 0.0, 1.0);
    bucket_one.bucket = 1;
    auto bucket_zero = delta(1, 0.0, 2.0);
    bucket_zero.bucket = 0;
    texas::MultiwayWorkerDeltaStream first(0, 2);
    texas::MultiwayWorkerDeltaStream second(1, 2);
    first.try_append(bucket_one);
    second.try_append(bucket_zero);
    first.sort_fixed_order();
    second.sort_fixed_order();
    coordinator.merge_worker_streams({first, second});
    const auto at_bucket_zero = coordinator.storage().average_strategy(root_infoset(), 0);
    const auto at_bucket_one = coordinator.storage().average_strategy(root_infoset(), 1);
    EXPECT_NEAR(at_bucket_zero[0], 0.0, 1e-12);
    EXPECT_NEAR(at_bucket_zero[1], 1.0, 1e-12);
    EXPECT_NEAR(at_bucket_one[0], 1.0, 1e-12);
    EXPECT_NEAR(at_bucket_one[1], 0.0, 1e-12);
}

TEST_CASE(multiway_solver_sparse_merge_rejects_lost_nonzero_float64_updates_transactionally) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row());
    const auto large = std::ldexp(1.0, 53);

    texas::MultiwayWorkerDeltaStream first_large(0, 1);
    texas::MultiwayWorkerDeltaStream second_large(1, 1);
    EXPECT_TRUE(first_large.try_append(delta(0, large, large, 0U)));
    first_large.sort_fixed_order();
    second_large.sort_fixed_order();
    coordinator.merge_worker_streams({first_large, second_large});

    texas::MultiwayWorkerDeltaStream first_lost(0, 1);
    texas::MultiwayWorkerDeltaStream second_lost(1, 1);
    EXPECT_TRUE(first_lost.try_append(delta(0, 1.0, 1.0, 1U)));
    first_lost.sort_fixed_order();
    second_lost.sort_fixed_order();
    EXPECT_THROW(coordinator.merge_worker_streams({first_lost, second_lost}), std::overflow_error);
    const auto after_rejection = coordinator.storage().average_strategy(root_infoset(), 0U);
    EXPECT_NEAR(after_rejection[0], 1.0, 1e-12);

    texas::MultiwayWorkerDeltaStream first_representable(0, 1);
    texas::MultiwayWorkerDeltaStream second_representable(1, 1);
    EXPECT_TRUE(first_representable.try_append(delta(0, 2.0, 2.0, 2U)));
    first_representable.sort_fixed_order();
    second_representable.sort_fixed_order();
    coordinator.merge_worker_streams({first_representable, second_representable});
    EXPECT_NEAR(coordinator.storage().average_strategy(root_infoset(), 0U)[0], 1.0, 1e-12);
}

TEST_CASE(multiway_solver_sparse_admission_enforces_row_and_value_caps) {
    auto limits = valid_limits();
    limits.max_sparse_rows = 1;
    limits.max_sparse_values = 3;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(valid_root(), cfr, limits));
    coordinator.admit_infoset_row(root_row());

    auto second = root_row();
    second.infoset = {root_public_state().id, 1};
    EXPECT_THROW(coordinator.admit_infoset_row(second), std::length_error);

    auto too_wide = root_row(2);
    texas::MultiwaySolverCoordinator values_limited(
        texas::MultiwaySolveRequest(valid_root(), cfr, limits));
    EXPECT_THROW(values_limited.admit_infoset_row(too_wide), std::length_error);
}

TEST_CASE(multiway_solver_root_bucket_must_fit_its_admitted_sparse_row) {
    auto root = valid_root();
    root.root_bucket = 1;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(root, cfr, valid_limits()));

    EXPECT_THROW(coordinator.admit_infoset_row(root_row(1)), std::invalid_argument);

    coordinator.admit_infoset_row(root_row(2));
    const auto policy = coordinator.export_root_policy();
    EXPECT_EQ(policy.bucket, 1U);
    EXPECT_EQ(policy.actions.size(), std::size_t{3});
    for (const auto& action : policy.actions) {
        EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
    }
}

TEST_CASE(multiway_solver_compact_backend_routes_admission_merge_and_export) {
    auto limits = valid_limits();
    limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(valid_root(), cfr, limits));
    coordinator.admit_infoset_row(root_row());
    texas::MultiwayWorkerDeltaStream first(0, 1U);
    texas::MultiwayWorkerDeltaStream second(1, 1U);
    EXPECT_TRUE(first.try_append(delta(0U, 1.0, 2.0)));
    first.sort_fixed_order();
    second.sort_fixed_order();
    coordinator.merge_worker_streams({first, second});
    EXPECT_TRUE(coordinator.compact_storage() != nullptr);
    EXPECT_NEAR(coordinator.export_root_policy().actions[0].probability, 1.0, 1e-12);
    const auto checkpoint = coordinator.checkpoint();
    EXPECT_EQ(checkpoint.storage.shapes.size(), std::size_t{1});
    EXPECT_TRUE(checkpoint.storage.regrets[0] > 0.0);
}

TEST_CASE(multiway_solver_live_root_uses_distinct_exact_private_hand_rows) {
    const auto first = std::array<std::uint8_t, 2>{card(14, 0), card(13, 0)};
    const auto second = std::array<std::uint8_t, 2>{card(12, 0), card(11, 0)};
    const auto first_id = texas::solver::multiway::MultiwayBucketTable::hole_index(first);
    const auto second_id = texas::solver::multiway::MultiwayBucketTable::hole_index(second);
    EXPECT_TRUE(first_id != second_id);
    EXPECT_TRUE(first_id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT);
    EXPECT_TRUE(second_id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT);

    auto root = valid_root();
    root.root_uses_exact_private_hand = true;
    root.root_bucket = static_cast<std::uint32_t>(first_id);
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(root, cfr, valid_limits()));
    coordinator.admit_infoset_row(root_row(
        static_cast<std::uint32_t>(texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT)));
    EXPECT_EQ(coordinator.export_root_policy().bucket, static_cast<std::uint32_t>(first_id));

    root.root_bucket = static_cast<std::uint32_t>(second_id);
    texas::MultiwaySolverCoordinator second_coordinator(
        texas::MultiwaySolveRequest(root, cfr, valid_limits()));
    second_coordinator.admit_infoset_row(root_row(
        static_cast<std::uint32_t>(texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT)));
    EXPECT_EQ(second_coordinator.export_root_policy().bucket, static_cast<std::uint32_t>(second_id));
}

TEST_CASE(multiway_solver_same_street_exact_descendants_cannot_raise_the_value_cap) {
    auto root = valid_root();
    root.root_uses_exact_private_hand = true;
    auto limits = valid_limits();
    limits.max_sparse_values = 1U;
    limits.max_sparse_rows = 2U;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(root, cfr, limits));
    const auto admitted_capacity = coordinator.storage().value_capacity_limit();
    coordinator.admit_infoset_row(root_row(
        static_cast<std::uint32_t>(texas::MULTIWAY_HOLE_COMBINATION_COUNT)));

    const auto child = checked_action_child_public_state();
    coordinator.admit_public_state(child);
    const texas::MultiwaySparseRowShape child_row = {
        {child.id, child.betting.current_player},
        static_cast<std::uint32_t>(texas::MULTIWAY_HOLE_COMBINATION_COUNT),
        static_cast<std::uint8_t>(child.legal_actions.size()),
    };
    coordinator.admit_infoset_row(child_row);
    EXPECT_EQ(coordinator.storage().row_count(), std::size_t{2U});
    EXPECT_EQ(coordinator.storage().value_capacity_limit(), admitted_capacity);
}

TEST_CASE(multiway_solver_sparse_rows_require_matching_public_state_shapes) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    const auto root_shape = root_row();
    coordinator.admit_infoset_row(root_shape);
    coordinator.admit_infoset_row(root_shape);
    EXPECT_EQ(coordinator.storage().row_count(), std::size_t{1});
    EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);

    auto conflicting_bucket_shape = root_shape;
    conflicting_bucket_shape.bucket_count = 2;
    EXPECT_THROW(coordinator.admit_infoset_row(conflicting_bucket_shape), std::invalid_argument);

    auto conflicting_action_shape = root_shape;
    conflicting_action_shape.action_count = 2;
    EXPECT_THROW(coordinator.admit_infoset_row(conflicting_action_shape), std::invalid_argument);

    const texas::MultiwaySparseRowShape unknown_state_shape{{{99}, 0}, 1, 1};
    EXPECT_THROW(coordinator.admit_infoset_row(unknown_state_shape), std::invalid_argument);

    auto child = checked_action_child_public_state();
    coordinator.admit_public_state(child);

    const texas::MultiwaySparseRowShape child_shape{{child.id, 1}, 1, 3};
    EXPECT_EQ(child.betting.current_player, child_shape.infoset.seat);
    coordinator.admit_infoset_row(child_shape);
    EXPECT_TRUE(coordinator.storage().has_row(child_shape.infoset));

    const texas::MultiwaySparseRowShape nonacting_child_shape{{child.id, 0}, 1, 3};
    EXPECT_THROW(coordinator.admit_infoset_row(nonacting_child_shape), std::invalid_argument);

    auto too_few_actions = child_shape;
    too_few_actions.action_count = 2;
    EXPECT_THROW(coordinator.admit_infoset_row(too_few_actions), std::invalid_argument);

    auto too_many_actions = child_shape;
    too_many_actions.action_count = 4;
    EXPECT_THROW(coordinator.admit_infoset_row(too_many_actions), std::invalid_argument);

    const auto all_in = action_child_public_state(root_public_state(), 3, 103, 2);
    coordinator.admit_public_state(all_in);
    const auto runout = action_child_public_state(all_in, 4, 104, 1);
    coordinator.admit_public_state(runout);
    const texas::MultiwaySparseRowShape nondecision_shape{{runout.id, 0}, 1, 0};
    EXPECT_THROW(coordinator.admit_infoset_row(nondecision_shape), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_admission_requires_parent_and_conflict_free_identity) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    auto child = checked_action_child_public_state();
    child.parent_id = {99};
    EXPECT_THROW(coordinator.admit_public_state(child), std::invalid_argument);

    child.parent_id = root_public_state().id;
    coordinator.admit_public_state(child);
    EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);
    child.canonical_history_id = 103;
    EXPECT_THROW(coordinator.admit_public_state(child), std::invalid_argument);
}

TEST_CASE(multiway_solver_child_admission_requires_a_replayed_parent_action_history) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    const auto child = checked_action_child_public_state();

    auto missing_history = child;
    missing_history.id = {3};
    missing_history.canonical_history_id = 103;
    missing_history.history.clear();
    EXPECT_THROW(coordinator.admit_public_state(missing_history), std::invalid_argument);

    auto wrong_actor = child;
    wrong_actor.id = {3};
    wrong_actor.canonical_history_id = 103;
    wrong_actor.history.back().actor = 1;
    EXPECT_THROW(coordinator.admit_public_state(wrong_actor), std::invalid_argument);

    auto wrong_action_menu = child;
    wrong_action_menu.id = {3};
    wrong_action_menu.canonical_history_id = 103;
    wrong_action_menu.history.back().action.action_menu_id = 9002;
    EXPECT_THROW(coordinator.admit_public_state(wrong_action_menu), std::invalid_argument);

    auto replay_mismatched_betting = child;
    replay_mismatched_betting.id = {3};
    replay_mismatched_betting.canonical_history_id = 103;
    replay_mismatched_betting.betting = root_public_state().betting;
    EXPECT_THROW(coordinator.admit_public_state(replay_mismatched_betting), std::invalid_argument);

    auto altered_board = child;
    altered_board.id = {3};
    altered_board.canonical_history_id = 103;
    altered_board.board[0] = card(3, 0);
    EXPECT_THROW(coordinator.admit_public_state(altered_board), std::invalid_argument);

    auto altered_runout = child;
    altered_runout.id = {3};
    altered_runout.canonical_history_id = 103;
    altered_runout.board_runout.chance_only_runout = true;
    EXPECT_THROW(coordinator.admit_public_state(altered_runout), std::invalid_argument);

    auto reused_parent_history_id = child;
    reused_parent_history_id.id = {3};
    reused_parent_history_id.canonical_history_id = 101;
    EXPECT_THROW(coordinator.admit_public_state(reused_parent_history_id), std::invalid_argument);

    coordinator.admit_public_state(child);
    auto nonappended_history = child;
    const auto grandchild_betting = texas::MultiwayState::from_snapshot(child.betting)
                                        .apply(texas::MultiwayAction::Check);
    nonappended_history.id = {3};
    nonappended_history.parent_id = {2};
    nonappended_history.canonical_history_id = 103;
    nonappended_history.betting = grandchild_betting.snapshot();
    nonappended_history.history = {{1, {texas::MultiwayAction::Check, 0, 0, 9001}}};
    EXPECT_THROW(coordinator.admit_public_state(nonappended_history), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_duplicate_id_requires_the_complete_descriptor_to_match) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    auto child = checked_action_child_public_state();
    coordinator.admit_public_state(child);
    coordinator.admit_public_state(child);
    EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);

    auto changed_parent = child;
    changed_parent.parent_id = {};
    EXPECT_THROW(coordinator.admit_public_state(changed_parent), std::invalid_argument);

    auto changed_betting = child;
    changed_betting.betting.last_aggressor = 1;
    EXPECT_THROW(coordinator.admit_public_state(changed_betting), std::invalid_argument);

    auto changed_history = child;
    changed_history.history[0].actor = 1;
    EXPECT_THROW(coordinator.admit_public_state(changed_history), std::invalid_argument);

    auto changed_action_descriptor = child;
    for (auto& action : changed_action_descriptor.legal_actions) {
        action.action_menu_id = 9002;
    }
    EXPECT_THROW(coordinator.admit_public_state(changed_action_descriptor), std::invalid_argument);

    texas::MultiwaySolverCoordinator runout_coordinator(valid_request());
    const auto all_in = action_child_public_state(root_public_state(), 3, 103, 2);
    runout_coordinator.admit_public_state(all_in);
    auto runout = action_child_public_state(all_in, 4, 104, 1);
    runout_coordinator.admit_public_state(runout);
    runout_coordinator.admit_public_state(runout);
    EXPECT_EQ(runout_coordinator.diagnostics().public_states_admitted, 3U);

    auto changed_runout = runout;
    changed_runout.board.push_back(card(3, 3));
    changed_runout.board_runout.remaining_board_cards = 1;
    EXPECT_THROW(runout_coordinator.admit_public_state(changed_runout), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_admission_enforces_its_capacity) {
    auto limits = valid_limits();
    limits.max_public_states = 1;
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2;
    texas::MultiwaySolverCoordinator coordinator(
        texas::MultiwaySolveRequest(valid_root(), cfr, limits));
    const auto child = checked_action_child_public_state();
    EXPECT_THROW(coordinator.admit_public_state(child), std::length_error);
}

TEST_CASE(multiway_solver_worker_delta_stream_is_bounded_finite_and_sortable) {
    texas::MultiwayWorkerDeltaStream stream(0, 2);
    EXPECT_TRUE(stream.try_append(delta(2, 1.0, 1.0, 2)));
    EXPECT_TRUE(stream.try_append(delta(0, 1.0, 1.0, 1)));
    EXPECT_TRUE(!stream.try_append(delta(1, 1.0, 1.0, 3)));
    EXPECT_TRUE(!stream.is_fixed_order());
    stream.sort_fixed_order();
    EXPECT_TRUE(stream.is_fixed_order());
    EXPECT_EQ(stream.deltas()[0].action, 0U);

    texas::MultiwayWorkerDeltaStream finite_stream(1, 1);
    EXPECT_TRUE(!finite_stream.try_append(delta(0, std::numeric_limits<double>::infinity(), 1.0)));
}

TEST_CASE(multiway_solver_merge_rejects_missing_misindexed_or_unsorted_worker_streams) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row());
    texas::MultiwayWorkerDeltaStream only_worker(0, 4);
    only_worker.sort_fixed_order();
    EXPECT_THROW(coordinator.merge_worker_streams({only_worker}), std::invalid_argument);

    texas::MultiwayWorkerDeltaStream misplaced(1, 4);
    texas::MultiwayWorkerDeltaStream worker_one(1, 4);
    misplaced.sort_fixed_order();
    worker_one.sort_fixed_order();
    EXPECT_THROW(coordinator.merge_worker_streams({misplaced, worker_one}), std::invalid_argument);

    texas::MultiwayWorkerDeltaStream unsorted(0, 4);
    unsorted.try_append(delta(2, 0.0, 1.0));
    unsorted.try_append(delta(0, 0.0, 1.0));
    texas::MultiwayWorkerDeltaStream sorted(1, 4);
    sorted.sort_fixed_order();
    EXPECT_THROW(coordinator.merge_worker_streams({unsorted, sorted}), std::invalid_argument);
}

TEST_CASE(multiway_solver_merge_is_transactional_when_a_late_cell_is_invalid) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row());
    texas::MultiwayWorkerDeltaStream first(0, 2);
    texas::MultiwayWorkerDeltaStream second(1, 2);
    EXPECT_TRUE(first.try_append(delta(0, 1.0, 4.0, 1U)));
    auto invalid = delta(3, 1.0, 1.0, 2U);
    EXPECT_TRUE(second.try_append(invalid));
    first.sort_fixed_order();
    second.sort_fixed_order();

    EXPECT_THROW(coordinator.merge_worker_streams({first, second}), std::invalid_argument);
    const auto policy = coordinator.export_root_policy();
    for (const auto& action : policy.actions) {
        EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
    }
    EXPECT_EQ(coordinator.diagnostics().worker_delta_entries_merged, 0U);
}

TEST_CASE(multiway_solver_worker_partitions_merge_identical_trajectory_ids_identically) {
    const auto merged_policy = [](std::size_t workers) {
        auto limits = valid_limits();
        limits.worker_count = static_cast<std::uint32_t>(workers);
        limits.max_worker_delta_entries = 12U;
        texas::MultiwayCFRConfig cfr;
        cfr.player_count = 2;
        texas::MultiwaySolverCoordinator coordinator(
            texas::MultiwaySolveRequest(valid_root(), cfr, limits));
        coordinator.admit_infoset_row(root_row());
        std::vector<texas::MultiwayWorkerDeltaStream> streams;
        streams.reserve(workers);
        for (std::size_t worker = 0; worker < workers; ++worker) {
            streams.emplace_back(worker, 12U);
        }
        for (std::uint64_t trajectory = 0; trajectory < 12U; ++trajectory) {
            const auto worker = static_cast<std::size_t>(trajectory % workers);
            EXPECT_TRUE(streams[worker].try_append(delta(
                static_cast<std::uint8_t>(trajectory % 3U),
                static_cast<double>(trajectory + 1U),
                static_cast<double>(trajectory + 2U),
                trajectory)));
        }
        for (auto& stream : streams) stream.sort_fixed_order();
        coordinator.merge_worker_streams(streams);
        return coordinator.export_root_policy();
    };

    const auto baseline = merged_policy(1U);
    for (std::size_t workers = 2U; workers <= 6U; ++workers) {
        const auto policy = merged_policy(workers);
        EXPECT_EQ(policy.actions.size(), baseline.actions.size());
        for (std::size_t action = 0; action < policy.actions.size(); ++action) {
            EXPECT_NEAR(policy.actions[action].probability, baseline.actions[action].probability, 1e-12);
        }
    }
}

TEST_CASE(multiway_solver_merge_is_fixed_order_and_exports_only_root_policy) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row());
    texas::MultiwayWorkerDeltaStream first(0, 4);
    texas::MultiwayWorkerDeltaStream second(1, 4);
    first.try_append(delta(2, -1.0, 2.0, 3));
    first.try_append(delta(0, 1.0, 1.0, 1));
    second.try_append(delta(1, 2.0, 3.0, 2));
    first.sort_fixed_order();
    second.sort_fixed_order();
    coordinator.merge_worker_streams({first, second});

    const auto policy = coordinator.export_root_policy();
    EXPECT_EQ(policy.public_state, root_public_state().id);
    EXPECT_EQ(policy.infoset.seat, 0);
    EXPECT_EQ(policy.actions.size(), std::size_t{3});
    EXPECT_NEAR(policy.actions[0].probability, 1.0 / 6.0, 1e-12);
    EXPECT_NEAR(policy.actions[1].probability, 0.5, 1e-12);
    EXPECT_NEAR(policy.actions[2].probability, 1.0 / 3.0, 1e-12);
    EXPECT_EQ(coordinator.diagnostics().worker_delta_entries_merged, 3U);
}

TEST_CASE(multiway_solver_preallocates_and_reuses_bounded_merge_scratch) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    coordinator.admit_infoset_row(root_row());
    EXPECT_EQ(coordinator.merge_scratch_capacity(), 8U);

    for (std::uint64_t trajectory = 0U; trajectory < 2U; ++trajectory) {
        texas::MultiwayWorkerDeltaStream first(0U, 4U);
        texas::MultiwayWorkerDeltaStream second(1U, 4U);
        EXPECT_TRUE(first.try_append(delta(0U, 1.0, 1.0, trajectory * 2U)));
        EXPECT_TRUE(second.try_append(delta(1U, 1.0, 1.0, trajectory * 2U + 1U)));
        first.sort_fixed_order();
        second.sort_fixed_order();
        coordinator.merge_worker_streams({first, second});
        EXPECT_EQ(coordinator.merge_scratch_capacity(), 8U);
    }
}

TEST_CASE(multiway_solver_root_export_is_uniform_until_its_sparse_row_is_admitted) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    const auto policy = coordinator.export_root_policy();
    EXPECT_EQ(policy.actions.size(), std::size_t{3});
    for (const auto& action : policy.actions) {
        EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
    }
}

TEST_CASE(multiway_solver_row_admission_rejects_a_shape_that_disagrees_with_root_actions) {
    texas::MultiwaySolverCoordinator coordinator(valid_request());
    EXPECT_THROW(coordinator.admit_infoset_row(root_row(1, 2)), std::invalid_argument);
}

TEST_CASE(multiway_solver_result_copies_root_values_and_diagnostics) {
    texas::MultiwayRootPolicy policy;
    policy.public_state = {1};
    std::vector<texas::Value> values = {1.0, -1.0};
    texas::MultiwaySolveDiagnostics diagnostics;
    diagnostics.batches_completed = 4;
    const texas::MultiwaySolveResult result(policy, values, diagnostics);
    values[0] = 99.0;
    diagnostics.batches_completed = 0;

    EXPECT_NEAR(result.root_values()[0], 1.0, 1e-12);
    EXPECT_EQ(result.diagnostics().batches_completed, 4U);
}
