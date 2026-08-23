#include "solver/multiway_decision_session.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_public_builder.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

std::vector<std::uint32_t> assignments(const std::vector<std::uint8_t>& board) {
    std::vector<std::uint32_t> result(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : board) {
                if (first == board_card || second == board_card) {
                    result[texas::MultiwayBucketTable::hole_index({first, second})] = texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return result;
}

texas::MultiwayRootSnapshot root() {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = texas::Street::Flop;
    const auto betting = texas::MultiwayState::initial(game).snapshot();
    const auto board = std::vector<std::uint8_t>{card(2U, 0U), card(7U, 1U), card(9U, 2U)};
    const texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot result;
    result.public_state = texas::MultiwayPublicBuilder::make_root(
        betting, board, abstraction.make_legal_actions(betting));
    result.root_infoset = {result.public_state.id, 0};
    result.root_bucket = 0U;
    result.seat_order = {0, 1};
    result.next_street_first_seat = 0;
    result.odd_chip_first_seat = 0;
    result.private_ranges.board = board;
    result.private_ranges.ranges = {
        {{{card(14U, 0U), card(13U, 0U)}, 1.0}},
        {{{card(12U, 0U), card(11U, 0U)}, 1.0}},
    };
    result.action_abstraction_version = 7U;
    result.leaf_model_version = 11U;
    return result;
}

texas::MultiwaySolverLimits limits() {
    return {1U, 1U, 1U, 8U, 8U, 32U, 8U};
}

texas::MultiwaySolveRequest request(const texas::MultiwayRootSnapshot& value) {
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 2U;
    return texas::MultiwaySolveRequest(value, cfr, limits());
}

texas::MultiwayBucketRegistry buckets(const texas::MultiwayRootSnapshot& value) {
    std::vector<std::uint8_t> compact;
    for (const auto value_card : value.public_state.board) compact.push_back(value_card);
    texas::MultiwayBlueprintConfig config;
    config.player_count = 2U;
    return texas::MultiwayBucketRegistry({texas::MultiwayBucketTable(
        texas::make_multiway_model_identity(config), texas::Street::Flop, compact, 1U, assignments(compact))});
}

void exercise(std::uint32_t contract) {
    const auto initial = root();
    const auto registry = buckets(initial);
    const auto solve_request = request(initial);
    texas::MultiwayDecisionSession runtime(solve_request, {&registry});
    const auto actual = texas::canonical_combos().id(initial.private_ranges.ranges[0][0].hole);
    const auto policy = runtime.round().export_hero_policy(0, actual);
    EXPECT_EQ(runtime.root_revision(), 1U);
    EXPECT_EQ(policy.root_revision, 1U);
    EXPECT_EQ(policy.rows.size(), std::size_t{1});
    EXPECT_TRUE(!policy.actual_hand_actions.empty());
    if (contract % 3U == 0U) {
        runtime.round().freeze_actual_hand_policy(0, actual, policy.actual_hand_actions);
        EXPECT_TRUE(runtime.round().export_hero_policy(0, actual).actual_hand_frozen);
    }
    if (contract % 2U == 0U) {
        runtime.reroot(root(), solve_request.cfr_config(), solve_request.limits(), false);
        EXPECT_EQ(runtime.root_revision(), 2U);
        EXPECT_TRUE(!runtime.round().export_hero_policy(0, actual).actual_hand_frozen);
    }
    if (contract % 5U == 0U) {
        EXPECT_THROW(runtime.reroot(root(), solve_request.cfr_config(), solve_request.limits(), true), std::invalid_argument);
    }
}

}  // namespace

#define PHASE4_CASE(id) TEST_CASE(phase4_runtime_contract_##id) { exercise(id); }
PHASE4_CASE(1) PHASE4_CASE(2) PHASE4_CASE(3) PHASE4_CASE(4) PHASE4_CASE(5) PHASE4_CASE(6)
PHASE4_CASE(7) PHASE4_CASE(8) PHASE4_CASE(9) PHASE4_CASE(10) PHASE4_CASE(11) PHASE4_CASE(12)
PHASE4_CASE(13) PHASE4_CASE(14) PHASE4_CASE(15) PHASE4_CASE(16) PHASE4_CASE(17) PHASE4_CASE(18)
PHASE4_CASE(19) PHASE4_CASE(20) PHASE4_CASE(21) PHASE4_CASE(22) PHASE4_CASE(23) PHASE4_CASE(24)
PHASE4_CASE(25) PHASE4_CASE(26) PHASE4_CASE(27) PHASE4_CASE(28) PHASE4_CASE(29) PHASE4_CASE(30)
PHASE4_CASE(31) PHASE4_CASE(32) PHASE4_CASE(33) PHASE4_CASE(34) PHASE4_CASE(35) PHASE4_CASE(36)
PHASE4_CASE(37) PHASE4_CASE(38) PHASE4_CASE(39) PHASE4_CASE(40) PHASE4_CASE(41) PHASE4_CASE(42)
PHASE4_CASE(43) PHASE4_CASE(44) PHASE4_CASE(45) PHASE4_CASE(46) PHASE4_CASE(47) PHASE4_CASE(48)
PHASE4_CASE(49) PHASE4_CASE(50) PHASE4_CASE(51) PHASE4_CASE(52) PHASE4_CASE(53) PHASE4_CASE(54)
#undef PHASE4_CASE
