#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kFlop = {card(2, 0), card(7, 1), card(9, 2)};
const std::vector<std::uint8_t> kTurn = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3),
};
const std::array<std::array<std::uint8_t, 2>, 2> kHoles = {{
    {card(14, 0), card(13, 0)},
    {card(12, 0), card(11, 0)},
}};

std::vector<std::uint8_t> compact_bucket_board(const std::vector<std::uint8_t>& hunl_board) {
    std::vector<std::uint8_t> compact;
    compact.reserve(hunl_board.size());
    for (const auto card : hunl_board) compact.push_back(card - core::HUNL_CARD_FIRST);
    return compact;
}

std::vector<std::uint32_t> assignments_for(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(core::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            if (std::find(compact_board.begin(), compact_board.end(), first) != compact_board.end() ||
                std::find(compact_board.begin(), compact_board.end(), second) != compact_board.end()) {
                assignments[core::MultiwayBucketTable::hole_index(hole)] =
                    core::MULTIWAY_INVALID_BUCKET;
            }
        }
    }
    return assignments;
}

core::MultiwayBucketRegistry make_flop_turn_buckets(bool include_turns) {
    core::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    const auto identity = core::make_multiway_model_identity(config);
    std::vector<core::MultiwayBucketTable> tables;
    const auto compact_flop = compact_bucket_board(kFlop);
    tables.emplace_back(identity, core::Street::Flop, compact_flop, 1U, assignments_for(compact_flop));
    if (include_turns) {
        for (std::uint8_t candidate = 0; candidate < 52U; ++candidate) {
            if (std::find(compact_flop.begin(), compact_flop.end(), candidate) != compact_flop.end()) continue;
            auto board = compact_flop;
            board.push_back(candidate);
            tables.emplace_back(
                identity, core::Street::Turn, board, 1U, assignments_for(board));
        }
    }
    return core::MultiwayBucketRegistry(std::move(tables));
}

core::MultiwayBucketRegistry make_root_buckets(
    core::Street street,
    const std::vector<std::uint8_t>& board) {
    core::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    const auto compact_board = compact_bucket_board(board);
    return core::MultiwayBucketRegistry({core::MultiwayBucketTable(
        core::make_multiway_model_identity(config), street, compact_board, 1U,
        assignments_for(compact_board))});
}

core::MultiwaySolverLimits limits(std::size_t delta_capacity = 256U) {
    core::MultiwaySolverLimits result;
    result.max_public_states = 512U;
    result.max_sparse_rows = 128U;
    result.max_sparse_values = 1024U;
    result.max_worker_delta_entries = delta_capacity;
    return result;
}

core::MultiwayCFRConfig cfr() {
    core::MultiwayCFRConfig result;
    result.player_count = 2U;
    return result;
}

core::MultiwayRootSnapshot make_root(
    const core::MultiwayState& state,
    const std::vector<std::uint8_t>& board,
    const core::MultiwayActionAbstraction& abstraction) {
    core::MultiwayRootSnapshot root;
    root.public_state = core::MultiwayPublicBuilder::make_root(
        state.snapshot(), board, abstraction.make_legal_actions(state.snapshot(), 83U));
    root.root_infoset = {root.public_state.id, state.current_player()};
    root.seat_order = {0, 1};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = board;
    for (const auto& hole : kHoles) root.private_ranges.ranges.push_back({{hole, 1.0}});
    root.action_abstraction_version = 1U;
    root.leaf_model_version = 1U;
    return root;
}

core::MultiwayState fresh_state(core::Street street, core::PlayerId first_player = 0) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = first_player;
    game.big_blind = 100;
    game.street = street;
    return core::MultiwayState::initial(game);
}

struct LeafProbe {
    std::size_t calls = 0U;
    std::array<core::Street, 256> streets{};
    std::array<std::size_t, 256> board_sizes{};
};

