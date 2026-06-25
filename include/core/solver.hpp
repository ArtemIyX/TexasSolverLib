#pragma once

#include "core/core.hpp"

namespace core {

void validate_dcfr_parameters(double alpha, double beta, double gamma);
SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma);
SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma);

}  // namespace core
