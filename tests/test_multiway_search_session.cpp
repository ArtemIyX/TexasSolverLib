#include "core/lib.hpp"
#include "solver/multiway_blueprint_trainer.hpp"
#include "solver/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

std::vector<std::uint32_t> bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : compact_board) {
                if (first == board_card || second == board_card) {
                    assignments[texas::MultiwayBucketTable::hole_index({first, second})] =
                        texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

texas::MultiwayRootSnapshot make_root(texas::Street street) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1'000, 1'000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = street;
    const auto betting = texas::MultiwayState::initial(game).snapshot();

    const texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot root;
    std::vector<std::uint8_t> board;
    if (street != texas::Street::Preflop) {
        board = {card(2U, 0U), card(7U, 1U), card(9U, 2U)};
        if (street == texas::Street::Turn || street == texas::Street::River) board.push_back(card(11U, 3U));
        if (street == texas::Street::River) board.push_back(card(13U, 1U));
    }
    root.public_state = texas::MultiwayPublicBuilder::make_root(
        betting, board, abstraction.make_legal_actions(betting));
    root.root_infoset = {root.public_state.id, 0};
    root.root_bucket = 0U;
    root.seat_order = {0, 1};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = root.public_state.board;
    root.private_ranges.ranges = {
        {{{card(14U, 0U), card(13U, 0U)}, 1.0}},
        {{{card(12U, 0U), card(11U, 0U)}, 1.0}},
    };
    root.action_abstraction_version = 17U;
    root.leaf_model_version = 31U;
    return root;
}

texas::MultiwaySolverLimits limits() {
    texas::MultiwaySolverLimits result;
    result.worker_count = 1U;
    result.trajectories_per_batch = 1U;
    result.max_public_states = 8U;
    result.max_sparse_rows = 8U;
    result.max_sparse_values = 32U;
    result.max_worker_delta_entries = 8U;
    return result;
}

texas::MultiwaySolveRequest make_request(texas::MultiwayRootSnapshot root) {
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2U;
    return texas::MultiwaySolveRequest(std::move(root), cfr, limits());
}

texas::MultiwayBucketRegistry make_buckets(const texas::MultiwayRootSnapshot& root) {
    std::vector<std::uint8_t> compact_board;
    compact_board.reserve(root.public_state.board.size());
    for (const auto value : root.public_state.board) {
        compact_board.push_back(value);
    }
    texas::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    return texas::MultiwayBucketRegistry({texas::MultiwayBucketTable(
        texas::make_multiway_model_identity(config), root.public_state.betting.street,
        compact_board, 1U, bucket_assignments(compact_board))});
}

texas::CanonicalComboId combo_id(texas::CanonicalComboCards cards) {
    return texas::canonical_combos().id(cards);
}

texas::Value session_leaf(
    const texas::MultiwayLeafEvaluationRequest& leaf,
    const void*) noexcept {
    return static_cast<texas::Value>(
        leaf.betting->contributions[static_cast<std::size_t>(leaf.traverser)]);
}

texas::MultiwaySearchSessionCleanSnapshot capture_clean_snapshot(
    const texas::MultiwaySolveRequest& request,
    const texas::MultiwayBucketRegistry& buckets,
    std::uint64_t seed) {
    texas::MultiwaySearchSession session(request, {&buckets}, 1U);
    const texas::MultiwayActionAbstraction abstraction;
    const texas::MultiwayLeafEvaluator evaluator = {session_leaf, nullptr};
    texas::MultiwayRootExternalSamplingTraversal traversal(
        session.coordinator(), session.coordinator().root(), abstraction, buckets, &evaluator, 1U);
    texas::MultiwayRootBatchRunner runner(traversal, session.coordinator(), 1U, 8U);
    const auto result = runner.run(0U, 1U, seed);
    if (!session.capture_clean_snapshot(
            result.clean, 1U, 0U, 1U, result.trajectories_accepted,
            result.delta_entries_merged, 1U)) {
        throw std::runtime_error("session fixture failed to capture a clean snapshot");
    }
    return *session.clean_snapshot();
}

static_assert(!std::is_copy_constructible_v<texas::MultiwaySearchSession>);
static_assert(!std::is_copy_assignable_v<texas::MultiwaySearchSession>);
static_assert(!std::is_move_constructible_v<texas::MultiwaySearchSession>);
static_assert(!std::is_move_assignable_v<texas::MultiwaySearchSession>);

}  // namespace

