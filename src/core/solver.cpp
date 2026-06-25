#include "core/solver.hpp"

#include "core/dcfr.hpp"
#include "core/kuhn.hpp"

#include <stdexcept>

namespace core {

void validate_dcfr_parameters(double alpha, double beta, double gamma) {
    if (alpha < 0.0 || beta < 0.0 || gamma < 0.0) {
        throw std::invalid_argument("DCFR alpha, beta, and gamma must be non-negative");
    }
}

SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma) {
    validate_dcfr_parameters(alpha, beta, gamma);
    DCFRSolver<KuhnState> solver(DCFRConfig{alpha, beta, gamma}, KuhnState::initial());
    return solver.solve(iterations);
}

SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma) {
    (void)iterations;
    validate_dcfr_parameters(alpha, beta, gamma);
    throw std::logic_error("solve_leduc is not implemented yet");
}

}  // namespace core
