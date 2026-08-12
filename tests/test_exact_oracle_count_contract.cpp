#include "util/checked_numeric.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

TEST_CASE(exact_oracle_count_contract_preserves_all_representable_u32_counts) {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    for (const auto count : {std::size_t{0}, std::size_t{1}, maximum / 2, maximum - 1, maximum}) {
        EXPECT_EQ(texas::util::detail::checked_u32_count(count, "test"), static_cast<std::uint32_t>(count));
    }
}

TEST_CASE(exact_oracle_count_contract_rejects_each_first_u32_overflow_shape) {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    for (std::size_t extra = 1; extra <= 20; ++extra) {
        EXPECT_THROW(texas::util::detail::checked_u32_count(maximum + extra, "test"), std::overflow_error);
    }
}

TEST_CASE(exact_oracle_count_contract_checks_arena_additions_without_allocating) {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    for (std::size_t right = 1; right <= 20; ++right) {
        EXPECT_THROW(texas::util::detail::checked_size_add(maximum, right, "test"), std::overflow_error);
    }
    EXPECT_EQ(texas::util::detail::checked_size_add(40, 2, "test"), 42U);
}
