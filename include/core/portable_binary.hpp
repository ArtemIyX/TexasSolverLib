#pragma once

/** @file
 * @brief Declares little-endian binary readers and writers for fixed-width integers.
 */

#include <cstdint>
#include <istream>
#include <ostream>

/** @brief Fixed-width integer serialization helpers using little-endian byte order. */
namespace texas::core::portable
{

	/** @brief Writes one byte to a stream.
	 * @param out Destination stream.
	 * @param value Byte value to write.
	 */
	inline void write_u8(std::ostream& out, std::uint8_t value)
	{
		out.put(static_cast<char>(value));
	}

	/** @brief Writes a 16-bit unsigned integer least-significant byte first.
	 * @param out Destination stream.
	 * @param value Integer to write.
	 */
	inline void write_u16(std::ostream& out, std::uint16_t value)
	{
		write_u8(out, static_cast<std::uint8_t>(value));
		write_u8(out, static_cast<std::uint8_t>(value >> 8U));
	}

	/** @brief Writes a 32-bit unsigned integer least-significant byte first.
	 * @param out Destination stream.
	 * @param value Integer to write.
	 */
	inline void write_u32(std::ostream& out, std::uint32_t value)
	{
		for (std::uint8_t shift = 0; shift < 32U; shift += 8U)
		{
			write_u8(out, static_cast<std::uint8_t>(value >> shift));
		}
	}

	/** @brief Writes a 64-bit unsigned integer least-significant byte first.
	 * @param out Destination stream.
	 * @param value Integer to write.
	 */
	inline void write_u64(std::ostream& out, std::uint64_t value)
	{
		for (std::uint8_t shift = 0; shift < 64U; shift += 8U)
		{
			write_u8(out, static_cast<std::uint8_t>(value >> shift));
		}
	}

	/** @brief Writes the uint32_t conversion of a signed 32-bit value in little-endian order.
	 * @param out Destination stream.
	 * @param value Signed integer to convert and write.
	 */
	inline void write_i32(std::ostream& out, std::int32_t value)
	{
		write_u32(out, static_cast<std::uint32_t>(value));
	}

	/** @brief Reads one byte from a stream.
	 * @param in Source stream.
	 * @param[out] value Receives the byte on success and is unchanged on failure.
	 * @return True if one byte was read; false if the stream could not provide it.
	 */
	inline bool read_u8(std::istream& in, std::uint8_t& value)
	{
		char byte = 0;
		if (!in.get(byte))
			return false;
		value = static_cast<std::uint8_t>(static_cast<unsigned char>(byte));
		return true;
	}

	/** @brief Reads a 16-bit unsigned integer in little-endian order.
	 * @param in Source stream.
	 * @param[out] value Receives the integer on success and is unchanged on failure.
	 * @return True if two bytes were read; false otherwise.
	 */
	inline bool read_u16(std::istream& in, std::uint16_t& value)
	{
		std::uint8_t a = 0;
		std::uint8_t b = 0;
		if (!read_u8(in, a) || !read_u8(in, b))
			return false;
		value = static_cast<std::uint16_t>(a) | (static_cast<std::uint16_t>(b) << 8U);
		return true;
	}

	/** @brief Reads a 32-bit unsigned integer in little-endian order.
	 * @param in Source stream.
	 * @param[out] value Receives the decoded prefix; it is zeroed before reading and may be partial on failure.
	 * @return True if four bytes were read; false otherwise.
	 */
	inline bool read_u32(std::istream& in, std::uint32_t& value)
	{
		value = 0;
		for (std::uint8_t shift = 0; shift < 32U; shift += 8U)
		{
			std::uint8_t byte = 0;
			if (!read_u8(in, byte))
				return false;
			value |= static_cast<std::uint32_t>(byte) << shift;
		}
		return true;
	}

	/** @brief Reads a 64-bit unsigned integer in little-endian order.
	 * @param in Source stream.
	 * @param[out] value Receives the decoded prefix; it is zeroed before reading and may be partial on failure.
	 * @return True if eight bytes were read; false otherwise.
	 */
	inline bool read_u64(std::istream& in, std::uint64_t& value)
	{
		value = 0;
		for (std::uint8_t shift = 0; shift < 64U; shift += 8U)
		{
			std::uint8_t byte = 0;
			if (!read_u8(in, byte))
				return false;
			value |= static_cast<std::uint64_t>(byte) << shift;
		}
		return true;
	}

	/** @brief Reads a little-endian 32-bit value and converts it to int32_t.
	 * @param in Source stream.
	 * @param[out] value Receives the signed value on success and is unchanged on failure.
	 * @return True if four bytes were read; false otherwise.
	 */
	inline bool read_i32(std::istream& in, std::int32_t& value)
	{
		std::uint32_t raw = 0;
		if (!read_u32(in, raw))
			return false;
		value = static_cast<std::int32_t>(raw);
		return true;
	}

} // namespace texas::core::portable
