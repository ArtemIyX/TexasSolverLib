#pragma once

/** @file
 * @brief Declares byte-wise helpers for building 64-bit FNV-1a fingerprints.
 */

#include <cstdint>

/** @brief FNV-1a fingerprint construction helpers. */
namespace texas::core::fingerprint
{

	/// FNV-1a 64-bit offset basis used to initialize a fingerprint.
	inline constexpr std::uint64_t FNV1A_OFFSET = 14695981039346656037ULL;
	/// FNV-1a 64-bit prime multiplied into the fingerprint after each byte.
	inline constexpr std::uint64_t FNV1A_PRIME = 1099511628211ULL;

	/** @brief Appends one byte to an FNV-1a fingerprint in place.
	 * @param hash Fingerprint accumulator to update.
	 * @param value Byte to append.
	 */
	inline void append_u8(std::uint64_t& hash, std::uint8_t value) noexcept
	{
		hash ^= value;
		hash *= FNV1A_PRIME;
	}

	/** @brief Appends a 64-bit integer least-significant byte first.
	 * @param hash Fingerprint accumulator to update.
	 * @param value Integer whose eight bytes are appended.
	 */
	inline void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept
	{
		for (std::uint8_t byte = 0U; byte < 8U; ++byte)
		{
			append_u8(hash, static_cast<std::uint8_t>(value >> (byte * 8U)));
		}
	}

	/** @brief Finalizes a fingerprint while reserving zero as an unavailable value.
	 * @param hash Accumulator after all input bytes have been appended.
	 * @return hash, except that zero is returned as one.
	 */
	[[nodiscard]] inline std::uint64_t finish(std::uint64_t hash) noexcept
	{
		return hash == 0U ? 1U : hash;
	}

} // namespace texas::core::fingerprint
