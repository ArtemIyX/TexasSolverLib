#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_simd.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace {

constexpr double TOL = 1e-6;

TEST_CASE(hunl_sampled_config_defaults_validate) {
    const core::HUNLSampledSolverConfig config;
    const auto validation = core::validate_sampled_config(config);

    EXPECT_TRUE(validation.ok);
    EXPECT_EQ(config.mode, core::HUNLFlatSamplingMode::External);
    EXPECT_EQ(config.precision, core::HUNLFlatStoragePrecision::Float32);
    EXPECT_EQ(config.layout, core::HUNLFlatValueLayout::InfosetActionHand);
    EXPECT_TRUE(config.lazy_public_expansion);
    EXPECT_TRUE(config.sparse_infosets);
}

TEST_CASE(hunl_flat_mccfr_config_defaults_match_external_sampling_baseline) {
    const core::HUNLFlatMCCFRConfig config;

    EXPECT_EQ(config.mode, core::HUNLFlatSamplingMode::External);
    EXPECT_EQ(config.seed, 1U);
    EXPECT_EQ(config.traversals_per_iteration, 1024U);
    EXPECT_EQ(config.batch_size, 64U);
    EXPECT_TRUE(config.update_both_players);
    EXPECT_TRUE(!config.use_discounting);
}

TEST_CASE(hunl_sampled_storage_allocates_one_sparse_row) {
    core::HUNLSampledStorage storage;
    const auto row = storage.ensure_row({
        core::InfosetId{7},
        1,
        core::Street::Turn,
        3,
        2,
    });

    EXPECT_EQ(storage.row_count(), 1U);
    EXPECT_EQ(storage.total_value_count(), 6U);
    EXPECT_TRUE(!row.empty());
    EXPECT_EQ(row.bucket_count, 3U);
    EXPECT_EQ(row.action_count, 2U);
    EXPECT_EQ(row.regret[0], 0.0f);
    EXPECT_EQ(row.strategy_sum[5], 0.0f);
}

TEST_CASE(hunl_sampled_scheduler_partitions_trajectories_deterministically) {
    const auto first = core::HUNLSampledScheduler::partition_deterministic(10, 3);
    const auto second = core::HUNLSampledScheduler::partition_deterministic(10, 3);

    EXPECT_EQ(first.size(), 3U);
    EXPECT_EQ(first[0].trajectories.begin, 0U);
    EXPECT_EQ(first[0].trajectories.end, 4U);
    EXPECT_EQ(first[1].trajectories.begin, 4U);
    EXPECT_EQ(first[1].trajectories.end, 7U);
    EXPECT_EQ(first[2].trajectories.begin, 7U);
    EXPECT_EQ(first[2].trajectories.end, 10U);

    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].worker_index, second[i].worker_index);
        EXPECT_EQ(first[i].trajectories.begin, second[i].trajectories.begin);
        EXPECT_EQ(first[i].trajectories.end, second[i].trajectories.end);
    }
}

TEST_CASE(hunl_sampled_simd_scalar_reference_kernels_match_hand_computed_rows) {
    const std::array<float, 6> regret = {1.0f, -2.0f, 3.0f, 3.0f, 2.0f, -1.0f};
    std::array<float, 6> strategy = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::regret_matching_action_major_f32(regret.data(), 2, 3, strategy.data());

    EXPECT_NEAR(strategy[0], 0.25, TOL);
    EXPECT_NEAR(strategy[3], 0.75, TOL);
    EXPECT_NEAR(strategy[1], 0.0, TOL);
    EXPECT_NEAR(strategy[4], 1.0, TOL);
    EXPECT_NEAR(strategy[2], 1.0, TOL);
    EXPECT_NEAR(strategy[5], 0.0, TOL);

    const std::array<float, 3> reach = {2.0f, 4.0f, 1.0f};
    std::array<float, 6> strategy_sum = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::accumulate_average_strategy_action_major_f32(
        strategy.data(),
        reach.data(),
        2,
        3,
        0.5f,
        strategy_sum.data());

    EXPECT_NEAR(strategy_sum[0], 0.25, TOL);
    EXPECT_NEAR(strategy_sum[3], 0.75, TOL);
    EXPECT_NEAR(strategy_sum[1], 0.0, TOL);
    EXPECT_NEAR(strategy_sum[4], 2.0, TOL);
    EXPECT_NEAR(strategy_sum[2], 0.5, TOL);
    EXPECT_NEAR(strategy_sum[5], 0.0, TOL);

    const std::array<float, 6> action_values = {2.0f, 5.0f, 4.0f, 6.0f, 1.0f, 3.0f};
    const std::array<float, 3> node_values = {4.0f, 3.0f, 2.0f};
    const std::array<float, 3> cf_reach = {1.0f, 0.5f, 2.0f};
    std::array<float, 6> regret_delta = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::add_regret_delta_action_major_f32(
        action_values.data(),
        node_values.data(),
        cf_reach.data(),
        2,
        3,
        regret_delta.data());

    EXPECT_NEAR(regret_delta[0], -2.0, TOL);
    EXPECT_NEAR(regret_delta[1], 1.0, TOL);
    EXPECT_NEAR(regret_delta[2], 4.0, TOL);
    EXPECT_NEAR(regret_delta[3], 2.0, TOL);
    EXPECT_NEAR(regret_delta[4], -1.0, TOL);
    EXPECT_NEAR(regret_delta[5], 2.0, TOL);
}

TEST_CASE(hunl_sampled_profile_formats_summary_into_caller_buffer) {
    core::HUNLSampledProfile profile;
    profile.record_traversal(128, 4096, 64);
    profile.record_sparse_storage(12, 768);
    profile.add_traverse_seconds(0.25);
    profile.add_merge_seconds(0.05);

    std::array<char, 256> buffer = {};
    const auto written = profile.format_summary(buffer.data(), buffer.size());

    EXPECT_TRUE(written > 0);
    EXPECT_TRUE(std::strstr(buffer.data(), "traversals=128") != nullptr);
    EXPECT_TRUE(std::strstr(buffer.data(), "sparse_rows=12") != nullptr);
    EXPECT_TRUE(std::strstr(buffer.data(), "t_merge=0.050000") != nullptr);
}

}  // namespace
