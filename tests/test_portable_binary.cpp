#include "core/portable_binary.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <sstream>

TEST_CASE(portable_binary_round_trips_fixed_width_little_endian_values) {
    std::stringstream stream;
    texas::core::portable::write_u16(stream, 0x1234U);
    texas::core::portable::write_u32(stream, 0x12345678U);
    texas::core::portable::write_u64(stream, 0x0123456789abcdefULL);
    texas::core::portable::write_i32(stream, -7);

    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    std::int32_t i32 = 0;
    EXPECT_TRUE(texas::core::portable::read_u16(stream, u16));
    EXPECT_TRUE(texas::core::portable::read_u32(stream, u32));
    EXPECT_TRUE(texas::core::portable::read_u64(stream, u64));
    EXPECT_TRUE(texas::core::portable::read_i32(stream, i32));
    EXPECT_EQ(u16, 0x1234U);
    EXPECT_EQ(u32, 0x12345678U);
    EXPECT_EQ(u64, 0x0123456789abcdefULL);
    EXPECT_EQ(i32, -7);
}

TEST_CASE(portable_binary_rejects_truncated_values) {
    std::stringstream stream;
    texas::core::portable::write_u8(stream, 0x42U);
    std::uint32_t value = 0;
    EXPECT_TRUE(!texas::core::portable::read_u32(stream, value));
}
