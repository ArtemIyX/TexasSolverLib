#include "core/lib.hpp"
#include "solver/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

std::vector<std::uint32_t> bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(core::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : compact_board) {
                if (first == board_card || second == board_card) {
                    assignments[core::MultiwayBucketTable::hole_index({first, second})] =
                        core::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

core::MultiwayRootSnapshot make_root(core::Street street) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1'000, 1'000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = street;
    const auto betting = core::MultiwayState::initial(game).snapshot();

    const core::MultiwayActionAbstraction abstraction;
    core::MultiwayRootSnapshot root;
    std::vector<std::uint8_t> board;
    if (street != core::Street::Preflop) {
        board = {card(2U, 0U), card(7U, 1U), card(9U, 2U)};
    }
    root.public_state = core::MultiwayPublicBuilder::make_root(
        betting, board, abstraction.make_legal_actions(betting, 9'001U));
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

core::MultiwaySolverLimits limits() {
    core::MultiwaySolverLimits result;
    result.worker_count = 1U;
    result.trajectories_per_batch = 1U;
    result.max_public_states = 8U;
    result.max_sparse_rows = 8U;
    result.max_sparse_values = 32U;
    result.max_worker_delta_entries = 8U;
    return result;
}

core::MultiwaySolveRequest make_request(core::MultiwayRootSnapshot root) {
    core::MultiwayCFRConfig cfr;
    cfr.player_count = 2U;
    return core::MultiwaySolveRequest(std::move(root), cfr, limits());
}

core::MultiwayBucketRegistry make_buckets(const core::MultiwayRootSnapshot& root) {
    std::vector<std::uint8_t> compact_board;
    compact_board.reserve(root.public_state.board.size());
    for (const auto value : root.public_state.board) {
        compact_board.push_back(value - core::HUNL_CARD_FIRST);
    }
    core::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    return core::MultiwayBucketRegistry({core::MultiwayBucketTable(
        core::make_multiway_model_identity(config), root.public_state.betting.street,
        compact_board, 1U, bucket_assignments(compact_board))});
}

core::CanonicalComboId combo_id(core::CanonicalComboCards cards) {
    return core::canonical_combos().id(cards);
}

static_assert(!std::is_copy_constructible_v<core::MultiwaySearchSession>);
static_assert(!std::is_copy_assignable_v<core::MultiwaySearchSession>);
static_assert(!std::is_move_constructible_v<core::MultiwaySearchSession>);
static_assert(!std::is_move_assignable_v<core::MultiwaySearchSession>);

}  // namespace

TEST_CASE(multiway_search_session_request_rejects_stale_schema_v2_public_descriptors) {
    auto root = make_root(core::Street::Flop);
    ++root.public_state.id.value;
    EXPECT_THROW(make_request(root), std::invalid_argument);

    root = make_root(core::Street::Flop);
    ++root.public_state.canonical_history_id;
    EXPECT_THROW(make_request(root), std::invalid_argument);

    root = make_root(core::Street::Flop);
    ++root.public_state.legal_actions.front().action_menu_id;
    EXPECT_THROW(make_request(root), std::invalid_argument);
}

TEST_CASE(multiway_search_session_initializes_request_local_postflop_state) {
    auto root = make_root(core::Street::Flop);
    const auto expected_menu = root.public_state.legal_actions;
    const auto expected_public_state = root.public_state.id;
    const auto expected_identity = root.action_abstraction_identity();
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);

    core::MultiwaySearchSession session(request, {&buckets}, 23U);

    EXPECT_EQ(session.coordinator().root().public_state.id, expected_public_state);
    EXPECT_EQ(session.root_metadata().public_state, expected_public_state);
    EXPECT_EQ(session.root_metadata().action_abstraction, expected_identity);
    EXPECT_EQ(session.root_metadata().revision, 23U);
    EXPECT_EQ(session.action_menu(), expected_menu);
    EXPECT_TRUE(session.buckets() == &buckets);

    const auto first = session.belief(0);
    const auto second = session.belief(1);
    EXPECT_EQ(first.metadata().source, core::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(second.metadata().source, core::MultiwayRangeBeliefSource::Supplied);
    EXPECT_NEAR(first.weight(combo_id({card(14U, 0U), card(13U, 0U)})), 1.0, 1e-15);
    EXPECT_NEAR(second.weight(combo_id({card(12U, 0U), card(11U, 0U)})), 1.0, 1e-15);
    EXPECT_TRUE(!first.legal(combo_id({card(2U, 0U), card(3U, 0U)})));
    EXPECT_NEAR(first.weight(combo_id({card(2U, 0U), card(3U, 0U)})), 0.0, 0.0);
}

TEST_CASE(multiway_search_session_isolates_request_data_and_belief_updates) {
    auto root = make_root(core::Street::Flop);
    const auto first_hole = root.private_ranges.ranges[0][0].hole;
    const auto second_hole = root.private_ranges.ranges[1][0].hole;
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    const auto other_request = make_request(make_root(core::Street::Flop));
    core::MultiwaySearchSession session(request, {&buckets}, 1U);
    core::MultiwaySearchSession other_session(other_request, {&buckets}, 1U);

    root.public_state.legal_actions.clear();
    root.private_ranges.ranges[0][0].weight = 0.0;
    EXPECT_TRUE(!session.action_menu().empty());
    EXPECT_NEAR(session.belief(0).weight(combo_id(first_hole)), 1.0, 1e-15);

    core::MultiwayBucketActionPolicy policy;
    const auto& table = buckets.table_hunl(core::Street::Flop, request.root().public_state.board);
    policy.identity = table.identity();
    policy.public_state = request.root().public_state.id;
    policy.action_menu_id = request.root().action_menu_id();
    policy.bucket_table_identity = table.table_identity();
    policy.bucket_count = 1U;
    policy.action_count = 2U;
    policy.probabilities = {65535U, 0U};
    const core::MultiwayRangeBeliefObservation observation(
        table, policy, core::MultiwayRangeBeliefSource::Blueprint, 0U, 7U);

    EXPECT_EQ(
        session.apply_observation(0, observation),
        core::MultiwayRangeBeliefUpdateResult::Applied);
    EXPECT_EQ(session.belief(0).metadata().source, core::MultiwayRangeBeliefSource::Blueprint);
    EXPECT_EQ(session.belief(0).metadata().last_update_revision, 2U);
    EXPECT_EQ(session.belief(1).metadata().source, core::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(session.belief(1).metadata().last_update_revision, 1U);
    EXPECT_NEAR(session.belief(1).weight(combo_id(second_hole)), 1.0, 1e-15);
    EXPECT_EQ(other_session.belief(0).metadata().source, core::MultiwayRangeBeliefSource::Supplied);
    EXPECT_EQ(other_session.belief(0).metadata().last_update_revision, 1U);
    EXPECT_NEAR(other_session.belief(0).weight(combo_id(first_hole)), 1.0, 1e-15);
}

TEST_CASE(multiway_search_session_rejects_invalid_postflop_dependencies) {
    const auto root = make_root(core::Street::Flop);
    const auto request = make_request(root);
    const auto buckets = make_buckets(root);

    EXPECT_THROW(core::MultiwaySearchSession(request, {nullptr}, 1U), std::invalid_argument);
    EXPECT_THROW(core::MultiwaySearchSession(request, {&buckets}, 0U), std::invalid_argument);

    auto unavailable_root = root;
    unavailable_root.root_bucket = 1U;
    const auto unavailable_request = make_request(unavailable_root);
    EXPECT_THROW(
        core::MultiwaySearchSession(unavailable_request, {&buckets}, 1U),
        std::invalid_argument);
}

TEST_CASE(multiway_search_session_permits_preflop_without_bucket_registry) {
    const auto root = make_root(core::Street::Preflop);
    const auto request = make_request(root);

    core::MultiwaySearchSession session(request, {nullptr}, 5U);

    EXPECT_TRUE(session.buckets() == nullptr);
    EXPECT_EQ(session.root_metadata().public_state, request.root().public_state.id);
    EXPECT_EQ(session.root_metadata().revision, 5U);
    EXPECT_EQ(session.action_menu(), request.root().public_state.legal_actions);
    EXPECT_EQ(session.belief(1).metadata().source, core::MultiwayRangeBeliefSource::Supplied);
}

TEST_CASE(multiway_search_session_exposes_only_its_own_clean_row_snapshot) {
    const auto root = make_root(core::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    core::MultiwaySearchSession session(request, {&buckets}, 11U);

    const auto before = session.row_view();
    EXPECT_EQ(before.row_count, std::size_t{0});
    EXPECT_EQ(before.value_count, std::size_t{0});
    EXPECT_TRUE(!before.has_root_row);
    EXPECT_TRUE(session.clean_snapshot() == nullptr);

    const core::MultiwayActionAbstraction abstraction;
    const core::MultiwayLeafEvaluator evaluator = {
        [](const core::MultiwayLeafEvaluationRequest& leaf, const void*) noexcept {
            return static_cast<core::Value>(leaf.betting->contributions[static_cast<std::size_t>(leaf.traverser)]);
        },
        nullptr,
    };
    core::MultiwayRootExternalSamplingTraversal traversal(
        session.coordinator(), session.coordinator().root(), abstraction, buckets, &evaluator, 1U);
    core::MultiwayRootBatchRunner runner(traversal, session.coordinator(), 1U, 8U);
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
}

TEST_CASE(multiway_search_session_rejects_invalid_clean_snapshot_metadata) {
    const auto root = make_root(core::Street::Flop);
    const auto buckets = make_buckets(root);
    const auto request = make_request(root);
    core::MultiwaySearchSession session(request, {&buckets}, 3U);

    EXPECT_TRUE(!session.capture_clean_snapshot(true, 0U, 0U, 1U, 1U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 0U, 1U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 0U, 1U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 1U, 0U, 1U));
    EXPECT_TRUE(!session.capture_clean_snapshot(true, 1U, 0U, 1U, 1U, 1U, 2U));
    EXPECT_TRUE(session.clean_snapshot() == nullptr);
}

TEST_CASE(multiway_search_session_is_exposed_by_core_lib) {
    const auto root = make_root(core::Street::Preflop);
    const core::lib::MultiwaySolveRequest request = make_request(root);
    const core::lib::MultiwaySearchSessionDependencies dependencies = {nullptr};
    core::lib::MultiwaySearchSession session(request, dependencies, 3U);

    const core::lib::MultiwaySearchSessionRootMetadata metadata = session.root_metadata();
    const core::lib::MultiwaySearchSessionRowView rows = session.row_view();
    EXPECT_EQ(metadata.revision, 3U);
    EXPECT_EQ(rows.row_count, std::size_t{0});
}
