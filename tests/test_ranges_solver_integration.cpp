#include "games/hunl_flat_graph.hpp"
#include "games/hunl_solver.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "test_harness.hpp"

#include <memory>
#include <stdexcept>

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
