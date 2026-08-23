#pragma once

#include "core/namespaces.hpp"

// Transitional source-compatibility import. New subsystem headers must use
// owning namespaces and include their domain headers directly.
namespace texas {
using namespace core;
using namespace games;
using namespace games::hunl;
using namespace games::multiway;
using namespace ranges;
using namespace preflop;
using namespace solver;
using namespace solver::dcfr;
using namespace solver::hunl;
using namespace solver::multiway;
using namespace util;
}  // namespace texas