core::Value probe_leaf(
    const core::MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept {
    auto& probe = *const_cast<LeafProbe*>(static_cast<const LeafProbe*>(context));
    if (probe.calls < probe.streets.size()) {
        probe.streets[probe.calls] = request.betting->street;
        probe.board_sizes[probe.calls] = request.board->size();
    }
    ++probe.calls;
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<core::Value>(request.betting->street_contributions[traverser]);
}

bool saw_turn(const LeafProbe& probe) {
    const auto count = std::min(probe.calls, probe.streets.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (probe.streets[index] == core::Street::Turn && probe.board_sizes[index] == 4U) {
            return true;
        }
    }
    return false;
}

struct ChanceTraversalFixture {
    ChanceTraversalFixture(
        std::uint32_t max_decision_depth,
        std::uint32_t max_public_chance_depth,
        bool include_turn_buckets,
        std::size_t delta_capacity = 256U)
        : root(make_root(fresh_state(core::Street::Flop), kFlop, abstraction)),
          request(root, cfr(), limits(delta_capacity)),
          coordinator(request),
          buckets(make_flop_turn_buckets(include_turn_buckets)),
          evaluator{probe_leaf, &probe},
          traversal(
              coordinator, request.root(), abstraction, buckets, &evaluator,
              max_decision_depth, max_public_chance_depth) {}

    core::MultiwayActionAbstraction abstraction;
    core::MultiwayRootSnapshot root;
    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayBucketRegistry buckets;
    LeafProbe probe;
    core::MultiwayLeafEvaluator evaluator;
    core::MultiwayRootExternalSamplingTraversal traversal;
};

struct AllInRunoutFixture {
    AllInRunoutFixture(
        core::Street street,
        const std::vector<std::uint8_t>& board,
        std::size_t delta_capacity = 256U)
        : facing_all_in(fresh_state(street, 1).apply(core::MultiwayAction::AllIn, 1000)),
          root(make_root(facing_all_in, board, abstraction)),
          request(root, cfr(), limits(delta_capacity)),
          coordinator(request),
          buckets(make_root_buckets(street, board)),
          evaluator{probe_leaf, &probe},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U, 0U) {}

    core::MultiwayActionAbstraction abstraction;
    core::MultiwayState facing_all_in;
    core::MultiwayRootSnapshot root;
    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayBucketRegistry buckets;
    LeafProbe probe;
    core::MultiwayLeafEvaluator evaluator;
    core::MultiwayRootExternalSamplingTraversal traversal;
};

bool same_deltas(
    const core::MultiwayWorkerDeltaStream& left,
    const core::MultiwayWorkerDeltaStream& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& a = left.deltas()[index];
        const auto& b = right.deltas()[index];
        if (!(a.infoset == b.infoset) || a.bucket != b.bucket || a.action != b.action ||
            a.regret != b.regret || a.strategy_sum != b.strategy_sum ||
            a.trajectory_id != b.trajectory_id) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE(multiway_public_chance_traversal_rejects_excessive_chance_depth) {
    core::MultiwayActionAbstraction abstraction;
    const auto root = make_root(fresh_state(core::Street::Flop), kFlop, abstraction);
    const core::MultiwaySolveRequest request(root, cfr(), limits());
    core::MultiwaySolverCoordinator coordinator(request);
    const auto buckets = make_flop_turn_buckets(false);
    LeafProbe probe;
    const core::MultiwayLeafEvaluator evaluator = {probe_leaf, &probe};
    EXPECT_THROW(
        core::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator,
            1U, core::MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH + 1U),
        std::invalid_argument);
}

TEST_CASE(multiway_public_chance_traversal_rejects_oversized_root_action_menu) {
    core::MultiwayActionAbstraction abstraction;
    const auto state = fresh_state(core::Street::Flop);
    auto root = make_root(state, kFlop, abstraction);
    root.public_state.legal_actions.clear();
    root.public_state.legal_actions.push_back({core::MultiwayAction::Check, 0U, 0, 83U});
    for (int target = 100; target <= 700; target += 100) {
        root.public_state.legal_actions.push_back({
            core::MultiwayAction::Bet,
            static_cast<std::uint32_t>(root.public_state.legal_actions.size()),
            target,
            83U,
        });
    }
    root.public_state.legal_actions.push_back({
        core::MultiwayAction::AllIn,
        static_cast<std::uint32_t>(root.public_state.legal_actions.size()),
        1000,
        83U,
    });
    root.public_state = core::MultiwayPublicBuilder::make_root(
        state.snapshot(), kFlop, std::move(root.public_state.legal_actions));
    root.root_infoset = {root.public_state.id, state.current_player()};
    EXPECT_EQ(
        root.public_state.legal_actions.size(),
        core::MULTIWAY_MAX_TRAVERSAL_ACTIONS + 1U);

    const core::MultiwaySolveRequest request(root, cfr(), limits());
    core::MultiwaySolverCoordinator coordinator(request);
    const auto buckets = make_flop_turn_buckets(false);
    LeafProbe probe;
    const core::MultiwayLeafEvaluator evaluator = {probe_leaf, &probe};
    EXPECT_THROW(
        core::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator, 1U, 0U),
        std::invalid_argument);
    EXPECT_EQ(coordinator.storage().row_count(), std::size_t{0});
    EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 1U);
}

TEST_CASE(multiway_public_chance_traversal_same_seed_has_the_same_path_and_deltas) {
    std::uint64_t transition_seed = 0U;
    for (std::uint64_t seed = 1U; seed <= 32U && transition_seed == 0U; ++seed) {
        ChanceTraversalFixture scout(2U, 1U, false);
        core::MultiwayWorkerDeltaStream scout_stream(0U, 256U);
        EXPECT_TRUE(scout.traversal.run(0, 91U, seed, scout_stream));
        if (saw_turn(scout.probe)) transition_seed = seed;
    }
    EXPECT_TRUE(transition_seed != 0U);

    ChanceTraversalFixture first(2U, 1U, false);
    ChanceTraversalFixture second(2U, 1U, false);
    core::MultiwayWorkerDeltaStream first_stream(0U, 256U);
    core::MultiwayWorkerDeltaStream second_stream(0U, 256U);
    EXPECT_TRUE(first.traversal.run(0, 91U, transition_seed, first_stream));
    EXPECT_TRUE(second.traversal.run(0, 91U, transition_seed, second_stream));
    EXPECT_TRUE(saw_turn(first.probe));
    EXPECT_TRUE(saw_turn(second.probe));
    EXPECT_TRUE(same_deltas(first_stream, second_stream));
    EXPECT_EQ(first.probe.calls, second.probe.calls);
    EXPECT_EQ(first.probe.streets, second.probe.streets);
    EXPECT_EQ(first.probe.board_sizes, second.probe.board_sizes);
}

