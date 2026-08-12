#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_solver.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace {

texas::MultiwayWorkerDelta worker_delta(
    std::int32_t seat = 0,
    std::uint64_t public_state = 1U,
    std::uint8_t action = 0U,
    std::uint32_t bucket = 0U,
    std::uint64_t trajectory = 0U) {
    texas::MultiwayWorkerDelta result;
    result.infoset = {{public_state}, seat};
    result.action = action;
    result.bucket = bucket;
    result.trajectory_id = trajectory;
    result.regret = 1.0;
    result.strategy_sum = 2.0;
    return result;
}

std::array<texas::MultiwayWorkerBatch, 8U> partition(
    std::uint64_t trajectories,
    std::size_t workers,
    std::size_t& count) {
    std::array<texas::MultiwayWorkerBatch, 8U> batches{};
    count = texas::MultiwayScheduler::partition_deterministic_into(
        trajectories, workers, batches.data(), batches.size());
    return batches;
}

}  // namespace

TEST_CASE(multiway_p7_hot_path_empty_partition_writes_one_empty_batch) {
    std::size_t count = 0U;
    const auto batches = partition(0U, 4U, count);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(batches[0].trajectories.size(), 0U);
}

TEST_CASE(multiway_p7_hot_path_empty_vector_partition_has_one_batch) {
    const auto batches = texas::MultiwayScheduler::partition_deterministic(0U, 4U);
    EXPECT_EQ(batches.size(), 1U);
    EXPECT_EQ(batches[0].worker_index, 0U);
}

TEST_CASE(multiway_p7_hot_path_zero_workers_defaults_to_one) {
    std::size_t count = 0U;
    const auto batches = partition(5U, 0U, count);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(batches[0].trajectories.size(), 5U);
}

TEST_CASE(multiway_p7_hot_path_workers_are_clamped_to_trajectory_count) {
    std::size_t count = 0U;
    const auto batches = partition(2U, 8U, count);
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(batches[1].trajectories.end, 2U);
}

TEST_CASE(multiway_p7_hot_path_workers_are_clamped_to_output_capacity) {
    std::array<texas::MultiwayWorkerBatch, 2U> batches{};
    const auto count = texas::MultiwayScheduler::partition_deterministic_into(
        10U, 8U, batches.data(), batches.size());
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(batches[0].trajectories.size(), 5U);
}

TEST_CASE(multiway_p7_hot_path_null_partition_output_is_rejected) {
    EXPECT_EQ(texas::MultiwayScheduler::partition_deterministic_into(3U, 2U, nullptr, 2U), 0U);
}

TEST_CASE(multiway_p7_hot_path_zero_partition_capacity_is_rejected) {
    texas::MultiwayWorkerBatch batch{};
    EXPECT_EQ(texas::MultiwayScheduler::partition_deterministic_into(3U, 2U, &batch, 0U), 0U);
}

TEST_CASE(multiway_p7_hot_path_partition_distributes_remainder_to_first_workers) {
    std::size_t count = 0U;
    const auto batches = partition(10U, 3U, count);
    EXPECT_EQ(batches[0].trajectories.size(), 4U);
    EXPECT_EQ(batches[1].trajectories.size(), 3U);
    EXPECT_EQ(batches[2].trajectories.size(), 3U);
}

TEST_CASE(multiway_p7_hot_path_partition_17_by_4_is_balanced) {
    std::size_t count = 0U;
    const auto batches = partition(17U, 4U, count);
    EXPECT_EQ(batches[0].trajectories.size(), 5U);
    EXPECT_EQ(batches[3].trajectories.size(), 4U);
}

TEST_CASE(multiway_p7_hot_path_partition_worker_indices_are_dense) {
    std::size_t count = 0U;
    const auto batches = partition(7U, 4U, count);
    for (std::size_t index = 0U; index < count; ++index) {
        EXPECT_EQ(batches[index].worker_index, index);
    }
}

