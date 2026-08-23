#include "games/multiway_private.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) { return texas::card_to_int(rank, suit); }

texas::MultiwayWeightedHole hand(std::uint8_t rank0, std::uint8_t suit0, std::uint8_t rank1, std::uint8_t suit1, double weight = 1.0) {
    return {{{c(rank0, suit0), c(rank1, suit1)}}, weight};
}

texas::MultiwayPrivateConfig ranges() {
    texas::MultiwayPrivateConfig config;
    config.board = {c(2, 0), c(7, 1), c(9, 2)};
    config.ranges = {
        {hand(14, 0, 13, 0), hand(12, 0, 11, 0)},
        {hand(10, 0, 8, 0)},
        {hand(6, 0, 5, 0)},
    };
    return config;
}

bool overlap(const std::array<std::uint8_t, 2>& lhs, const std::array<std::uint8_t, 2>& rhs) {
    return lhs[0] == rhs[0] || lhs[0] == rhs[1] || lhs[1] == rhs[0] || lhs[1] == rhs[1];
}

}  // namespace

TEST_CASE(multiway_private_validates_two_through_six_seat_ranges) {
    auto config = ranges();
    config.validate();
    config.ranges.resize(1);
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config.ranges.resize(7, config.ranges.front());
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_private_rejects_board_blocked_invalid_and_zero_mass_hands) {
    auto config = ranges();
    config.ranges[0] = {hand(2, 0, 14, 0)};
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config = ranges();
    config.ranges[0] = {texas::MultiwayWeightedHole{{0, c(14, 0)}, 1.0}};
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0, 0.0)};
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_private_rejects_duplicate_board_hole_and_nonfinite_weight) {
    auto config = ranges();
    config.board[1] = config.board[0];
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config = ranges();
    config.ranges[0] = {texas::MultiwayWeightedHole{{c(14, 0), c(14, 0)}, 1.0}};
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0, std::numeric_limits<double>::infinity())};
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_private_joint_sampling_is_blocker_correct) {
    const auto sample = texas::sample_multiway_private_hands(ranges(), 12345);
    EXPECT_EQ(sample.holes.size(), std::size_t{3});
    EXPECT_TRUE(!overlap(sample.holes[0], sample.holes[1]));
    EXPECT_TRUE(!overlap(sample.holes[0], sample.holes[2]));
    EXPECT_TRUE(!overlap(sample.holes[1], sample.holes[2]));
}

TEST_CASE(multiway_private_joint_sampling_is_seed_deterministic) {
    const auto first = texas::sample_multiway_private_hands(ranges(), 99);
    const auto second = texas::sample_multiway_private_hands(ranges(), 99);
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_EQ(first.attempts, second.attempts);
}

TEST_CASE(multiway_compiled_private_ranges_canonicalize_duplicates_and_sample_into_worker_scratch) {
    auto config = ranges();
    config.ranges[0].push_back(hand(13, 0, 14, 0, 2.0));
    texas::MultiwayCompiledPrivateRanges compiled(config);
    texas::MultiwayPrivateWorkerScratch first;
    texas::MultiwayPrivateWorkerScratch second;
    compiled.sample_into(99, first);
    compiled.sample_into(99, second);
    EXPECT_EQ(compiled.seat_count(), 3U);
    EXPECT_EQ(first.seat_count, 3U);
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_EQ(first.attempts, second.attempts);
}

TEST_CASE(multiway_private_joint_sampling_never_returns_a_board_card) {
    const auto config = ranges();
    const auto sample = texas::sample_multiway_private_hands(config, 71);
    for (const auto& hole : sample.holes) {
        for (const auto board_card : config.board) {
            EXPECT_TRUE(hole[0] != board_card);
            EXPECT_TRUE(hole[1] != board_card);
        }
    }
}

TEST_CASE(multiway_private_rejects_impossible_joint_ranges_without_cartesian_expansion) {
    auto config = ranges();
    config.ranges[1] = config.ranges[0];
    EXPECT_THROW(texas::sample_multiway_private_hands(config, 7), std::runtime_error);
}

TEST_CASE(multiway_showdown_awards_main_and_side_pots_from_real_hand_strengths) {
    texas::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3), c(13, 0)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(5, 1), c(6, 1)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 300, 300};
    input.folded = {false, false, false};
    const auto result = texas::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[1], 700);
    EXPECT_EQ(result.payouts[0], 0);
    EXPECT_EQ(result.payouts[2], 0);
}

TEST_CASE(multiway_showdown_excludes_folded_best_hand) {
    texas::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3), c(13, 0)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(5, 1), c(6, 1)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 100, 100};
    input.folded = {false, true, false};
    const auto result = texas::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[1], 0);
}