TEST_CASE(multiway_public_chance_traversal_lazily_admits_transition_and_calls_turn_leaf) {
    ChanceTraversalFixture fixture(2U, 1U, false);
    bool reached_turn = false;
    for (std::uint64_t seed = 1U; seed <= 32U && !reached_turn; ++seed) {
        fixture.probe = {};
        core::MultiwayWorkerDeltaStream stream(0U, 256U);
        EXPECT_TRUE(fixture.traversal.run(0, seed, seed, stream));
        reached_turn = saw_turn(fixture.probe);
    }
    EXPECT_TRUE(reached_turn);
    EXPECT_TRUE(
        fixture.coordinator.diagnostics().public_states_admitted >
        1U + fixture.request.root().public_state.legal_actions.size());
}

TEST_CASE(multiway_public_chance_traversal_enters_next_street_bucket_and_counts_chance_once) {
    ChanceTraversalFixture fixture(3U, 1U, true);
    bool found_turn_row = false;
    for (std::uint64_t seed = 1U; seed <= 32U && !found_turn_row; ++seed) {
        fixture.probe = {};
        core::MultiwayWorkerDeltaStream stream(0U, 256U);
        EXPECT_TRUE(fixture.traversal.run(0, seed, seed, stream));
        for (std::size_t begin = 0; begin < stream.size(); ++begin) {
            if (stream.deltas()[begin].infoset.public_state ==
                    fixture.request.root().public_state.id ||
                stream.deltas()[begin].action != 0U) {
                continue;
            }
            double strategy_total = 0.0;
            double first_regret = stream.deltas()[begin].regret;
            double last_regret = first_regret;
            for (std::size_t index = begin; index < stream.size(); ++index) {
                if (!(stream.deltas()[index].infoset == stream.deltas()[begin].infoset)) continue;
                strategy_total += stream.deltas()[index].strategy_sum;
                last_regret = stream.deltas()[index].regret;
            }
            if (std::fabs(strategy_total - 45.0) < 1e-12 &&
                std::fabs((last_regret - first_regret) - 1000.0) < 1e-12) {
                found_turn_row = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_turn_row);
    EXPECT_TRUE(fixture.coordinator.storage().row_count() > 1U);
}

TEST_CASE(multiway_public_chance_traversal_all_in_flop_and_turn_run_to_exact_river_terminal) {
    AllInRunoutFixture flop(core::Street::Flop, kFlop);
    core::MultiwayWorkerDeltaStream flop_stream(0U, 256U);
    EXPECT_TRUE(flop.traversal.run(0, 1U, 77U, flop_stream));
    EXPECT_EQ(flop.probe.calls, std::size_t{0});
    EXPECT_TRUE(
        flop.coordinator.diagnostics().public_states_admitted >
        1U + flop.request.root().public_state.legal_actions.size());

    AllInRunoutFixture turn(core::Street::Turn, kTurn);
    core::MultiwayWorkerDeltaStream turn_stream(0U, 256U);
    EXPECT_TRUE(turn.traversal.run(0, 2U, 88U, turn_stream));
    EXPECT_EQ(turn.probe.calls, std::size_t{0});
    EXPECT_TRUE(
        turn.coordinator.diagnostics().public_states_admitted >
        1U + turn.request.root().public_state.legal_actions.size());
}

TEST_CASE(multiway_public_chance_traversal_rolls_back_capacity_failure_after_runout) {
    AllInRunoutFixture fixture(core::Street::Flop, kFlop, 1U);
    core::MultiwayWorkerDeltaStream stream(0U, 1U);
    core::MultiwayWorkerDelta sentinel;
    sentinel.infoset = fixture.request.root().root_infoset;
    sentinel.trajectory_id = 9U;
    EXPECT_TRUE(stream.try_append(sentinel));

    EXPECT_TRUE(!fixture.traversal.run(0, 10U, 99U, stream));
    EXPECT_EQ(stream.size(), std::size_t{1});
    EXPECT_EQ(stream.deltas()[0].trajectory_id, 9U);
    EXPECT_EQ(fixture.probe.calls, std::size_t{0});
    EXPECT_TRUE(
        fixture.coordinator.diagnostics().public_states_admitted >
        1U + fixture.request.root().public_state.legal_actions.size());
}
