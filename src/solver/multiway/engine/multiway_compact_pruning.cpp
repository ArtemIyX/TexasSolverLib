#include "solver/multiway/engine/detail/multiway_compact_pruning.hpp"

#include <cstddef>
#include <cstdint>

#if defined(TEXASSOLVER_COMPACT_PRUNE_AVX2_COMPILED) && defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	#include <intrin.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	#include <emmintrin.h>
#endif

namespace texas::solver::multiway::detail
{
	namespace
	{
#if defined(TEXASSOLVER_COMPACT_PRUNE_AVX2_COMPILED)
		bool runtime_has_avx2() noexcept
		{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
			int cpu_info[4] = {};
			__cpuidex(cpu_info, 1, 0);
			const bool osxsave = (cpu_info[2] & (1 << 27)) != 0;
			const bool avx = (cpu_info[2] & (1 << 28)) != 0;
			if (!osxsave || !avx)
				return false;
			const auto xcr0 = _xgetbv(0);
			if ((xcr0 & 0x6) != 0x6)
				return false;
			__cpuidex(cpu_info, 0, 0);
			if (cpu_info[0] < 7)
				return false;
			__cpuidex(cpu_info, 7, 0);
			return (cpu_info[1] & (1 << 5)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
			return __builtin_cpu_supports("avx2") != 0;
#else
			return false;
#endif
		}
#endif
	}

	std::size_t prune_compact_regrets_scalar(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept
	{
		std::size_t pruned = 0U;
		for (std::size_t i = 0U; i < count; ++i)
		{
			if (regrets[i] < threshold)
			{
				regrets[i] = replacement;
				++pruned;
			}
		}
		return pruned;
	}

	std::size_t prune_compact_regrets_sse2(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept
	{
		if (count == 0U)
			return 0U;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
		const auto threshold_vector = _mm_set1_epi32(threshold);
		const auto replacement_vector = _mm_set1_epi32(replacement);
		std::size_t pruned = 0U;
		std::size_t i = 0U;
		for (; count - i >= 4U; i += 4U)
		{
			const auto values = _mm_loadu_si128(reinterpret_cast<const __m128i*>(regrets + i));
			const auto mask = _mm_cmpgt_epi32(threshold_vector, values);
			const auto updated = _mm_or_si128(_mm_and_si128(mask, replacement_vector),
				_mm_andnot_si128(mask, values));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(regrets + i), updated);
			const auto bits = static_cast<unsigned>(_mm_movemask_ps(_mm_castsi128_ps(mask)));
			pruned += (bits & 1U) + ((bits >> 1U) & 1U) + ((bits >> 2U) & 1U) + ((bits >> 3U) & 1U);
		}
		return pruned + prune_compact_regrets_scalar(regrets + i, count - i, threshold, replacement);
#else
		return prune_compact_regrets_scalar(regrets, count, threshold, replacement);
#endif
	}

	bool multiway_compact_prune_backend_available(MultiwayCompactPruneBackend backend) noexcept
	{
		switch (backend)
		{
			case MultiwayCompactPruneBackend::Scalar:
				return true;
			case MultiwayCompactPruneBackend::Sse2:
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
				return true;
#else
				return false;
#endif
			case MultiwayCompactPruneBackend::Avx2:
#if defined(TEXASSOLVER_COMPACT_PRUNE_AVX2_COMPILED)
				{
					static const bool available = runtime_has_avx2();
					return available;
				}
#else
				return false;
#endif
		}
		return false;
	}

	std::size_t prune_compact_regrets_selected(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept
	{
		if (multiway_compact_prune_backend_available(MultiwayCompactPruneBackend::Avx2))
			return prune_compact_regrets_avx2(regrets, count, threshold, replacement);
		if (multiway_compact_prune_backend_available(MultiwayCompactPruneBackend::Sse2))
			return prune_compact_regrets_sse2(regrets, count, threshold, replacement);
		return prune_compact_regrets_scalar(regrets, count, threshold, replacement);
	}
}
