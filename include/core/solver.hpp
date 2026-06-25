#pragma once

#include "core/core.hpp"

namespace core {

SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma);
SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma);

}  // namespace core

