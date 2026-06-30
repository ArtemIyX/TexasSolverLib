#pragma once

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/parallel_dcfr.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLSolveOutput {
    std::unordered_map<std::string, std::vector<double>> average_strategy;
    double exploitability = 0.0;
    double game_value = 0.0;
    std::uint32_t iterations = 0;
    double wallclock_seconds = 0.0;
    double traversal_seconds = 0.0;
    double solver_finalize_seconds = 0.0;
    double wrapper_postprocess_seconds = 0.0;
    std::uint32_t infoset_count = 0;
    bool used_parallel = false;
    SolveProfile profile;
};

enum class HUNLSolveError {
    PreflopNotSupported = 0,
    BoardLengthMismatch = 1,
    RakeNonZero = 2,
    InvalidConfig = 3,
};

HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8,
    bool force_parallel = false);

void validate_config(const HUNLConfig& config);

}  // namespace core