TEST_CASE(multiway_search_session_request_rejects_stale_schema_v2_public_descriptors) {
    auto root = make_root(texas::Street::Flop);
    ++root.public_state.id.value;
    EXPECT_THROW(make_request(root), std::invalid_argument);

    root = make_root(texas::Street::Flop);
    ++root.public_state.canonical_history_id;
    EXPECT_THROW(make_request(root), std::invalid_argument);

    root = make_root(texas::Street::Flop);
    ++root.public_state.legal_actions.front().action_menu_id;
    EXPECT_THROW(make_request(root), std::invalid_argument);
}

TEST_CASE(multiway_search_session_initializes_request_local_postflop_state) {
    auto root = make_root(texas::Street::Flop);
    const auto expected_menu = root.public_state.legal_actions;
    const auto expected_public_state = root.public_state.id;
    const auto expected_identity = root.action_abstraction_identity();
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);

    texas::MultiwaySearchSession session(request, {&buckets}, 23U);

    EXPECT_EQ(session.coordinator().root().public_state.id, expected_public_state);
    EXPECT_EQ(session.root_metadata().public_state, expected_public_state);
    EXPECT_EQ(session.root_metadata().action_abstraction, expected_identity);
    EXPECT_EQ(session.root_metadata().revision, 23U);
    EXPECT_EQ(session.action_menu(), expected_menu);
    EXPECT_TRUE(session.buckets() == &buckets);

    const auto first = session.belief(0);
    const auto second = session.belief(1);
    EXPECT_EQ(first.metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(second.metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_NEAR(first.weight(combo_id({card(14U, 0U), card(13U, 0U)})), 1.0, 1e-15);
    EXPECT_NEAR(second.weight(combo_id({card(12U, 0U), card(11U, 0U)})), 1.0, 1e-15);
    EXPECT_TRUE(!first.legal(combo_id({card(2U, 0U), card(3U, 0U)})));
    EXPECT_NEAR(first.weight(combo_id({card(2U, 0U), card(3U, 0U)})), 0.0, 0.0);
}

TEST_CASE(multiway_search_session_retains_observed_and_translated_action_metadata) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 23U);
    const texas::MultiwayActionAbstraction abstraction;
    const auto translated = abstraction.translate_observed_action(
        root.public_state.betting, root.public_state.legal_actions,
        root.public_state.legal_actions.front().action,
        root.public_state.legal_actions.front().target_street_contribution);

    session.record_action_translation(translated);
    const auto* recorded = session.action_translation();
    EXPECT_TRUE(recorded != nullptr);
    EXPECT_EQ(recorded->observed_action.action_menu_id, 0U);
    EXPECT_EQ(recorded->translated_action, root.public_state.legal_actions.front());

    auto unrelated = translated;
    ++unrelated.translated_action.action_menu_id;
    EXPECT_THROW(session.record_action_translation(unrelated), std::invalid_argument);
}

TEST_CASE(multiway_search_session_plans_important_deviation_without_mutating_current_root) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 23U);
    texas::MultiwayActionAbstractionConfig action_config;
    action_config.translation_max_pseudo_harmonic_distance_basis_points = 1U;
    const texas::MultiwayActionAbstraction abstraction(action_config);
    texas::MultiwayDeviationExpansionConfig expansion;
    expansion.minimum_pseudo_harmonic_distance_basis_points = 1U;
    const auto planned = session.plan_local_expansion(
        abstraction, texas::MultiwayAction::Bet, 500, expansion);

    EXPECT_TRUE(planned.root.public_state.id != session.root_metadata().public_state);
    EXPECT_TRUE(planned.root.public_state.legal_actions.size() <= texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    EXPECT_EQ(session.root_metadata().public_state, root.public_state.id);
}

