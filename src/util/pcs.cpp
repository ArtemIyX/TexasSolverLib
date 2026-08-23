#include "util/pcs.hpp"

#include <cassert>
#include <cmath>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace texas::util {

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

std::uint64_t PcsRng::mix_seed_word(std::uint64_t value) noexcept {
    auto z = value + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
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

double PcsRng::next_unit_f64() {
    const auto bits = next_u64() >> 11U;
    return static_cast<double>(bits) / static_cast<double>(1ULL << 53U);
}

double PcsRng::next_f64_signed() {
    return 2.0 * next_unit_f64() - 1.0;
}

bool PcsRng::bernoulli(double probability) {
    if (probability <= 0.0) {
        return false;
    }
    if (probability >= 1.0) {
        return true;
    }
    return next_unit_f64() < probability;
}

std::pair<std::size_t, double> PcsRng::sample_weighted(const double* weights, std::size_t count) {
    assert(weights != nullptr);
    assert(count > 0);

    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        assert(weights[i] >= 0.0);
        total += weights[i];
    }
    assert(total > 0.0);

    double draw = next_unit_f64() * total;
    for (std::size_t i = 0; i < count; ++i) {
        const auto weight = weights[i];
        if (weight <= 0.0) {
            continue;
        }
        if (draw < weight) {
            return {i, total / weight};
        }
        draw -= weight;
    }

    for (std::size_t i = count; i > 0; --i) {
        if (weights[i - 1] > 0.0) {
            return {i - 1, total / weights[i - 1]};
        }
    }

    return {0, 0.0};
}

std::pair<std::size_t, double> PcsRng::sample_weighted(const std::vector<double>& weights) {
    return sample_weighted(weights.data(), weights.size());
}

std::pair<std::size_t, double> sample_uniform_outcome(PcsRng& rng, std::size_t k_outcomes) {
    assert(k_outcomes > 0);
    const auto idx = static_cast<std::size_t>(rng.gen_range(static_cast<std::uint64_t>(k_outcomes)));
    return {idx, static_cast<double>(k_outcomes)};
}

}  // namespace texas::util


