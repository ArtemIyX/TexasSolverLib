#include "core/fingerprint.hpp"
#include "test_harness.hpp"

TEST_CASE(core_fingerprint_uses_one_stable_u64_encoding) {
    auto first = texas::core::fingerprint::FNV1A_OFFSET;
    auto second = texas::core::fingerprint::FNV1A_OFFSET;
    texas::core::fingerprint::append_u64(first, 1U);
    texas::core::fingerprint::append_u64(second, 1U);

    EXPECT_EQ(first, second);
    EXPECT_TRUE(first != texas::core::fingerprint::FNV1A_OFFSET);
    EXPECT_EQ(texas::core::fingerprint::finish(0U), 1U);
}
