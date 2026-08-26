#pragma once

#include <cstdint>
#include <istream>
#include <ostream>

namespace texas::core::portable {

inline void write_u8(std::ostream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
}

inline void write_u16(std::ostream& out, std::uint16_t value) {
    write_u8(out, static_cast<std::uint8_t>(value));
    write_u8(out, static_cast<std::uint8_t>(value >> 8U));
}

inline void write_u32(std::ostream& out, std::uint32_t value) {
    for (std::uint8_t shift = 0; shift < 32U; shift += 8U) {
        write_u8(out, static_cast<std::uint8_t>(value >> shift));
    }
}

inline void write_u64(std::ostream& out, std::uint64_t value) {
    for (std::uint8_t shift = 0; shift < 64U; shift += 8U) {
        write_u8(out, static_cast<std::uint8_t>(value >> shift));
    }
}

inline void write_i32(std::ostream& out, std::int32_t value) {
    write_u32(out, static_cast<std::uint32_t>(value));
}

inline bool read_u8(std::istream& in, std::uint8_t& value) {
    char byte = 0;
    if (!in.get(byte)) return false;
    value = static_cast<std::uint8_t>(static_cast<unsigned char>(byte));
    return true;
}

inline bool read_u16(std::istream& in, std::uint16_t& value) {
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    if (!read_u8(in, a) || !read_u8(in, b)) return false;
    value = static_cast<std::uint16_t>(a) |
        (static_cast<std::uint16_t>(b) << 8U);
    return true;
}

inline bool read_u32(std::istream& in, std::uint32_t& value) {
    value = 0;
    for (std::uint8_t shift = 0; shift < 32U; shift += 8U) {
        std::uint8_t byte = 0;
        if (!read_u8(in, byte)) return false;
        value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return true;
}

inline bool read_u64(std::istream& in, std::uint64_t& value) {
    value = 0;
    for (std::uint8_t shift = 0; shift < 64U; shift += 8U) {
        std::uint8_t byte = 0;
        if (!read_u8(in, byte)) return false;
        value |= static_cast<std::uint64_t>(byte) << shift;
    }
    return true;
}

inline bool read_i32(std::istream& in, std::int32_t& value) {
    std::uint32_t raw = 0;
    if (!read_u32(in, raw)) return false;
    value = static_cast<std::int32_t>(raw);
    return true;
}

}  // namespace texas::core::portable
