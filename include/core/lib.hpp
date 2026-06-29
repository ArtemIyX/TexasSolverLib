#pragma once

#include "util/abstraction.hpp"
#include "util/api.hpp"
#include "solver/dcfr.hpp"
#include "solver/dcfr_vector.hpp"
#include "solver/dcfr_vector_parallel.hpp"
#include "solver/exploit.hpp"
#include "games/hunl.hpp"
#include "games/hunl_eval.hpp"
#include "games/hunl_solver.hpp"
#include "games/hunl_tree.hpp"
#include "games/kuhn.hpp"
#include "util/layout.hpp"
#include "games/leduc.hpp"
#include "util/pcs.hpp"
#include "preflop/preflop.hpp"
#include "preflop/preflop_equity.hpp"
#include "preflop/preflop_rvr.hpp"
#include "util/simd.hpp"
#include "solver/solver.hpp"
#include "util/suit_iso.hpp"

#include <memory>
#include <unordered_map>

namespace core::lib {

/**
 * @brief Stable convenience aliases for external consumers.
 */
using ::core::ActionId;
using ::core::ChanceOutcome;
using ::core::Class169RvrOutput;
using ::core::ExploitOutput;
using ::core::HUNLConfig;
using ::core::HUNLSolveOutput;
using ::core::InfosetKey;
using ::core::PlayerId;
using ::core::PreflopRvrOutput;
using ::core::PreflopSolveOutput;
using ::core::Probability;
using ::core::SolveOutput;
using ::core::Value;
using ::core::VectorSolveOutput;

/**
 * @brief Solve Kuhn poker through the library facade.
 */
inline SolveOutput solve_kuhn(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8) {
    return ::core::solve_kuhn(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

/**
 * @brief Solve Leduc poker through the library facade.
 */
inline SolveOutput solve_leduc(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8) {
    return ::core::solve_leduc(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

inline HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8,
    bool force_parallel = false) {
    return ::core::solve_hunl_postflop(
        config, iterations, alpha, beta, gamma, workers, frontier_multiplier, force_parallel);
}

inline PreflopSolveOutput solve_hunl_preflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop(config, iterations, alpha, beta, gamma);
}

inline ExploitOutput compute_exploitability(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy) {
    return ::core::compute_exploitability_and_value(config, strategy);
}

inline double compute_restricted_game_value(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy,
    const std::vector<std::array<std::uint8_t, 2>>& p0_holes,
    const std::vector<std::array<std::uint8_t, 2>>& p1_holes) {
    return ::core::compute_restricted_game_value(config, strategy, p0_holes, p1_holes);
}

inline VectorSolveOutput solve_range_vs_range_rust(
    const HUNLConfig& config,
    const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_vector_dcfr(
        ::core::BettingTree::build_from(::core::HUNLState::initial(std::make_shared<const HUNLConfig>(config))),
        hole_pairs,
        iterations,
        alpha,
        beta,
        gamma);
}

inline PreflopRvrOutput solve_hunl_preflop_rvr(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop_rvr(config, table, iterations, alpha, beta, gamma);
}

inline Class169RvrOutput solve_hunl_preflop_rvr_class169(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::vector<double> root_reach_p0,
    std::vector<double> root_reach_p1,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop_rvr_class169(
        config,
        table,
        std::move(root_reach_p0),
        std::move(root_reach_p1),
        iterations,
        alpha,
        beta,
        gamma);
}

}  // namespace core::lib