TEST_CASE(multiway_p7_hot_path_partition_ranges_are_contiguous) {
    std::size_t count = 0U;
    const auto batches = partition(7U, 4U, count);
    for (std::size_t index = 1U; index < count; ++index) {
        EXPECT_EQ(batches[index - 1U].trajectories.end, batches[index].trajectories.begin);
    }
}

TEST_CASE(multiway_p7_hot_path_partition_covers_every_trajectory) {
    std::size_t count = 0U;
    const auto batches = partition(31U, 6U, count);
    EXPECT_EQ(batches[0].trajectories.begin, 0U);
    EXPECT_EQ(batches[count - 1U].trajectories.end, 31U);
}

TEST_CASE(multiway_p7_hot_path_single_trajectory_uses_single_worker) {
    std::size_t count = 0U;
    const auto batches = partition(1U, 8U, count);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(batches[0].trajectories.end, 1U);
}

TEST_CASE(multiway_p7_hot_path_single_worker_supports_maximum_trajectory_id_range) {
    std::size_t count = 0U;
    const auto batches = partition(std::numeric_limits<std::uint64_t>::max(), 1U, count);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(batches[0].trajectories.end, std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE(multiway_p7_hot_path_vector_partition_matches_caller_owned_partition) {
    std::size_t count = 0U;
    const auto direct = partition(13U, 4U, count);
    const auto owned = texas::MultiwayScheduler::partition_deterministic(13U, 4U);
    for (std::size_t index = 0U; index < count; ++index) {
        EXPECT_EQ(direct[index].trajectories.begin, owned[index].trajectories.begin);
        EXPECT_EQ(direct[index].trajectories.end, owned[index].trajectories.end);
    }
}

TEST_CASE(multiway_p7_hot_path_partition_does_not_touch_unused_slots) {
    std::array<texas::MultiwayWorkerBatch, 4U> batches{};
    batches[3].worker_index = 99U;
    (void)texas::MultiwayScheduler::partition_deterministic_into(2U, 4U, batches.data(), batches.size());
    EXPECT_EQ(batches[3].worker_index, 99U);
}

TEST_CASE(multiway_p7_hot_path_partition_is_repeatable_in_reused_storage) {
    std::array<texas::MultiwayWorkerBatch, 4U> batches{};
    (void)texas::MultiwayScheduler::partition_deterministic_into(17U, 4U, batches.data(), batches.size());
    const auto expected = batches;
    (void)texas::MultiwayScheduler::partition_deterministic_into(17U, 4U, batches.data(), batches.size());
    EXPECT_EQ(batches[2].trajectories.begin, expected[2].trajectories.begin);
    EXPECT_EQ(batches[2].trajectories.end, expected[2].trajectories.end);
}

TEST_CASE(multiway_p7_hot_path_delta_stream_exposes_fixed_capacity_metadata) {
    const texas::MultiwayWorkerDeltaStream stream(7U, 12U);
    EXPECT_EQ(stream.worker_index(), 7U);
    EXPECT_EQ(stream.capacity(), 12U);
    EXPECT_EQ(stream.size(), 0U);
}

TEST_CASE(multiway_p7_hot_path_zero_capacity_stream_rejects_append) {
    texas::MultiwayWorkerDeltaStream stream(0U, 0U);
    EXPECT_TRUE(!stream.try_append(worker_delta()));
    EXPECT_EQ(stream.size(), 0U);
}

TEST_CASE(multiway_p7_hot_path_stream_accepts_finite_delta) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    EXPECT_TRUE(stream.try_append(worker_delta()));
    EXPECT_EQ(stream.size(), 1U);
}

TEST_CASE(multiway_p7_hot_path_stream_accepts_negative_finite_delta) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    auto delta = worker_delta();
    delta.regret = -3.5;
    delta.strategy_sum = -1.0;
    EXPECT_TRUE(stream.try_append(delta));
}

