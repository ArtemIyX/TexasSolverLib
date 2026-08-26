#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "test_harness.hpp"

#include <array>
#include <vector>

TEST_CASE(hunl_sampled_stable_config_and_storage_contract) {
    texas::HUNLSampledSolverConfig config;
    texas::validate_sampled_config_or_throw(config);

    texas::HUNLSampledStorage storage;
    storage.ensure_row({texas::InfosetId{1}, 0, texas::Street::Flop, 3, 2});
    const auto row = storage.view(texas::InfosetId{1});
    EXPECT_EQ(row.action_count, 2U);
    EXPECT_EQ(row.bucket_count, 3U);
    EXPECT_EQ(storage.row_count(), 1U);
}

TEST_CASE(hunl_sampled_stable_scheduler_partitions_deterministically) {
    const auto first = texas::HUNLSampledScheduler::partition_deterministic(17U, 4U);
    const auto second = texas::HUNLSampledScheduler::partition_deterministic(17U, 4U);
    EXPECT_EQ(first.size(), second.size());
    EXPECT_EQ(first.front().trajectories.begin, 0U);
    EXPECT_EQ(first.back().trajectories.end, 17U);
    for (std::size_t index = 0; index < first.size(); ++index) {
        EXPECT_EQ(first[index].trajectories.begin, second[index].trajectories.begin);
        EXPECT_EQ(first[index].trajectories.end, second[index].trajectories.end);
    }
}

TEST_CASE(hunl_sampled_stable_profile_uses_caller_buffer) {
    texas::HUNLSampledProfile profile;
    profile.record_traversal(2U, 3U, 4U);
    std::array<char, 128> buffer = {};
    const auto size = profile.format_summary(buffer.data(), buffer.size());
    EXPECT_TRUE(size > 0U);
    EXPECT_TRUE(size < buffer.size());
}
