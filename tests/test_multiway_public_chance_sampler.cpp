#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_terminal_adapter.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

const std::array<std::array<std::uint8_t, 2>, 3> kHoles = {{
    {card(14, 0), card(13, 0)},
    {card(12, 0), card(11, 0)},
    {card(10, 0), card(8, 0)},
}};
const std::vector<std::uint8_t> kFlop = {card(2, 0), card(7, 1), card(9, 2)};
const std::vector<std::uint8_t> kTurn = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3),
};

core::MultiwayRootSnapshot make_root(
    core::Street street,
    const std::vector<std::uint8_t>& board,
    const core::MultiwayActionAbstraction& abstraction) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000, 1000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = street;
    const auto betting = core::MultiwayState::initial(game).snapshot();

    core::MultiwayRootSnapshot root;
    root.public_state.id = {1U};
    root.public_state.canonical_history_id = 1U;
    root.public_state.betting = betting;
    root.public_state.board = board;
    root.public_state.board_runout.remaining_board_cards =
        static_cast<std::uint8_t>(5U - board.size());
    root.public_state.legal_actions = abstraction.make_legal_actions(betting, 71U);
    root.root_infoset = {{1U}, 0};
    root.seat_order = {0, 1, 2};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = board;
    for (const auto& hole : kHoles) {
        root.private_ranges.ranges.push_back({{hole, 1.0}});
    }
    root.action_abstraction_version = 1U;
    root.leaf_model_version = 1U;
    return root;
}

core::MultiwaySolverLimits limits() {
    core::MultiwaySolverLimits result;
    result.max_public_states = 32U;
    result.max_sparse_rows = 8U;
    result.max_sparse_values = 64U;
    result.max_worker_delta_entries = 64U;
    return result;
}

core::MultiwayCFRConfig cfr() {
    core::MultiwayCFRConfig result;
    result.player_count = 3U;
    return result;
}

struct SamplerFixture {
    SamplerFixture(core::Street street, const std::vector<std::uint8_t>& board)
        : root(make_root(street, board, abstraction)),
          request(root, cfr(), limits()),
          coordinator(request),
          adapter(coordinator) {}

    core::MultiwayPublicStateDescriptor close_street_with_checks() {
        auto state = request.root().public_state;
        for (std::size_t seat = 0; seat < kHoles.size(); ++seat) {
            const auto found = std::find_if(
                state.legal_actions.begin(), state.legal_actions.end(),
                [](const core::MultiwayActionDescriptor& action) {
                    return action.action == core::MultiwayAction::Check;
                });
            EXPECT_TRUE(found != state.legal_actions.end());
            const auto action_index = static_cast<std::uint32_t>(
                std::distance(state.legal_actions.begin(), found));
            const auto next = core::MultiwayState::from_snapshot(state.betting).apply(
                found->action, found->target_street_contribution);
            std::vector<core::MultiwayActionDescriptor> child_actions;
            if (next.current_player() >= 0) {
                child_actions = abstraction.make_legal_actions(next.snapshot(), 71U);
            }
            state = core::MultiwayPublicBuilder::make_action_child(
                state, action_index, std::move(child_actions));
            coordinator.admit_public_state(state);
        }
        EXPECT_TRUE(core::MultiwayState::from_snapshot(state.betting).requires_street_transition());
        return state;
    }

    core::MultiwayActionAbstraction abstraction;
    core::MultiwayRootSnapshot root;
    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayTerminalAdapter adapter;
};

bool same_edge(
    const core::MultiwaySampledPublicBoardChance& left,
    const core::MultiwaySampledPublicBoardChance& right) {
    return left.parent_id == right.parent_id &&
           left.dealt_cards == right.dealt_cards &&
           left.dealt_card_count == right.dealt_card_count &&
           left.board == right.board && left.board_count == right.board_count &&
           left.board_runout == right.board_runout && left.probability == right.probability;
}

void expect_no_collision(
    const core::MultiwaySampledPublicBoardChance& edge,
    const std::vector<std::uint8_t>& original_board) {
    std::array<bool, 52> used{};
    for (const auto board_card : original_board) used[board_card] = true;
    for (const auto& hole : kHoles) {
        used[hole[0]] = true;
        used[hole[1]] = true;
    }
    for (std::size_t index = 0; index < edge.dealt_card_count; ++index) {
        EXPECT_TRUE(!used[edge.dealt_cards[index]]);
        used[edge.dealt_cards[index]] = true;
    }
}

}  // namespace

