#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "test_harness.hpp"

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
