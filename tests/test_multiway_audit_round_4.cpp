#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_memory.hpp"
#include "test_harness.hpp"

#include <limits>

TEST_CASE(multiway_audit_round_4_scales_continuation_regrets_by_importance_weight) {
    texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    const texas::MultiwayContinuationSelectionKey key = {
        {71U}, 0, texas::Street::Turn, 4U, 3U, 9U,
    };
    selector.update_regrets_weighted(key, {1.0, 0.0, 0.0, 0.0},
        {0.0, 2.0, 0.0, 0.0}, 0.25);
    const auto mixture = selector.strategy(key);
    EXPECT_NEAR(mixture[1], 1.0, 1e-12);
}

TEST_CASE(multiway_audit_round_4_preflight_counts_continuation_storage) {
    texas::MultiwaySolverLimits limits;
    limits.worker_count = 2U;
    limits.trajectories_per_batch = 1U;
    limits.max_public_states = 1U;
    limits.max_sparse_rows = 1U;
    limits.max_sparse_values = 1U;
    limits.max_worker_delta_entries = 3U;
    const auto result = texas::preflight_multiway_memory(
        limits, {std::numeric_limits<std::uint64_t>::max(),
                 std::numeric_limits<std::uint64_t>::max()});
    const auto continuation_bytes = static_cast<std::uint64_t>(
        limits.worker_count * limits.max_worker_delta_entries * sizeof(texas::MultiwayContinuationDelta));
    EXPECT_TRUE(result.estimate.worker_delta_bytes >= continuation_bytes);
    EXPECT_TRUE(result.estimate.merge_scratch_bytes >=
        static_cast<std::uint64_t>(limits.worker_count * limits.max_worker_delta_entries *
                                   (sizeof(texas::MultiwayContinuationDelta) + sizeof(void*))));
}
