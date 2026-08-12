#include "solver/multiway_resolver_budget.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

texas::MultiwayResolverBudgetConfig valid_budget_config() {
    texas::MultiwayResolverBudgetConfig config;
    config.external_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    config.max_batches = 2U;
    config.trajectories_per_batch = 3U;
    config.max_sparse_rows = 8U;
    config.max_sparse_values = 64U;
    return config;
}

}  // namespace

TEST_CASE(multiway_resolver_budget_accepts_clean_batches_within_limits) {
    texas::MultiwayResolverBudget budget(valid_budget_config());

    EXPECT_EQ(
        budget.checkpoint(0U, 0U),
        texas::MultiwayResolverBudgetCheckpoint::Ready);
    EXPECT_TRUE(budget.accept_clean_batch(true, 3U, 6U, 2U, 16U));
    EXPECT_EQ(budget.clean_batches(), 1U);
    EXPECT_TRUE(!budget.cancelled());
}

TEST_CASE(multiway_resolver_budget_rejects_incomplete_or_oversized_batches) {
    auto config = valid_budget_config();
    texas::MultiwayResolverBudget budget(config);

    EXPECT_TRUE(!budget.accept_clean_batch(false, 3U, 6U, 2U, 16U));
    EXPECT_TRUE(budget.cancelled());

    texas::MultiwayResolverBudget oversized(valid_budget_config());
    EXPECT_TRUE(!oversized.accept_clean_batch(true, 3U, 6U, 9U, 16U));
    EXPECT_TRUE(oversized.cancelled());

    texas::MultiwayResolverBudget explicitly_cancelled(valid_budget_config());
    explicitly_cancelled.cancel();
    EXPECT_TRUE(!explicitly_cancelled.accept_clean_batch(true, 3U, 6U, 2U, 16U));
}

TEST_CASE(multiway_resolver_budget_reports_deadline_and_trajectory_limits) {
    auto expired = valid_budget_config();
    expired.external_deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    texas::MultiwayResolverBudget expired_budget(expired);
    EXPECT_EQ(
        expired_budget.checkpoint(0U, 0U),
        texas::MultiwayResolverBudgetCheckpoint::DeadlineExpired);
    EXPECT_TRUE(expired_budget.deadline_expired());

    auto limited = valid_budget_config();
    texas::MultiwayResolverBudget limited_budget(limited);
    EXPECT_EQ(
        limited_budget.checkpoint(2U, 6U),
        texas::MultiwayResolverBudgetCheckpoint::LimitReached);
}

TEST_CASE(multiway_resolver_budget_validates_required_limits) {
    auto invalid = valid_budget_config();
    invalid.max_batches = 0U;
    EXPECT_THROW(texas::MultiwayResolverBudget(invalid), std::invalid_argument);

    invalid = valid_budget_config();
    invalid.external_deadline = std::chrono::steady_clock::time_point{};
    EXPECT_THROW(texas::MultiwayResolverBudget(invalid), std::invalid_argument);
}
