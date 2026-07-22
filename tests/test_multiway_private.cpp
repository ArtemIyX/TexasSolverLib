#include "games/multiway_private.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <limits>
#include <stdexcept>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) { return core::card_to_int(rank, suit); }

core::MultiwayWeightedHole hand(std::uint8_t rank0, std::uint8_t suit0, std::uint8_t rank1, std::uint8_t suit1, double weight = 1.0) {
    return {{{c(rank0, suit0), c(rank1, suit1)}}, weight};
}

core::MultiwayPrivateConfig ranges() {
    core::MultiwayPrivateConfig config;
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
    config.ranges[0] = {core::MultiwayWeightedHole{{0, c(14, 0)}, 1.0}};
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
    config.ranges[0] = {core::MultiwayWeightedHole{{c(14, 0), c(14, 0)}, 1.0}};
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0, std::numeric_limits<double>::infinity())};
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_private_joint_sampling_is_blocker_correct) {
    const auto sample = core::sample_multiway_private_hands(ranges(), 12345);
    EXPECT_EQ(sample.holes.size(), std::size_t{3});
    EXPECT_TRUE(!overlap(sample.holes[0], sample.holes[1]));
    EXPECT_TRUE(!overlap(sample.holes[0], sample.holes[2]));
    EXPECT_TRUE(!overlap(sample.holes[1], sample.holes[2]));
}

TEST_CASE(multiway_private_joint_sampling_is_seed_deterministic) {
    const auto first = core::sample_multiway_private_hands(ranges(), 99);
    const auto second = core::sample_multiway_private_hands(ranges(), 99);
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_EQ(first.attempts, second.attempts);
}

TEST_CASE(multiway_compiled_private_ranges_canonicalize_duplicates_and_sample_into_worker_scratch) {
    auto config = ranges();
    config.ranges[0].push_back(hand(13, 0, 14, 0, 2.0));
    core::MultiwayCompiledPrivateRanges compiled(config);
    core::MultiwayPrivateWorkerScratch first;
    core::MultiwayPrivateWorkerScratch second;
    compiled.sample_into(99, first);
    compiled.sample_into(99, second);
    EXPECT_EQ(compiled.seat_count(), 3U);
    EXPECT_EQ(first.seat_count, 3U);
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_EQ(first.attempts, second.attempts);
}

TEST_CASE(multiway_private_joint_sampling_never_returns_a_board_card) {
    const auto config = ranges();
    const auto sample = core::sample_multiway_private_hands(config, 71);
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
    config.max_rejection_attempts = 3;
    EXPECT_THROW(core::sample_multiway_private_hands(config, 7), std::runtime_error);
}

TEST_CASE(multiway_showdown_awards_main_and_side_pots_from_real_hand_strengths) {
    core::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3), c(13, 0)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(5, 1), c(6, 1)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 300, 300};
    input.folded = {false, false, false};
    const auto result = core::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[1], 700);
    EXPECT_EQ(result.payouts[0], 0);
    EXPECT_EQ(result.payouts[2], 0);
}

TEST_CASE(multiway_showdown_excludes_folded_best_hand) {
    core::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3), c(13, 0)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(5, 1), c(6, 1)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 100, 100};
    input.folded = {false, true, false};
    const auto result = core::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[1], 0);
}

TEST_CASE(multiway_showdown_splits_a_board_tie_deterministically) {
    core::MultiwayShowdownInput input;
    input.board = {c(10, 0), c(11, 1), c(12, 2), c(13, 3), c(14, 0)};
    input.holes = {{c(2, 1), c(3, 1)}, {c(4, 1), c(5, 1)}, {c(6, 1), c(7, 1)}};
    input.contributions = {101, 101, 101};
    input.folded = {false, false, false};
    const auto result = core::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 101);
    EXPECT_EQ(result.payouts[1], 101);
    EXPECT_EQ(result.payouts[2], 101);
}

TEST_CASE(multiway_compiled_private_ranges_reject_impossible_deals_before_worker_sampling) {
    auto config = ranges();
    config.ranges[0] = {hand(14, 0, 13, 0)};
    config.ranges[1] = {hand(14, 0, 13, 0)};
    EXPECT_THROW(core::MultiwayCompiledPrivateRanges(config), std::invalid_argument);
}

TEST_CASE(multiway_compiled_private_ranges_offer_nonthrowing_worker_sampling) {
    core::MultiwayCompiledPrivateRanges compiled(ranges());
    core::MultiwayPrivateWorkerScratch scratch;
    EXPECT_TRUE(compiled.try_sample_into(99, scratch));
    EXPECT_EQ(scratch.seat_count, 3U);
}

TEST_CASE(multiway_showdown_preserves_explicit_odd_chip_order) {
    core::MultiwayShowdownInput input;
    input.board = {c(14, 0), c(13, 0), c(12, 1), c(11, 2), c(2, 0)};
    input.holes = {{c(10, 0), c(3, 1)}, {c(10, 1), c(4, 1)}, {c(9, 0), c(8, 0)}};
    input.contributions = {101, 101, 101};
    input.folded = {false, false, false};
    input.odd_chip_first_seat = 1;
    const auto result = core::evaluate_multiway_showdown(input);
    EXPECT_EQ(result.payouts[0], 151);
    EXPECT_EQ(result.payouts[1], 152);
    EXPECT_EQ(result.payouts[2], 0);

    input.odd_chip_first_seat = 3;
    EXPECT_THROW(input.validate(), std::invalid_argument);
}

TEST_CASE(multiway_showdown_rejects_duplicate_cards_and_non_river_board) {
    core::MultiwayShowdownInput input;
    input.board = {c(2, 0), c(3, 1), c(4, 2), c(9, 3)};
    input.holes = {{c(14, 1), c(14, 2)}, {c(13, 1), c(12, 1)}};
    input.contributions = {100, 100};
    input.folded = {false, false};
    EXPECT_THROW(input.validate(), std::invalid_argument);
    input.board.push_back(c(13, 0));
    input.holes[1][0] = c(14, 1);
    EXPECT_THROW(input.validate(), std::invalid_argument);
}
