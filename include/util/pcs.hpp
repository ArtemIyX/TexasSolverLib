#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace core {

enum class SamplingStrategy : std::uint8_t {
    Full = 0,
    PublicChance = 1,
};

double effective_beta(SamplingStrategy strategy, double requested_beta);

class PcsRng {
public:
    explicit PcsRng(std::uint64_t seed);

    std::uint64_t next_u64();
    std::uint64_t gen_range(std::uint64_t n);
    double next_f64_signed();

private:
    std::uint64_t state_ = 0;
};

std::pair<std::size_t, double> sample_uniform_outcome(PcsRng& rng, std::size_t k_outcomes);

}  // namespace core


