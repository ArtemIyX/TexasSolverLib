#include "games/hunl_flat_graph.hpp"
#include "games/hunl_solver.hpp"
#include "core/lib.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_sampled_solver.hpp"
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

TEST_CASE(ranges_structured_root_sampled_positive_work_is_fail_closed) {
    core::HUNLStructuredRootRequest root;
    root.config = range_contract_config();
    root.blueprint_version = "blueprint-v1";
    root.model_version = "value-v1";

    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.workers = 1;
    config.max_cached_public_states = 1024;
    config.seed = 0xB10EULL;
    EXPECT_THROW(
        core::lib::solve_hunl_postflop_sampled(root, config, 1),
        core::HUNLSampledStructuredRangeNotReady);
}

TEST_CASE(ranges_structured_root_allows_low_spr_balanced_pots_but_rejects_facing_bets) {
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