TEST_CASE(multiway_search_session_isolates_request_data_and_belief_updates) {
    auto root = make_root(texas::Street::Flop);
    const auto first_hole = root.private_ranges.ranges[0][0].hole;
    const auto second_hole = root.private_ranges.ranges[1][0].hole;
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    const auto other_request = make_request(make_root(texas::Street::Flop));
    texas::MultiwaySearchSession session(request, {&buckets}, 1U);
    texas::MultiwaySearchSession other_session(other_request, {&buckets}, 1U);

    root.public_state.legal_actions.clear();
    root.private_ranges.ranges[0][0].weight = 0.0;
    EXPECT_TRUE(!session.action_menu().empty());
    EXPECT_NEAR(session.belief(0).weight(combo_id(first_hole)), 1.0, 1e-15);

    texas::MultiwayBucketActionPolicy policy;
    const auto& table = buckets.table(texas::Street::Flop, request.root().public_state.board);
    policy.identity = table.identity();
    policy.public_state = request.root().public_state.id;
    policy.action_menu_id = request.root().action_menu_id();
    policy.bucket_table_identity = table.table_identity();
    policy.bucket_count = 1U;
    policy.action_count = 2U;
    policy.probabilities = {65535U, 0U};
    const texas::MultiwayRangeBeliefObservation observation(
        table, policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 7U);

    EXPECT_EQ(
        session.apply_observation(0, observation),
        texas::MultiwayRangeBeliefUpdateResult::Applied);
    EXPECT_EQ(session.belief(0).metadata().source, texas::MultiwayRangeBeliefSource::Blueprint);
    EXPECT_EQ(session.belief(0).metadata().last_update_revision, 2U);
    EXPECT_EQ(session.belief(1).metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(session.belief(1).metadata().last_update_revision, 1U);
    EXPECT_NEAR(session.belief(1).weight(combo_id(second_hole)), 1.0, 1e-15);
    EXPECT_EQ(other_session.belief(0).metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(other_session.belief(0).metadata().last_update_revision, 1U);
    EXPECT_NEAR(other_session.belief(0).weight(combo_id(first_hole)), 1.0, 1e-15);
}

TEST_CASE(multiway_search_session_rejects_invalid_postflop_dependencies) {
    const auto root = make_root(texas::Street::Flop);
    const auto request = make_request(root);
    const auto buckets = make_buckets(root);

    EXPECT_THROW(texas::MultiwaySearchSession(request, {nullptr}, 1U), std::invalid_argument);
    EXPECT_THROW(texas::MultiwaySearchSession(request, {&buckets}, 0U), std::invalid_argument);

    auto unavailable_root = root;
    unavailable_root.root_bucket = 1U;
    const auto unavailable_request = make_request(unavailable_root);
    EXPECT_THROW(
        texas::MultiwaySearchSession(unavailable_request, {&buckets}, 1U),
        std::invalid_argument);
}

TEST_CASE(multiway_search_session_permits_preflop_without_bucket_registry) {
    const auto root = make_root(texas::Street::Preflop);
    const auto request = make_request(root);

    texas::MultiwaySearchSession session(request, {nullptr}, 5U);

    EXPECT_TRUE(session.buckets() == nullptr);
    EXPECT_EQ(session.root_metadata().public_state, request.root().public_state.id);
    EXPECT_EQ(session.root_metadata().revision, 5U);
    EXPECT_EQ(session.action_menu(), request.root().public_state.legal_actions);
    EXPECT_EQ(session.belief(1).metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
}

TEST_CASE(multiway_search_session_exposes_only_its_own_clean_row_snapshot) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 11U);

    const auto before = session.row_view();
    EXPECT_EQ(before.row_count, std::size_t{0});
    EXPECT_EQ(before.value_count, std::size_t{0});
    EXPECT_TRUE(!before.has_root_row);
    EXPECT_TRUE(session.clean_snapshot() == nullptr);

    const texas::MultiwayActionAbstraction abstraction;
    const texas::MultiwayLeafEvaluator evaluator = {session_leaf, nullptr};
    texas::MultiwayRootExternalSamplingTraversal traversal(
        session.coordinator(), session.coordinator().root(), abstraction, buckets, &evaluator, 1U);
    texas::MultiwayRootBatchRunner runner(traversal, session.coordinator(), 1U, 8U);
    const auto result = runner.run(8U, 1U, 0x45U);

    const auto rows = session.row_view();
    EXPECT_TRUE(result.clean);
    EXPECT_TRUE(result.trajectories_accepted > 0U);
    EXPECT_TRUE(result.delta_entries_merged > 0U);
    EXPECT_TRUE(rows.row_count > 0U);
    EXPECT_TRUE(rows.value_count > 0U);
    EXPECT_TRUE(rows.has_root_row);

    EXPECT_TRUE(!session.capture_clean_snapshot(
        false, 1U, 8U, 1U, result.trajectories_accepted, result.delta_entries_merged, 1U));
    EXPECT_TRUE(session.clean_snapshot() == nullptr);
    EXPECT_TRUE(session.capture_clean_snapshot(
        result.clean, 1U, 8U, 1U, result.trajectories_accepted, result.delta_entries_merged, 1U));

    const auto* snapshot = session.clean_snapshot();
    EXPECT_TRUE(snapshot != nullptr);
    EXPECT_EQ(snapshot->root_revision, 11U);
    EXPECT_EQ(snapshot->batch_index, 1U);
    EXPECT_EQ(snapshot->first_trajectory_id, 8U);
    EXPECT_EQ(snapshot->trajectory_count, 1U);
    EXPECT_EQ(snapshot->accepted_trajectories, result.trajectories_accepted);
    EXPECT_EQ(snapshot->merged_delta_entries, result.delta_entries_merged);
    EXPECT_EQ(snapshot->worker_count, 1U);
    EXPECT_EQ(snapshot->rows.row_count, rows.row_count);
    EXPECT_EQ(snapshot->rows.value_count, rows.value_count);
    EXPECT_TRUE(snapshot->rows.has_root_row);
    EXPECT_EQ(snapshot->root_policy.public_state, request.root().public_state.id);

    const auto preserved = *snapshot;
    EXPECT_TRUE(!session.capture_clean_snapshot(
        false, 2U, 9U, 1U, result.trajectories_accepted, result.delta_entries_merged, 1U));
    const auto* after_rejection = session.clean_snapshot();
    EXPECT_TRUE(after_rejection != nullptr);
    EXPECT_EQ(after_rejection->batch_index, preserved.batch_index);
    EXPECT_EQ(after_rejection->root_policy.actions.size(), preserved.root_policy.actions.size());
    for (std::size_t action = 0U; action < preserved.root_policy.actions.size(); ++action) {
        EXPECT_EQ(after_rejection->root_policy.actions[action].action, preserved.root_policy.actions[action].action);
        EXPECT_NEAR(
            after_rejection->root_policy.actions[action].probability,
            preserved.root_policy.actions[action].probability,
            0.0);
    }
}

TEST_CASE(multiway_search_session_rejects_invalid_clean_snapshot_metadata) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 3U);

    EXPECT_TRUE(!session.capture_clean_snapshot(true, 0U, 0U, 1U, 1U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 0U, 1U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 0U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 1U, 0U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 1U, 1U, 2U));
    EXPECT_TRUE(session.clean_snapshot() == nullptr);
}

