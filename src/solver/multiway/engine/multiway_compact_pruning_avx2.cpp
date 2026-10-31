#include "solver/multiway/engine/detail/multiway_compact_pruning.hpp"

#if defined(__AVX2__)
	#include <immintrin.h>
#endif

namespace texas::solver::multiway::detail
{
	std::size_t prune_compact_regrets_avx2(
		std::int32_t* regrets, std::size_t count,
		std::int32_t threshold, std::int32_t replacement) noexcept
	{
		if (count == 0U)
			return 0U;
#if defined(__AVX2__)
		const auto threshold_vector = _mm256_set1_epi32(threshold);
		const auto replacement_vector = _mm256_set1_epi32(replacement);
		std::size_t pruned = 0U;
		std::size_t i = 0U;
		for (; count - i >= 8U; i += 8U)
		{
			const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(regrets + i));
			const auto mask = _mm256_cmpgt_epi32(threshold_vector, values);
			const auto updated = _mm256_blendv_epi8(values, replacement_vector, mask);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(regrets + i), updated);
			const auto bits = static_cast<unsigned>(_mm256_movemask_ps(_mm256_castsi256_ps(mask)));
			pruned += (bits & 1U) + ((bits >> 1U) & 1U) + ((bits >> 2U) & 1U) +
				((bits >> 3U) & 1U) + ((bits >> 4U) & 1U) + ((bits >> 5U) & 1U) +
				((bits >> 6U) & 1U) + ((bits >> 7U) & 1U);
		}
		return pruned + prune_compact_regrets_scalar(regrets + i, count - i, threshold, replacement);
#else
		return prune_compact_regrets_scalar(regrets, count, threshold, replacement);
#endif
	}
}
