#include "solver/solver.hpp"
#include "util/profiling.hpp"

#include "games/kuhn.hpp"
#include "games/leduc.hpp"

#include <stdexcept>

namespace core {

void validate_dcfr_parameters(double alpha, double beta, double gamma) {
    TEXASSOLVER_PROFILE_SCOPE("solver.validate_dcfr_parameters");
    validate_dcfr_config_values(alpha, beta, gamma);
}

SolveOutput solve_kuhn(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers,
    std::size_t frontier_multiplier) {
    TEXASSOLVER_PROFILE_SCOPE("solver.solve_kuhn");
    return detail::solve_generic<KuhnState>(
        iterations, alpha, beta, gamma, workers, {}, frontier_multiplier);
}

SolveOutput solve_leduc(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers,
    std::size_t frontier_multiplier) {
    TEXASSOLVER_PROFILE_SCOPE("solver.solve_leduc");
    return detail::solve_generic<LeducState>(
        iterations, alpha, beta, gamma, workers, {}, frontier_multiplier);
}

}  // namespace core


