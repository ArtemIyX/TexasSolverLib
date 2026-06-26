#include "util/pcs.hpp"

#include <cassert>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace core {

namespace {

std::uint64_t mul_high_u64(std::uint64_t lhs, std::uint64_t rhs) {
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0;
    (void)_umul128(lhs, rhs, &high);
    return high;
#elif defined(__SIZEOF_INT128__)
    const auto product = static_cast<unsigned __int128>(lhs) * static_cast<unsigned __int128>(rhs);
    return static_cast<std::uint64_t>(product >> 64U);
#else
    const std::uint64_t lhs_lo = static_cast<std::uint32_t>(lhs);
    const std::uint64_t lhs_hi = lhs >> 32U;
    const std::uint64_t rhs_lo = static_cast<std::uint32_t>(rhs);
    const std::uint64_t rhs_hi = rhs >> 32U;

    const std::uint64_t lo_lo = lhs_lo * rhs_lo;
    const std::uint64_t hi_lo = lhs_hi * rhs_lo;
    const std::uint64_t lo_hi = lhs_lo * rhs_hi;
    const std::uint64_t hi_hi = lhs_hi * rhs_hi;

    const std::uint64_t carry =
        ((lo_lo >> 32U) + static_cast<std::uint32_t>(hi_lo) + static_cast<std::uint32_t>(lo_hi)) >> 32U;
    return hi_hi + (hi_lo >> 32U) + (lo_hi >> 32U) + carry;
#endif
}

}  // namespace

double effective_beta(SamplingStrategy strategy, double requested_beta) {
    switch (strategy) {
        case SamplingStrategy::Full:
            return requested_beta;
        case SamplingStrategy::PublicChance:
            return 0.5;
    }
    return requested_beta;
}

PcsRng::PcsRng(std::uint64_t seed) : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

std::uint64_t PcsRng::next_u64() {
    auto z = state_ + 0x9E3779B97F4A7C15ULL;
    state_ = z;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

std::uint64_t PcsRng::gen_range(std::uint64_t n) {
    assert(n > 0);
    return mul_high_u64(next_u64(), n);
}

double PcsRng::next_f64_signed() {
    const auto bits = next_u64() >> 11U;
    const auto unit = static_cast<double>(bits) / static_cast<double>(1ULL << 53U);
    return 2.0 * unit - 1.0;
}

std::pair<std::size_t, double> sample_uniform_outcome(PcsRng& rng, std::size_t k_outcomes) {
    assert(k_outcomes > 0);
    const auto idx = static_cast<std::size_t>(rng.gen_range(static_cast<std::uint64_t>(k_outcomes)));
    return {idx, static_cast<double>(k_outcomes)};
}

}  // namespace core


