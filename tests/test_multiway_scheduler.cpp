#include "solver/multiway_scheduler.hpp"
#include "test_harness.hpp"

TEST_CASE(multiway_scheduler_partitions_trajectories_in_fixed_order) {
    const auto batches = core::MultiwayScheduler::partition_deterministic(10U, 3U);
    EXPECT_EQ(batches.size(), 3U);
    EXPECT_EQ(batches[0].trajectories.begin, 0U);
    EXPECT_EQ(batches[0].trajectories.end, 4U);
    EXPECT_EQ(batches[1].trajectories.begin, 4U);
    EXPECT_EQ(batches[1].trajectories.end, 7U);
    EXPECT_EQ(batches[2].trajectories.begin, 7U);
    EXPECT_EQ(batches[2].trajectories.end, 10U);
}

TEST_CASE(multiway_scheduler_handles_empty_and_oversized_worker_requests) {
    const auto empty = core::MultiwayScheduler::partition_deterministic(0U, 0U);
    EXPECT_EQ(empty.size(), 1U);
    EXPECT_EQ(empty[0].trajectories.size(), 0U);

    const auto clamped = core::MultiwayScheduler::partition_deterministic(2U, 100U);
    EXPECT_EQ(clamped.size(), 2U);
    EXPECT_EQ(clamped[0].trajectories.size(), 1U);
    EXPECT_EQ(clamped[1].trajectories.size(), 1U);
}
