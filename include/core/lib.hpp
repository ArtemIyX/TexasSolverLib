#pragma once

// Narrow compatibility facade. Internal solver, storage, artifact, and
// scheduler types must be included from their owning subsystem headers.
#include "games/hunl_solver.hpp"
#include "preflop/preflop.hpp"
#include "solver/generic/exploit.hpp"
#include "solver/hunl/sampled/hunl_sampled_solver.hpp"
#include "solver/generic/solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace texas::core::lib {

using ::texas::ExploitOutput;
using ::texas::HUNLBackendSelection;
using ::texas::HUNLConfig;
using ::texas::HUNLLeafEvaluator;
using ::texas::HUNLSampledSolveResult;
using ::texas::HUNLSampledSolveRequest;
using ::texas::HUNLSampledSolverConfig;
using ::texas::HUNLSampledSolver;
using ::texas::HUNLStructuredRootRequest;
using ::texas::HUNLSolveOutput;
using ::texas::PreflopEquityTable;
using ::texas::PreflopSolveOutput;
using ::texas::SolveOutput;

inline HUNLSampledSolveResult solve_hunl_postflop_sampled(
    const HUNLStructuredRootRequest& root,
    HUNLSampledSolverConfig config = {},
    std::uint32_t batches = 1U,
    const HUNLLeafEvaluator* leaf_evaluator = nullptr) {
    root.validate();
    HUNLSampledSolver solver(config);
    HUNLSampledSolveRequest request;
    request.structured_root = root;
    request.leaf_evaluator = leaf_evaluator;
    return solver.run_batches(request, batches);
}

inline SolveOutput solve_kuhn(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1U,
    std::size_t frontier_multiplier = 8U) {
    return ::texas::solve_kuhn(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

inline SolveOutput solve_leduc(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1U,
    std::size_t frontier_multiplier = 8U) {
    return ::texas::solve_leduc(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

inline HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1U,
    std::size_t frontier_multiplier = 8U,
    bool force_parallel = false,
    HUNLBackendSelection backend = HUNLBackendSelection::Auto) {
    return ::texas::solve_hunl_postflop(
        config, iterations, alpha, beta, gamma, workers, frontier_multiplier, force_parallel, backend);
}

inline PreflopSolveOutput solve_hunl_preflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::texas::solve_hunl_preflop(config, iterations, alpha, beta, gamma);
}

inline ExploitOutput compute_exploitability(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy) {
    return ::texas::compute_exploitability_and_value(config, strategy);
}

inline double compute_restricted_game_value(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy,
    const std::vector<std::array<std::uint8_t, 2>>& p0_holes,
    const std::vector<std::array<std::uint8_t, 2>>& p1_holes) {
    return ::texas::compute_restricted_game_value(config, strategy, p0_holes, p1_holes);
}

}  // namespace texas::core::lib
