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

}  // namespace texas