TEST_CASE(multiway_showdown_splits_a_board_tie_deterministically) {
    texas::MultiwayShowdownInput input;
    input.board = {c(10, 0), c(11, 1), c(12, 2), c(13, 3), c(14, 0)};
    input.holes = {{c(2, 1), c(3, 1)}, {c(4, 1), c(5, 1)}, {c(6, 1), c(7, 1)}};
    input.contributions = {101, 101, 101};
    input.folded = {false, false, false};
    const auto result = texas::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 101);
    EXPECT_EQ(result.payouts[1], 101);
    EXPECT_EQ(result.payouts[2], 101);
}

TEST_CASE(multiway_compiled_private_ranges_reject_impossible_deals_before_worker_sampling) {
    auto config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0)};
    config.ranges[1] = {hand(14, 0, 13, 0)};
    EXPECT_THROW(texas::MultiwayCompiledPrivateRanges(config), std::invalid_argument);
}

TEST_CASE(multiway_private_compiled_proposal_samples_two_through_six_seat_toys) {
    for (std::size_t seats = 2U; seats <= 6U; ++seats) {
        texas::MultiwayPrivateConfig config;
        config.board = {c(2, 0), c(2, 1), c(2, 2)};
        for (std::size_t seat = 0; seat < seats; ++seat) {
            const auto rank = static_cast<std::uint8_t>(4U + seat);
            config.ranges.push_back({
                hand(rank, 0, rank, 1, 1.0),
                hand(rank, 2, rank, 3, 3.0),
            });
        }

        texas::MultiwayCompiledPrivateRanges compiled(config);
        for (std::uint64_t seed = 1U; seed <= 5U; ++seed) {
            texas::MultiwayPrivateWorkerScratch scratch;
            EXPECT_TRUE(compiled.try_sample_into(seed, scratch));
            EXPECT_EQ(scratch.seat_count, seats);
            EXPECT_EQ(scratch.accepted_trajectories, 1U);
            EXPECT_EQ(scratch.discarded_trajectories, 0U);

            double expected_reach = 1.0;
            for (std::size_t seat = 0; seat < seats; ++seat) {
                EXPECT_TRUE(!overlap(scratch.holes[seat], {c(2, 0), c(2, 1)}));
                for (std::size_t other = seat + 1U; other < seats; ++other) {
                    EXPECT_TRUE(!overlap(scratch.holes[seat], scratch.holes[other]));
                }
                const auto& range = config.ranges[seat];
                expected_reach *= scratch.holes[seat] == range[0].hole ? 0.25 : 0.75;
            }
            EXPECT_NEAR(scratch.chance_reach, expected_reach, 1e-12);
            EXPECT_NEAR(scratch.conditional_deal_probability, expected_reach, 1e-12);
            EXPECT_NEAR(scratch.proposal_reach, expected_reach, 1e-12);
            EXPECT_NEAR(scratch.inclusion_reach, 1.0, 1e-12);
        }
    }
}

TEST_CASE(multiway_private_range_feasibility_preflight_reports_feasible_and_infeasible_configs) {
    const auto feasible = texas::preflight_multiway_private_range_feasibility(ranges());
    EXPECT_EQ(feasible.status, texas::MultiwayPrivateRangeFeasibilityStatus::Feasible);
    EXPECT_TRUE(feasible.visited_nodes > 0U);
    EXPECT_EQ(feasible.node_budget, 1'000'000U);
    EXPECT_TRUE(!feasible.reason.empty());

    auto impossible = ranges();
    impossible.ranges[0] = {hand(14, 0, 13, 0)};
    impossible.ranges[1] = {hand(14, 0, 13, 0)};
    const auto infeasible = texas::preflight_multiway_private_range_feasibility(impossible);
    EXPECT_EQ(infeasible.status, texas::MultiwayPrivateRangeFeasibilityStatus::Infeasible);
    EXPECT_TRUE(infeasible.visited_nodes > 0U);
    EXPECT_EQ(infeasible.node_budget, 1'000'000U);
    EXPECT_TRUE(!infeasible.reason.empty());
}

TEST_CASE(multiway_private_range_feasibility_preflight_reports_budget_exhaustion) {
    const auto result = texas::preflight_multiway_private_range_feasibility(ranges(), 0U);
    EXPECT_EQ(result.status, texas::MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted);
    EXPECT_EQ(result.visited_nodes, 0U);
    EXPECT_EQ(result.node_budget, 0U);
    EXPECT_TRUE(!result.reason.empty());
}