TEST_CASE(multiway_coordinator_checkpoint_restores_canonical_sparse_rows) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession original(request, {&buckets}, 1U);
    const auto infoset = request.root().root_infoset;
    const auto action_count = static_cast<std::uint8_t>(request.root().public_state.legal_actions.size());
    original.coordinator().admit_infoset_row({infoset, 1U, action_count});
    texas::MultiwayWorkerDeltaStream stream(0U, action_count);
    for (std::uint8_t action = 0U; action < action_count; ++action) {
        EXPECT_TRUE(stream.try_append({infoset, 0U, action, static_cast<double>(action + 1U),
            static_cast<double>(action + 2U), 17U}));
    }
    stream.sort_fixed_order();
    original.coordinator().merge_worker_streams(std::vector<texas::MultiwayWorkerDeltaStream>{stream});
    const auto expected = original.coordinator().storage().average_strategy(infoset, 0U);
    const auto checkpoint = original.coordinator().checkpoint();

    texas::MultiwaySearchSession resumed(request, {&buckets}, 1U);
    resumed.coordinator().restore_checkpoint(checkpoint);
    const auto actual = resumed.coordinator().storage().average_strategy(infoset, 0U);
    EXPECT_EQ(actual.size(), expected.size());
    for (std::size_t action = 0U; action < expected.size(); ++action) {
        EXPECT_NEAR(actual[action], expected[action], 0.0);
    }
}

