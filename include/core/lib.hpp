#pragma once

#include "core/abstraction.hpp"
#include "core/api.hpp"
#include "core/dcfr.hpp"
#include "core/dcfr_vector.hpp"
#include "core/dcfr_vector_parallel.hpp"
#include "core/exploit.hpp"
#include "core/hunl.hpp"
#include "core/hunl_eval.hpp"
#include "core/hunl_solver.hpp"
#include "core/hunl_tree.hpp"
#include "core/kuhn.hpp"
#include "core/layout.hpp"
#include "core/leduc.hpp"
#include "core/pcs.hpp"
#include "core/preflop.hpp"
#include "core/preflop_equity.hpp"
#include "core/preflop_rvr.hpp"
#include "core/simd.hpp"
#include "core/solver.hpp"
#include "core/suit_iso.hpp"

#include <memory>
#include <unordered_map>

namespace core::lib {

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

inline SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma) {
    return ::core::solve_kuhn(iterations, alpha, beta, gamma);
}

inline SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma) {
    return ::core::solve_leduc(iterations, alpha, beta, gamma);
}

inline HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_postflop(config, iterations, alpha, beta, gamma);
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
