#include "solver/multiway_cfr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

TEST_CASE(multiway_numerical_contract_normalizes_extreme_finite_regrets) {
    const auto maximum = std::numeric_limits<double>::max();
    for (std::size_t actions = 2; actions <= 6; ++actions) {
        std::vector<double> regrets(actions, maximum);
        const auto strategy = texas::multiway_regret_matching(regrets);
        EXPECT_EQ(strategy.size(), actions);
        EXPECT_NEAR(std::accumulate(strategy.begin(), strategy.end(), 0.0), 1.0, 1e-12);
        for (const auto probability : strategy) {
            EXPECT_TRUE(std::isfinite(probability));
            EXPECT_TRUE(probability >= 0.0);
        }
    }
}

TEST_CASE(multiway_numerical_contract_handles_mixed_regret_scales) {
    const auto maximum = std::numeric_limits<double>::max();
    const auto strategy = texas::multiway_regret_matching({maximum, 1.0, maximum / 2.0, -maximum});
    EXPECT_NEAR(std::accumulate(strategy.begin(), strategy.end(), 0.0), 1.0, 1e-12);
    EXPECT_TRUE(strategy[0] > strategy[2]);
    EXPECT_EQ(strategy[1], 0.0);
    EXPECT_EQ(strategy[3], 0.0);
}

TEST_CASE(multiway_numerical_contract_rejects_nonfinite_sampled_updates_before_publication) {
    const auto maximum = std::numeric_limits<double>::max();
    for (const auto sign : {-1.0, 1.0}) {
        texas::MultiwayExternalSamplingRequest request;
        request.player_reaches = {1.0, 1.0};
        request.traverser = 0;
        request.chance_reach = 1.0;
        request.sampling_reach = 1.0;
        request.traverser_reach = 1.0;
        request.strategy = {1.0, 0.0};
        request.sampled_action_values = {sign * maximum, -sign * maximum};
        EXPECT_THROW(
            texas::make_multiway_external_sampling_cfr_update(request),
            std::overflow_error);
    }
}

TEST_CASE(multiway_numerical_contract_rejects_nonfinite_nashconv_accumulation) {
    const auto maximum = std::numeric_limits<double>::max();
    for (std::size_t seats = 2; seats <= 6; ++seats) {
        std::vector<double> profile(seats, -maximum);
        std::vector<double> best_response(seats, maximum);
        EXPECT_THROW(texas::compute_multiway_nash_conv(profile, best_response), std::overflow_error);
    }
}

TEST_CASE(multiway_numerical_contract_preserves_rows_when_an_addition_overflows) {
    const auto maximum = std::numeric_limits<double>::max();
    for (const auto delta : {1.0, maximum / 2.0, maximum}) {
        texas::MultiwayCFRUpdate update;
        update.regret_deltas = {delta};
        update.strategy_deltas = {0.0};
        std::vector<double> regrets = {maximum};
        std::vector<double> strategies = {3.0};
        EXPECT_THROW(texas::apply_multiway_cfr_update(regrets, strategies, update), std::overflow_error);
        EXPECT_EQ(regrets[0], maximum);
        EXPECT_EQ(strategies[0], 3.0);
    }
}
