#pragma once

/** @file
 * Seeded pseudorandom sampling helpers used by poker chance traversal.
 */

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace texas::util
{

	/** Seeded generator for reproducible scalar and weighted sampling. */
	class PcsRng
	{
	public:
		/** Initializes the generator from a deterministic seed. */
		explicit PcsRng(std::uint64_t seed);

		/** Returns the next 64-bit pseudorandom word. */
		std::uint64_t next_u64();
		/** Returns an integer uniformly selected from [0, n). */
		std::uint64_t gen_range(std::uint64_t n);
		/** Returns a unit floating-point sample in [0, 1). */
		double next_unit_f64();
		/** Returns a signed floating-point sample in [-1, 1). */
		double next_f64_signed();
		/** Returns a Bernoulli outcome for a probability in [0, 1]. */
		bool bernoulli(double probability);
		/** Samples nonnegative weights and returns the index and selected mass. */
		std::pair<std::size_t, double> sample_weighted(const double* weights, std::size_t count);
		/** Samples an owning vector of nonnegative weights. */
		std::pair<std::size_t, double> sample_weighted(const std::vector<double>& weights);

		template <class... UInts>
		/** Combines integer identity words into a reproducible seed. */
		[[nodiscard]] static std::uint64_t mix_seed(std::uint64_t base_seed, UInts... words) noexcept
		{
			std::uint64_t mixed = mix_seed_word(base_seed == 0 ? 0x9E3779B97F4A7C15ULL : base_seed);
			((mixed = mix_seed_word(mixed ^ static_cast<std::uint64_t>(words))), ...);
			return mixed;
		}

	private:
		[[nodiscard]] static std::uint64_t mix_seed_word(std::uint64_t value) noexcept;

		std::uint64_t state_ = 0;
	};

	/** Samples one of k_outcomes uniformly and returns index and probability. */
	std::pair<std::size_t, double> sample_uniform_outcome(PcsRng& rng, std::size_t k_outcomes);

} // namespace texas::util
