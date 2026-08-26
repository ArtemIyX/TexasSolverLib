#include "util/iteration_range.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace {

TEST_CASE(iteration_range_covers_twenty_low_closed_ranges) {
    for (std::uint32_t target = 1U; target <= 20U; ++target) {
        const auto width = target % 5U;
        const auto last = target - width;
        std::vector<std::uint32_t> visited;
        texas::util::for_each_u32_after(
            last,
            target,
            [&](std::uint32_t value) { visited.push_back(value); });

        EXPECT_EQ(
            visited.size(),
            static_cast<std::size_t>(target - last));
        if (!visited.empty()) {
            EXPECT_EQ(visited.front(), last + 1U);
            EXPECT_EQ(visited.back(), target);
        }
    }
}

TEST_CASE(iteration_range_covers_twenty_maximum_boundary_ranges) {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t scenario = 0U; scenario < 20U; ++scenario) {
        const auto target = maximum - scenario;
        const auto width = scenario % 5U;
        const auto last = target - width;
        std::vector<std::uint32_t> visited;
        texas::util::for_each_u32_after(
            last,
            target,
            [&](std::uint32_t value) { visited.push_back(value); });

        EXPECT_EQ(
            visited.size(),
            static_cast<std::size_t>(width));
        if (!visited.empty()) {
            EXPECT_EQ(
                visited.front(),
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(last) + 1U));
            EXPECT_EQ(visited.back(), target);
        }
    }
}

TEST_CASE(iteration_range_processes_uint32_max_once_and_then_saturates) {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> visited;
    texas::util::for_each_u32_after(
        maximum - 1U,
        maximum,
        [&](std::uint32_t value) { visited.push_back(value); });
    EXPECT_EQ(visited, std::vector<std::uint32_t>({maximum}));

    visited.clear();
    texas::util::for_each_u32_after(
        maximum,
        maximum,
        [&](std::uint32_t value) { visited.push_back(value); });
    EXPECT_TRUE(visited.empty());
}

TEST_CASE(iteration_counter_accepts_twenty_premaximum_values_and_rejects_exhaustion) {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t distance = 1U; distance <= 20U; ++distance) {
        EXPECT_EQ(
            texas::util::checked_next_u32_iteration(maximum - distance),
            maximum - distance + 1U);
    }
    EXPECT_THROW(
        texas::util::checked_next_u32_iteration(maximum),
        std::overflow_error);
}

}  // namespace
