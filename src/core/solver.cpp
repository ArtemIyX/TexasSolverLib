#include "core/solver.hpp"

#include "core/kuhn.hpp"
#include "core/leduc.hpp"

#include <stdexcept>

namespace core {

void validate_dcfr_parameters(double alpha, double beta, double gamma) {
    if (alpha < 0.0 || beta < 0.0 || gamma < 0.0) {
        throw std::invalid_argument("DCFR alpha, beta, and gamma must be non-negative");
    }
}

SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma) {
    return detail::solve_generic<KuhnState>(iterations, alpha, beta, gamma);
}

SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma) {
    return detail::solve_generic<LeducState>(iterations, alpha, beta, gamma);
}

}  // namespace core
