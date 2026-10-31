#pragma once

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace texas::solver::multiway::bucket_artifact_detail
{
	using core::Street;

	inline constexpr std::uint8_t kMagic[] = { 'M', 'W', 'B', 'K' };
	inline constexpr std::size_t kIdentityFieldCount = 13U;

	inline std::uint32_t bucket_assignment_width(std::uint32_t bucket_count) noexcept
	{
		return bucket_count <= 0x100U ? 1U : bucket_count <= 0x10000U ? 2U
																	  : 4U;
	}

	inline std::size_t board_size_for(Street street) noexcept
	{
		switch (street)
		{
			case Street::Flop:
				return 3U;
			case Street::Turn:
				return 4U;
			case Street::River:
				return 5U;
			default:
				return 0U;
		}
	}

	inline void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value)
	{
		for (std::uint8_t index = 0U; index < 4U; ++index)
			output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
	}

	inline void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value)
	{
		for (std::uint8_t index = 0U; index < 8U; ++index)
			output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
	}

	inline std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t& cursor)
	{
		if (input.size() - cursor < 4U)
			throw std::invalid_argument("multiway bucket artifact is truncated");
		std::uint32_t value = 0U;
		for (std::uint8_t index = 0U; index < 4U; ++index)
			value |= static_cast<std::uint32_t>(input[cursor++]) << (index * 8U);
		return value;
	}

	inline std::uint64_t read_u64(const std::vector<std::uint8_t>& input, std::size_t& cursor)
	{
		if (input.size() - cursor < 8U)
			throw std::invalid_argument("multiway bucket artifact is truncated");
		std::uint64_t value = 0U;
		for (std::uint8_t index = 0U; index < 8U; ++index)
			value |= static_cast<std::uint64_t>(input[cursor++]) << (index * 8U);
		return value;
	}

	inline void append_identity(std::vector<std::uint8_t>& output, const MultiwayModelIdentity& identity)
	{
		visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) { append_u64(output, field); });
	}

	inline MultiwayModelIdentity read_identity(const std::vector<std::uint8_t>& input, std::size_t& cursor)
	{
		MultiwayModelIdentity identity;
		visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) { field = read_u64(input, cursor); });
		identity.validate();
		return identity;
	}
} // namespace texas::solver::multiway::bucket_artifact_detail
