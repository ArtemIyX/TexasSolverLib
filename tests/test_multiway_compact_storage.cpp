#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "test_harness.hpp"

#include <array>

TEST_CASE(multiway_compact_storage_saturates_regrets_and_normalizes_policies) {
    texas::solver::multiway::MultiwayCompactStorage storage(2U, 6U);
    const texas::solver::multiway::MultiwayInfosetId infoset{{7U}, 0};
    storage.admit_row({infoset, 2U, 3U});
    storage.apply_delta(infoset, 0U, 0U, 10'000'000.0, 2.0);
    storage.apply_delta(infoset, 0U, 1U, -10'000'000.0, 1.0);
    const auto current = storage.regret_matched_strategy(infoset, 0U);
    const auto average = storage.average_strategy(infoset, 0U);
    EXPECT_NEAR(current[0] + current[1] + current[2], 1.0, 1e-12);
    EXPECT_NEAR(average[0] + average[1] + average[2], 1.0, 1e-12);
    EXPECT_TRUE(current[0] > 0.99);
    EXPECT_NEAR(average[0], 2.0 / 3.0, 1e-6);
    EXPECT_TRUE(storage.memory_bytes() < 160U);
}

TEST_CASE(multiway_compact_storage_rejects_conflicting_rows) {
    texas::solver::multiway::MultiwayCompactStorage storage(1U, 3U);
    const texas::solver::multiway::MultiwayInfosetId infoset{{7U}, 0};
    storage.admit_row({infoset, 1U, 3U});
    EXPECT_THROW(storage.admit_row({infoset, 3U, 1U}), std::invalid_argument);
}

TEST_CASE(multiway_compact_storage_matches_reference_policy_shape) {
    const texas::solver::multiway::MultiwayInfosetId infoset{{9U}, 1};
    texas::solver::multiway::MultiwayCompactStorage compact(1U, 4U);
    compact.admit_row({infoset, 2U, 2U});
    compact.apply_delta(infoset, 0U, 0U, 1.0, 3.0);
    compact.apply_delta(infoset, 0U, 1U, -0.5, 1.0);
    EXPECT_TRUE(!compact.action_below_regret(infoset, 0U, 0U, 0.0));
    EXPECT_TRUE(compact.action_below_regret(infoset, 0U, 1U, 0.0));
    const auto current = compact.regret_matched_strategy(infoset, 0U);
    const auto average = compact.average_strategy(infoset, 0U);
    EXPECT_NEAR(current[0], 1.0, 1e-12);
    EXPECT_NEAR(current[1], 0.0, 1e-12);
    EXPECT_NEAR(average[0], 0.75, 1e-6);
    EXPECT_NEAR(average[1], 0.25, 1e-6);
}

TEST_CASE(multiway_compact_storage_allocation_free_policy_matches_vector_api) {
    const texas::solver::multiway::MultiwayInfosetId infoset{{10U}, 1};
    texas::solver::multiway::MultiwayCompactStorage compact(1U, 3U);
    compact.admit_row({infoset, 1U, 3U});
    compact.apply_delta(infoset, 0U, 0U, 3.0, 0.0);
    compact.apply_delta(infoset, 0U, 1U, 1.0, 0.0);
    compact.apply_delta(infoset, 0U, 2U, -2.0, 0.0);
    const auto expected = compact.regret_matched_strategy(infoset, 0U);
    std::array<texas::Probability, 3> actual{};
    compact.regret_matched_strategy_into(infoset, 0U, actual.data(), actual.size());
    for (std::size_t action = 0U; action < actual.size(); ++action) {
        EXPECT_NEAR(actual[action], expected[action], 0.0);
    }
    EXPECT_THROW(compact.regret_matched_strategy_into(infoset, 0U, actual.data(), 2U),
        std::invalid_argument);
}

TEST_CASE(multiway_compact_storage_pruning_keeps_configured_floor) {
    texas::solver::multiway::MultiwayCompactStorage storage(1U, 2U);
    const texas::solver::multiway::MultiwayInfosetId infoset{{11U}, 0};
    storage.admit_row({infoset, 1U, 2U});
    storage.apply_delta(infoset, 0U, 0U, -2.0, 0.0);
    storage.apply_delta(infoset, 0U, 1U, 1.0, 0.0);
    storage.prune_negative_regrets(-1.0, -0.25);
    const auto policy = storage.regret_matched_strategy(infoset, 0U);
    EXPECT_NEAR(policy[0], 0.0, 1e-12);
    EXPECT_NEAR(policy[1], 1.0, 1e-12);
}

TEST_CASE(multiway_compact_storage_saturates_extreme_finite_deltas) {
    texas::solver::multiway::MultiwayCompactStorage storage(1U, 1U);
    const texas::MultiwayInfosetId infoset{{1U}, 0};
    storage.admit_row({infoset, 1U, 1U});
    storage.apply_delta(infoset, 0U, 0U, 1.0e300, 1.0e300);
    EXPECT_TRUE(storage.regret_matched_strategy(infoset, 0U)[0] > 0.0);
    EXPECT_TRUE(storage.average_strategy(infoset, 0U)[0] == 1.0);
    storage.apply_delta(infoset, 0U, 0U, -1.0e300, 0.0);
    EXPECT_TRUE(storage.regret_matched_strategy(infoset, 0U)[0] == 1.0);
}
