#include "solver/solver.hpp"

#include "games/kuhn.hpp"
#include "games/leduc.hpp"

#include <stdexcept>

namespace core {

void validate_dcfr_parameters(double alpha, double beta, double gamma) {
    validate_alpha(alpha);
    if (beta < 0.0 || gamma < 0.0) {
        throw std::invalid_argument("DCFR beta and gamma must be non-negative");
    }
}

SolveOutput solve_kuhn(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers) {
    return detail::solve_generic<KuhnState>(iterations, alpha, beta, gamma, workers);
}

SolveOutput solve_leduc(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers) {
    return detail::solve_generic<LeducState>(iterations, alpha, beta, gamma, workers);
}

}  // namespace core