TEST_CASE(multiway_blueprint_training_checkpoint_resumes_sparse_state) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySolverCoordinator original_coordinator(request);
    const texas::MultiwayActionAbstraction abstraction;
    const texas::MultiwayLeafEvaluator evaluator = {session_leaf, nullptr};
    texas::MultiwayRootExternalSamplingTraversal original_traversal(
        original_coordinator, request.root(), abstraction, buckets, &evaluator, 1U);
    texas::MultiwayRootBatchRunner original_runner(original_traversal, original_coordinator, 1U, 8U);
    texas::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    const auto identity = texas::make_multiway_model_identity(config);
    texas::MultiwayBlueprintTrainer original(identity, original_runner, original_coordinator, {}, 0x77U);
    original.run_batches(1U, 1U, 0x77U);
    const auto checkpoint = original.checkpoint();
    const auto expected = original.export_full_policy();

    texas::MultiwaySolverCoordinator resumed_coordinator(request);
    texas::MultiwayRootExternalSamplingTraversal resumed_traversal(
        resumed_coordinator, request.root(), abstraction, buckets, &evaluator, 1U);
    texas::MultiwayRootBatchRunner resumed_runner(resumed_traversal, resumed_coordinator, 1U, 8U);
    texas::MultiwayBlueprintTrainer resumed(identity, resumed_runner, resumed_coordinator, {}, 0x77U);
    resumed.resume_from_checkpoint(checkpoint);
    const auto actual = resumed.export_full_policy();

    EXPECT_EQ(actual.rows.size(), expected.rows.size());
    EXPECT_EQ(actual.payload_hash, expected.payload_hash);
}

TEST_CASE(multiway_search_session_replays_clean_snapshots_for_identical_inputs) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);

    const auto first = capture_clean_snapshot(request, buckets, 0x57U);
    const auto second = capture_clean_snapshot(request, buckets, 0x57U);

    EXPECT_EQ(first.rows.row_count, second.rows.row_count);
    EXPECT_EQ(first.rows.value_count, second.rows.value_count);
    EXPECT_EQ(first.merged_delta_entries, second.merged_delta_entries);
    EXPECT_EQ(first.root_policy.actions.size(), second.root_policy.actions.size());
    for (std::size_t action = 0U; action < first.root_policy.actions.size(); ++action) {
        EXPECT_EQ(first.root_policy.actions[action].action, second.root_policy.actions[action].action);
        EXPECT_NEAR(
            first.root_policy.actions[action].probability,
            second.root_policy.actions[action].probability,
            0.0);
    }
}

TEST_CASE(multiway_search_session_exports_and_freezes_only_actual_hero_hand_policy) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 19U);
    const auto actual = combo_id(root.private_ranges.ranges[0][0].hole);

    const auto exported = session.export_hero_policy(0, actual);
    EXPECT_EQ(exported.hero_seat, 0);
    EXPECT_EQ(exported.root_revision, 19U);
    EXPECT_EQ(exported.rows.size(), std::size_t{1});
    EXPECT_EQ(exported.rows.front().combo, actual);
    EXPECT_EQ(exported.actual_hand_actions.size(), request.root().public_state.legal_actions.size());
    EXPECT_TRUE(!exported.actual_hand_frozen);

    session.freeze_actual_hand_policy(0, actual, exported.actual_hand_actions);
    const auto frozen = session.export_hero_policy(0, actual);
    EXPECT_TRUE(frozen.actual_hand_frozen);
    EXPECT_EQ(frozen.actual_hand_actions.size(), exported.actual_hand_actions.size());
    for (std::size_t action = 0U; action < frozen.actual_hand_actions.size(); ++action) {
        EXPECT_EQ(frozen.actual_hand_actions[action].action, exported.actual_hand_actions[action].action);
        EXPECT_NEAR(
            frozen.actual_hand_actions[action].probability,
            exported.actual_hand_actions[action].probability,
            0.0);
    }

    session.clear_actual_hand_freeze();
    EXPECT_TRUE(!session.export_hero_policy(0, actual).actual_hand_frozen);
}