TEST_CASE(multiway_public_chance_sampler_is_deterministic_and_does_not_admit_edges) {
    SamplerFixture fixture(core::Street::Preflop, {});
    const auto parent = fixture.close_street_with_checks();
    const auto deal = fixture.adapter.sample_private_deal(17U);
    const auto admitted_before = fixture.coordinator.diagnostics().public_states_admitted;
    std::uint64_t first_state = 991U;
    std::uint64_t second_state = 991U;
    const auto first = fixture.adapter.sample_public_board_chance(parent.id, deal, first_state);
    const auto second = fixture.adapter.sample_public_board_chance(parent.id, deal, second_state);

    EXPECT_TRUE(same_edge(first, second));
    EXPECT_EQ(first_state, second_state);
    EXPECT_TRUE(first_state != 991U);
    EXPECT_EQ(fixture.coordinator.diagnostics().public_states_admitted, admitted_before);
    EXPECT_EQ(first.dealt_card_count, 3U);
    EXPECT_EQ(first.board_count, 3U);
}

TEST_CASE(multiway_public_chance_sampler_flop_is_sorted_collision_free_and_exactly_weighted) {
    SamplerFixture fixture(core::Street::Preflop, {});
    const auto parent = fixture.close_street_with_checks();
    const auto deal = fixture.adapter.sample_private_deal(1U);
    std::uint64_t random_state = 123U;
    const auto edge = fixture.adapter.sample_public_board_chance(parent.id, deal, random_state);

    EXPECT_TRUE(std::is_sorted(
        edge.dealt_cards.begin(), edge.dealt_cards.begin() + edge.dealt_card_count));
    expect_no_collision(edge, {});
    EXPECT_NEAR(edge.probability, 1.0 / 15180.0, 1e-15);
    EXPECT_EQ(edge.board_runout.remaining_board_cards, 2U);
    EXPECT_TRUE(!edge.board_runout.chance_only_runout);
}

TEST_CASE(multiway_public_chance_sampler_changes_path_for_a_different_seed) {
    SamplerFixture fixture(core::Street::Preflop, {});
    const auto parent = fixture.close_street_with_checks();
    const auto deal = fixture.adapter.sample_private_deal(1U);
    std::uint64_t baseline_state = 1U;
    const auto baseline = fixture.adapter.sample_public_board_chance(
        parent.id, deal, baseline_state);
    bool found_different = false;
    for (std::uint64_t seed = 2U; seed <= 32U && !found_different; ++seed) {
        auto random_state = seed;
        const auto candidate = fixture.adapter.sample_public_board_chance(
            parent.id, deal, random_state);
        found_different = !same_edge(baseline, candidate);
    }
    EXPECT_TRUE(found_different);
}

TEST_CASE(multiway_public_chance_sampler_turn_and_river_are_single_card_edges) {
    SamplerFixture flop_fixture(core::Street::Flop, kFlop);
    const auto flop_parent = flop_fixture.close_street_with_checks();
    const auto flop_deal = flop_fixture.adapter.sample_private_deal(1U);
    std::uint64_t turn_state = 77U;
    const auto turn = flop_fixture.adapter.sample_public_board_chance(
        flop_parent.id, flop_deal, turn_state);
    EXPECT_EQ(turn.dealt_card_count, 1U);
    EXPECT_EQ(turn.board_count, 4U);
    EXPECT_NEAR(turn.probability, 1.0 / 43.0, 1e-15);
    expect_no_collision(turn, kFlop);

    SamplerFixture turn_fixture(core::Street::Turn, kTurn);
    const auto turn_parent = turn_fixture.close_street_with_checks();
    const auto turn_deal = turn_fixture.adapter.sample_private_deal(1U);
    std::uint64_t river_state = 77U;
    const auto river = turn_fixture.adapter.sample_public_board_chance(
        turn_parent.id, turn_deal, river_state);
    EXPECT_EQ(river.dealt_card_count, 1U);
    EXPECT_EQ(river.board_count, 5U);
    EXPECT_NEAR(river.probability, 1.0 / 42.0, 1e-15);
    expect_no_collision(river, kTurn);
}
