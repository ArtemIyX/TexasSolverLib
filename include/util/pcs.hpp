#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace texas::util {

class PcsRng {
public:
    explicit PcsRng(std::uint64_t seed);

    std::uint64_t next_u64();
    std::uint64_t gen_range(std::uint64_t n);
    double next_unit_f64();
    double next_f64_signed();
    bool bernoulli(double probability);
    std::pair<std::size_t, double> sample_weighted(const double* weights, std::size_t count);
    std::pair<std::size_t, double> sample_weighted(const std::vector<double>& weights);

    template <class... UInts>
    [[nodiscard]] static std::uint64_t mix_seed(std::uint64_t base_seed, UInts... words) noexcept {
        std::uint64_t mixed = mix_seed_word(base_seed == 0 ? 0x9E3779B97F4A7C15ULL : base_seed);
        ((mixed = mix_seed_word(mixed ^ static_cast<std::uint64_t>(words))), ...);
        return mixed;
    }

private:
    [[nodiscard]] static std::uint64_t mix_seed_word(std::uint64_t value) noexcept;

    std::uint64_t state_ = 0;
};

std::pair<std::size_t, double> sample_uniform_outcome(PcsRng& rng, std::size_t k_outcomes);

}  // namespace texas::util


