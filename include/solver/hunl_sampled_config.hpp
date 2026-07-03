#pragma once

#include "solver/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace core {

enum class HUNLFlatSamplingMode : std::uint8_t {
    Exact = 0,
    PublicChance = 1,
    External = 2,
    AverageStrategy = 3,
};

struct HUNLSampledSolverConfig {
    HUNLFlatSamplingMode mode = HUNLFlatSamplingMode::External;
    std::uint64_t seed = 1;
    std::uint32_t iterations = 0;
    std::uint32_t traversals_per_iteration = 8192;
    std::uint32_t minibatch_size = 64;
    std::uint32_t max_cached_public_states = 0;
    HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;
    bool lazy_public_expansion = true;
    bool sparse_infosets = true;
    bool deterministic_merge = true;
    bool use_public_chance_isomorphism = true;
    bool use_average_strategy_sampling = false;
    double as_epsilon = 0.05;
    double as_tau = 1000.0;
    double as_beta = 1e6;
    std::size_t workers = 1;
};

struct HUNLSampledConfigValidation {
    bool ok = true;
    const char* message = "";
};

[[nodiscard]] HUNLSampledConfigValidation validate_sampled_config(
    const HUNLSampledSolverConfig& config) noexcept;
void validate_sampled_config_or_throw(const HUNLSampledSolverConfig& config);

}  // namespace core
