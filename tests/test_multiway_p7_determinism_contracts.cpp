#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_solver.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

core::MultiwaySolverLimits valid_limits() {
    core::MultiwaySolverLimits limits;
    limits.seed = 0x5eedU;
    limits.worker_count = 4U;
    limits.trajectories_per_batch = 16U;
    limits.max_public_states = 8U;
    limits.max_sparse_rows = 8U;
    limits.max_sparse_values = 32U;
    limits.max_worker_delta_entries = 8U;
    return limits;
}

}  // namespace

TEST_CASE(multiway_p7_determinism_trajectory_seed_repeats_for_same_identity) {
    const auto first = core::multiway_deterministic_trajectory_seed(0x1234U, 17U);
    EXPECT_EQ(first, core::multiway_deterministic_trajectory_seed(0x1234U, 17U));
}

TEST_CASE(multiway_p7_determinism_trajectory_seed_changes_with_trajectory_zero_to_one) {
    EXPECT_TRUE(
        core::multiway_deterministic_trajectory_seed(9U, 0U) !=
        core::multiway_deterministic_trajectory_seed(9U, 1U));
}

TEST_CASE(multiway_p7_determinism_trajectory_seed_changes_with_trajectory_one_to_two) {
    EXPECT_TRUE(
        core::multiway_deterministic_trajectory_seed(9U, 1U) !=
        core::multiway_deterministic_trajectory_seed(9U, 2U));
}

TEST_CASE(multiway_p7_determinism_trajectory_seed_changes_with_base_seed) {
    EXPECT_TRUE(
        core::multiway_deterministic_trajectory_seed(9U, 7U) !=
        core::multiway_deterministic_trajectory_seed(10U, 7U));
}

TEST_CASE(multiway_p7_determinism_zero_seed_and_trajectory_are_stable) {
    const auto first = core::multiway_deterministic_trajectory_seed(0U, 0U);
    EXPECT_EQ(first, core::multiway_deterministic_trajectory_seed(0U, 0U));
}

TEST_CASE(multiway_p7_determinism_maximum_seed_and_trajectory_are_stable) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto first = core::multiway_deterministic_trajectory_seed(maximum, maximum);
    EXPECT_EQ(first, core::multiway_deterministic_trajectory_seed(maximum, maximum));
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_repeats_for_same_contract) {
    const auto first = core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U);
    EXPECT_EQ(first, core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U));
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_is_nonzero) {
    EXPECT_TRUE(core::multiway_deterministic_schedule_fingerprint(1U, 0U, 0U, 0U) != 0U);
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_changes_with_worker_count) {
    EXPECT_TRUE(
        core::multiway_deterministic_schedule_fingerprint(1U, 7U, 9U, 32U) !=
        core::multiway_deterministic_schedule_fingerprint(2U, 7U, 9U, 32U));
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_changes_with_base_seed) {
    EXPECT_TRUE(
        core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U) !=
        core::multiway_deterministic_schedule_fingerprint(4U, 8U, 9U, 32U));
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_changes_with_first_trajectory) {
    EXPECT_TRUE(
        core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U) !=
        core::multiway_deterministic_schedule_fingerprint(4U, 7U, 10U, 32U));
}

TEST_CASE(multiway_p7_determinism_schedule_fingerprint_changes_with_trajectory_count) {
    EXPECT_TRUE(
        core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 32U) !=
        core::multiway_deterministic_schedule_fingerprint(4U, 7U, 9U, 33U));
}

TEST_CASE(multiway_p7_determinism_metadata_defaults_to_deterministic_mode) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.mode, core::MultiwayRunMode::Deterministic);
}

TEST_CASE(multiway_p7_determinism_metadata_defaults_to_bitwise_deterministic) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_TRUE(metadata.bitwise_deterministic);
}

TEST_CASE(multiway_p7_determinism_metadata_defaults_to_zero_worker_count) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.worker_count, 0U);
}

TEST_CASE(multiway_p7_determinism_metadata_defaults_to_zero_schedule_identity) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.schedule_fingerprint, 0U);
}

TEST_CASE(multiway_p7_determinism_metadata_defaults_to_zero_merged_stream_identity) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.merged_stream_fingerprint, 0U);
}

TEST_CASE(multiway_p7_determinism_partition_version_is_published) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.partition_version, core::MULTIWAY_PARTITION_VERSION);
    EXPECT_TRUE(metadata.partition_version != 0U);
}

TEST_CASE(multiway_p7_determinism_trajectory_seed_version_is_published) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.trajectory_seed_version, core::MULTIWAY_TRAJECTORY_SEED_VERSION);
    EXPECT_TRUE(metadata.trajectory_seed_version != 0U);
}

TEST_CASE(multiway_p7_determinism_action_sampling_version_is_published) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.action_sampling_version, core::MULTIWAY_ACTION_SAMPLING_VERSION);
    EXPECT_TRUE(metadata.action_sampling_version != 0U);
}

TEST_CASE(multiway_p7_determinism_public_chance_order_version_is_published) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.public_chance_order_version, core::MULTIWAY_PUBLIC_CHANCE_ORDER_VERSION);
    EXPECT_TRUE(metadata.public_chance_order_version != 0U);
}

TEST_CASE(multiway_p7_determinism_merge_order_version_is_published) {
    const core::MultiwayRunMetadata metadata;
    EXPECT_EQ(metadata.merge_order_version, core::MULTIWAY_MERGE_ORDER_VERSION);
    EXPECT_TRUE(metadata.merge_order_version != 0U);
}

TEST_CASE(multiway_p7_determinism_limits_accept_complete_deterministic_configuration) {
    const auto limits = valid_limits();
    limits.validate();
    EXPECT_EQ(limits.run_mode, core::MultiwayRunMode::Deterministic);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_workers) {
    auto limits = valid_limits();
    limits.worker_count = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_trajectories_per_batch) {
    auto limits = valid_limits();
    limits.trajectories_per_batch = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_public_state_capacity) {
    auto limits = valid_limits();
    limits.max_public_states = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_sparse_row_capacity) {
    auto limits = valid_limits();
    limits.max_sparse_rows = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_sparse_value_capacity) {
    auto limits = valid_limits();
    limits.max_sparse_values = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_zero_worker_delta_capacity) {
    auto limits = valid_limits();
    limits.max_worker_delta_entries = 0U;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_limits_reject_aggregate_worker_delta_overflow) {
    auto limits = valid_limits();
    limits.worker_count = 2U;
    limits.max_worker_delta_entries = std::numeric_limits<std::size_t>::max();
    EXPECT_THROW(limits.validate(), std::overflow_error);
}

TEST_CASE(multiway_p7_determinism_limits_reject_unimplemented_run_mode) {
    auto limits = valid_limits();
    limits.run_mode = static_cast<core::MultiwayRunMode>(255U);
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_determinism_seed_does_not_change_limit_validity) {
    auto limits = valid_limits();
    limits.seed = std::numeric_limits<std::uint64_t>::max();
    limits.validate();
    EXPECT_EQ(limits.seed, std::numeric_limits<std::uint64_t>::max());
}
