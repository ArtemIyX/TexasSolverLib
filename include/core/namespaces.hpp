#pragma once

// Declares the public namespace hierarchy before individual subsystem headers
// contribute their declarations. Root using-directives support cross-subsystem
// references during the breaking migration from core:: to texas::.
namespace texas {
namespace core {}
namespace games {
namespace hunl {}
namespace multiway {}
}
namespace ranges {}
namespace preflop {}
namespace solver {
namespace dcfr {}
namespace hunl {}
namespace multiway {}
}
namespace util {
namespace detail {}
namespace profiling {}
}

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