TEST_CASE(multiway_p7_hot_path_stream_rejects_nan_regret) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    auto delta = worker_delta();
    delta.regret = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(!stream.try_append(delta));
}

TEST_CASE(multiway_p7_hot_path_stream_rejects_infinite_regret) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    auto delta = worker_delta();
    delta.regret = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(!stream.try_append(delta));
}

TEST_CASE(multiway_p7_hot_path_stream_rejects_nan_strategy_sum) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    auto delta = worker_delta();
    delta.strategy_sum = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(!stream.try_append(delta));
}

TEST_CASE(multiway_p7_hot_path_stream_rejects_infinite_strategy_sum) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    auto delta = worker_delta();
    delta.strategy_sum = -std::numeric_limits<double>::infinity();
    EXPECT_TRUE(!stream.try_append(delta));
}

TEST_CASE(multiway_p7_hot_path_full_stream_rejects_without_growth) {
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    EXPECT_TRUE(stream.try_append(worker_delta()));
    EXPECT_TRUE(!stream.try_append(worker_delta()));
    EXPECT_EQ(stream.size(), 1U);
}

TEST_CASE(multiway_p7_hot_path_stream_rewind_truncates_entries) {
    texas::MultiwayWorkerDeltaStream stream(0U, 3U);
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 0U, 0U, 1U)));
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 0U, 0U, 2U)));
    stream.rewind(1U);
    EXPECT_EQ(stream.size(), 1U);
    EXPECT_EQ(stream.deltas()[0].trajectory_id, 1U);
}

TEST_CASE(multiway_p7_hot_path_stream_rewind_cannot_grow_entries) {
    texas::MultiwayWorkerDeltaStream stream(0U, 3U);
    EXPECT_TRUE(stream.try_append(worker_delta()));
    stream.rewind(2U);
    EXPECT_EQ(stream.size(), 1U);
}

TEST_CASE(multiway_p7_hot_path_empty_stream_is_fixed_order) {
    const texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    EXPECT_TRUE(stream.is_fixed_order());
}

TEST_CASE(multiway_p7_hot_path_stream_detects_unsorted_seats) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(1)));
    EXPECT_TRUE(stream.try_append(worker_delta(0)));
    EXPECT_TRUE(!stream.is_fixed_order());
}

TEST_CASE(multiway_p7_hot_path_stream_sorts_by_seat) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(1)));
    EXPECT_TRUE(stream.try_append(worker_delta(0)));
    stream.sort_fixed_order();
    EXPECT_EQ(stream.deltas()[0].infoset.seat, 0);
}

TEST_CASE(multiway_p7_hot_path_stream_sorts_by_public_state) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(0, 9U)));
    EXPECT_TRUE(stream.try_append(worker_delta(0, 3U)));
    stream.sort_fixed_order();
    EXPECT_EQ(stream.deltas()[0].infoset.public_state.value, 3U);
}

TEST_CASE(multiway_p7_hot_path_stream_sorts_by_action_before_bucket) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 2U, 0U)));
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 1U, 9U)));
    stream.sort_fixed_order();
    EXPECT_EQ(stream.deltas()[0].action, 1U);
}

TEST_CASE(multiway_p7_hot_path_stream_sorts_by_bucket_after_action) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 1U, 9U)));
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 1U, 2U)));
    stream.sort_fixed_order();
    EXPECT_EQ(stream.deltas()[0].bucket, 2U);
}

TEST_CASE(multiway_p7_hot_path_stream_sorts_by_trajectory_last) {
    texas::MultiwayWorkerDeltaStream stream(0U, 2U);
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 1U, 2U, 8U)));
    EXPECT_TRUE(stream.try_append(worker_delta(0, 1U, 1U, 2U, 3U)));
    stream.sort_fixed_order();
    EXPECT_EQ(stream.deltas()[0].trajectory_id, 3U);
    EXPECT_TRUE(stream.is_fixed_order());
}