TEST_CASE(multiway_compiled_private_ranges_offer_nonthrowing_worker_sampling) {
    texas::MultiwayCompiledPrivateRanges compiled(ranges());
    texas::MultiwayPrivateWorkerScratch scratch;
    EXPECT_TRUE(compiled.try_sample_into(99, scratch));
    EXPECT_EQ(scratch.seat_count, 3U);
}

TEST_CASE(multiway_private_compiled_sampler_exposes_direct_one_shot_proposal_reach) {
    texas::MultiwayPrivateConfig config;
    config.board = {c(2, 0), c(3, 1), c(4, 2)};
    config.ranges = {
        {hand(14, 0, 13, 0), hand(12, 0, 11, 0)},
        {hand(14, 0, 10, 0), hand(9, 0, 8, 0)},
    };
    texas::MultiwayCompiledPrivateRanges compiled(config);
    texas::MultiwayPrivateWorkerScratch scratch;
    bool accepted = false;
    for (std::uint64_t seed = 1; seed <= 100U; ++seed) {
        if (compiled.try_sample_into(seed, scratch)) {
            accepted = true;
            break;
        }
    }
    EXPECT_TRUE(accepted);
    // One of four independent draws collides. Compatible deals retain their
    // direct independent proposal probability without global normalization.
    EXPECT_NEAR(scratch.chance_reach, 0.25, 1e-12);
    EXPECT_NEAR(scratch.conditional_deal_probability, 0.25, 1e-12);
    EXPECT_NEAR(scratch.inclusion_reach, 1.0, 1e-12);
    EXPECT_NEAR(scratch.proposal_reach, 0.25, 1e-12);
    EXPECT_EQ(scratch.accepted_trajectories, 1U);
    EXPECT_EQ(scratch.rejected_trajectories, 0U);
    EXPECT_EQ(scratch.discarded_trajectories, 0U);
}

TEST_CASE(multiway_private_proposal_contract_is_identity_for_always_compatible_ranges) {
    texas::MultiwayCompiledPrivateRanges compiled(ranges());
    for (std::uint64_t seed = 1; seed <= 20U; ++seed) {
        texas::MultiwayPrivateWorkerScratch scratch;
        EXPECT_TRUE(compiled.try_sample_into(seed, scratch));
        EXPECT_NEAR(scratch.conditional_deal_probability, scratch.chance_reach, 1e-12);
        EXPECT_NEAR(scratch.inclusion_reach, 1.0, 1e-12);
        EXPECT_NEAR(scratch.proposal_reach, scratch.chance_reach, 1e-12);
        EXPECT_EQ(scratch.accepted_trajectories, 1U);
        EXPECT_EQ(scratch.discarded_trajectories, 0U);
    }
}

TEST_CASE(multiway_private_proposal_does_not_retry_after_a_collision) {
    texas::MultiwayPrivateConfig config;
    config.board = {c(2, 0), c(3, 1), c(4, 2)};
    config.ranges = {
        {hand(14, 0, 13, 0), hand(12, 0, 11, 0)},
        {hand(14, 0, 10, 0), hand(9, 0, 8, 0)},
    };
    texas::MultiwayCompiledPrivateRanges compiled(config);
    texas::MultiwayPrivateWorkerScratch scratch;
    bool accepted = false;
    for (std::uint64_t seed = 1; seed <= 100U; ++seed) {
        if (compiled.try_sample_into(seed, scratch)) {
            accepted = true;
            break;
        }
    }
    EXPECT_TRUE(accepted);
    EXPECT_EQ(scratch.attempts, 1U);
    EXPECT_NEAR(scratch.inclusion_reach, 1.0, 1e-12);
    EXPECT_NEAR(scratch.conditional_deal_probability, scratch.chance_reach, 1e-12);
    EXPECT_NEAR(scratch.proposal_reach, scratch.chance_reach, 1e-12);
}

TEST_CASE(multiway_private_proposal_contract_never_records_retries) {
    auto config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0)};
    config.ranges[1] = {hand(14, 0, 13, 0), hand(10, 0, 8, 0)};
    texas::MultiwayCompiledPrivateRanges compiled(config);
    bool observed_accept = false;
    for (std::uint64_t seed = 1; seed <= 100U; ++seed) {
        texas::MultiwayPrivateWorkerScratch scratch;
        if (compiled.try_sample_into(seed, scratch)) {
            observed_accept = true;
            EXPECT_EQ(scratch.attempts, 1U);
            EXPECT_EQ(scratch.rejected_trajectories, 0U);
            EXPECT_EQ(scratch.accepted_trajectories, 1U);
            EXPECT_EQ(scratch.discarded_trajectories, 0U);
            break;
        }
    }
    EXPECT_TRUE(observed_accept);
}

