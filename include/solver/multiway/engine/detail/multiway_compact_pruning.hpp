#pragma once

/** @file
 * Internal scalar and SIMD kernels for compact multiway regret pruning.
 */

#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway::detail
{
	enum class MultiwayCompactPruneBackend : std::uint8_t
	{
		Scalar = 0,
		Sse2 = 1,
		Avx2 = 2,
	};

	[[nodiscard]] bool multiway_compact_prune_backend_available(
		MultiwayCompactPruneBackend backend) noexcept;

	[[nodiscard]] std::size_t prune_compact_regrets_scalar(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept;
	[[nodiscard]] std::size_t prune_compact_regrets_sse2(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept;
	[[nodiscard]] std::size_t prune_compact_regrets_avx2(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept;
	[[nodiscard]] std::size_t prune_compact_regrets_selected(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept;
}