TEST_CASE(multiway_search_session_carries_posteriors_into_reroots) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 1U);

    const auto next = session.make_next_round_root(make_root(texas::Street::Turn));
    EXPECT_EQ(next.public_state.betting.street, texas::Street::Turn);
    EXPECT_EQ(next.private_ranges.board, next.public_state.board);
    EXPECT_EQ(next.private_ranges.ranges[0].size(), std::size_t{1});
    EXPECT_EQ(next.private_ranges.ranges[1].size(), std::size_t{1});

    const auto reroot = session.make_reroot_root(make_root(texas::Street::Flop));
    EXPECT_EQ(reroot.public_state.betting.street, texas::Street::Flop);
    EXPECT_THROW(session.make_next_round_root(make_root(texas::Street::Flop)), std::invalid_argument);
}

TEST_CASE(multiway_decision_session_replaces_round_state_on_reroot) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwayDecisionSession runtime(request, {&buckets});
    const auto actual = combo_id(root.private_ranges.ranges[0][0].hole);
    const auto policy = runtime.round().export_hero_policy(0, actual);
    runtime.round().freeze_actual_hand_policy(0, actual, policy.actual_hand_actions);
    EXPECT_TRUE(runtime.round().export_hero_policy(0, actual).actual_hand_frozen);

    runtime.reroot(make_root(texas::Street::Flop), request.cfr_config(), request.limits(), false);
    EXPECT_EQ(runtime.root_revision(), 2U);
    EXPECT_EQ(runtime.round().root_metadata().revision, 2U);
    EXPECT_TRUE(!runtime.round().export_hero_policy(0, actual).actual_hand_frozen);
    EXPECT_EQ(runtime.round().root_metadata().public_state, make_root(texas::Street::Flop).public_state.id);
}

TEST_CASE(multiway_search_session_rejects_absent_hero_hand_and_invalid_freeze) {
    const auto root = make_root(texas::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    texas::MultiwaySearchSession session(request, {&buckets}, 1U);
    const auto actual = combo_id(root.private_ranges.ranges[0][0].hole);

    EXPECT_THROW(session.export_hero_policy(0, combo_id({card(2U, 0U), card(3U, 0U)})), std::invalid_argument);
    auto actions = session.export_hero_policy(0, actual).actual_hand_actions;
    actions.front().probability = 0.0;
    EXPECT_THROW(session.freeze_actual_hand_policy(0, actual, actions), std::invalid_argument);
}

TEST_CASE(multiway_search_session_is_exposed_by_core_lib) {
    const auto root = make_root(texas::Street::Preflop);
    const texas::lib::MultiwaySolveRequest request = make_request(root);
    const texas::lib::MultiwaySearchSessionDependencies dependencies = {nullptr};
    texas::lib::MultiwaySearchSession session(request, dependencies, 3U);

    const texas::lib::MultiwaySearchSessionRootMetadata metadata = session.root_metadata();
    const texas::lib::MultiwaySearchSessionRowView rows = session.row_view();
    EXPECT_EQ(metadata.revision, 3U);
    EXPECT_EQ(rows.row_count, std::size_t{0});
}

TEST_CASE(multiway_search_session_owns_continuation_selector_dependency) {
    const auto root = make_root(texas::Street::Preflop);
    const auto selector = std::make_shared<const texas::MultiwayFixedContinuationSelector>(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    const texas::MultiwaySearchSessionDependencies dependencies{nullptr, selector};
    texas::MultiwaySearchSession session(make_request(root), dependencies, 1U);

    EXPECT_EQ(session.continuation_selector(), selector.get());
}