TEST_CASE(multiway_private_proposal_contract_marks_bounded_rejection_exhaustion) {
    auto config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0)};
    config.ranges[1] = {hand(14, 0, 13, 0), hand(10, 0, 8, 0)};
    texas::MultiwayCompiledPrivateRanges compiled(config);
    bool observed_exhaustion = false;
    for (std::uint64_t seed = 1; seed <= 100U; ++seed) {
        texas::MultiwayPrivateWorkerScratch scratch;
        if (!compiled.try_sample_into(seed, scratch)) {
            observed_exhaustion = true;
            EXPECT_EQ(scratch.chance_reach, 0.0);
            EXPECT_EQ(scratch.conditional_deal_probability, 0.0);
            EXPECT_EQ(scratch.proposal_reach, 0.0);
            EXPECT_NEAR(scratch.inclusion_reach, 1.0, 1e-12);
            EXPECT_EQ(scratch.accepted_trajectories, 0U);
            EXPECT_EQ(scratch.rejected_trajectories, 1U);
            EXPECT_EQ(scratch.discarded_trajectories, 1U);
            break;
        }
    }
    EXPECT_TRUE(observed_exhaustion);
}

TEST_CASE(multiway_private_standalone_sample_preserves_compiled_proposal_fields) {
    const auto config = ranges();
    const auto sample = texas::sample_multiway_private_hands(config, 77U);
    EXPECT_TRUE(sample.chance_reach > 0.0);
    EXPECT_NEAR(sample.conditional_deal_probability, sample.chance_reach, 1e-12);
    EXPECT_NEAR(sample.proposal_reach, sample.chance_reach, 1e-12);
    EXPECT_NEAR(sample.inclusion_reach, 1.0, 1e-12);
    EXPECT_EQ(sample.accepted_trajectories, 1U);
    EXPECT_EQ(sample.discarded_trajectories, 0U);
}

TEST_CASE(multiway_private_proposal_contract_is_seed_deterministic_including_reach_fields) {
    auto config = ranges();
    texas::MultiwayCompiledPrivateRanges compiled(config);
    texas::MultiwayPrivateWorkerScratch first;
    texas::MultiwayPrivateWorkerScratch second;
    EXPECT_EQ(compiled.try_sample_into(31337U, first), compiled.try_sample_into(31337U, second));
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_EQ(first.attempts, second.attempts);
    EXPECT_NEAR(first.chance_reach, second.chance_reach, 1e-12);
    EXPECT_NEAR(first.conditional_deal_probability, second.conditional_deal_probability, 1e-12);
    EXPECT_NEAR(first.proposal_reach, second.proposal_reach, 1e-12);
    EXPECT_NEAR(first.inclusion_reach, second.inclusion_reach, 1e-12);
}

TEST_CASE(multiway_private_failed_try_sample_clears_reused_worker_scratch) {
    auto config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0)};
    config.ranges[1] = {hand(14, 0, 13, 0), hand(10, 0, 8, 0)};
    texas::MultiwayCompiledPrivateRanges compiled(config);
    bool observed_failure = false;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        texas::MultiwayPrivateWorkerScratch scratch;
        scratch.seat_count = 6;
        scratch.attempts = 99;
        scratch.holes[0] = {1, 2};
        if (!compiled.try_sample_into(seed, scratch)) {
            observed_failure = true;
            EXPECT_EQ(scratch.seat_count, 0U);
            EXPECT_EQ(scratch.attempts, 1U);
            EXPECT_EQ(scratch.holes[0], (std::array<std::uint8_t, 2>{0, 0}));
        }
    }
    EXPECT_TRUE(observed_failure);
}

TEST_CASE(multiway_showdown_preserves_explicit_odd_chip_order) {
    texas::MultiwayShowdownInput input;
    input.board = {c(14, 0), c(13, 0), c(12, 1), c(11, 2), c(2, 0)};
    input.holes = {{c(10, 0), c(3, 1)}, {c(10, 1), c(4, 1)}, {c(9, 3), c(8, 3)}};
    input.contributions = {101, 101, 101};
    input.folded = {false, false, false};
    input.odd_chip_first_seat = 1;
    const auto result = texas::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 151);
    EXPECT_EQ(result.payouts[1], 152);
    EXPECT_EQ(result.payouts[2], 0);

    input.odd_chip_first_seat = 3;
    EXPECT_THROW(input.validate(), std::invalid_argument);
}

TEST_CASE(multiway_showdown_rejects_duplicate_cards_and_non_river_board) {
    texas::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 100};
    input.folded = {false, false};
    EXPECT_THROW(input.validate(), std::invalid_argument);
    input.board.push_back(c(13, 0));
    input.holes[1][0] = c(14, 1);
    EXPECT_THROW(input.validate(), std::invalid_argument);
}
