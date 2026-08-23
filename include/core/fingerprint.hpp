#pragma once

#include "core/namespaces.hpp"

#include <cstdint>

namespace texas::core::fingerprint {

inline constexpr std::uint64_t FNV1A_OFFSET = 14695981039346656037ULL;
inline constexpr std::uint64_t FNV1A_PRIME = 1099511628211ULL;

inline void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= FNV1A_PRIME;
    }
}

[[nodiscard]] inline std::uint64_t finish(std::uint64_t hash) noexcept {
    return hash == 0U ? 1U : hash;
}

}  // namespace texas::core::fingerprint
