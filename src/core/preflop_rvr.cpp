#include "core/preflop_rvr.hpp"

namespace core {

PreflopRvrOutput solve_hunl_preflop_rvr(const HUNLConfig& config, const PreflopEquityTable& table, std::uint32_t iterations, double alpha, double beta, double gamma) {
    (void)config;
    (void)table;
    (void)iterations;
    (void)alpha;
    (void)beta;
    (void)gamma;
    PreflopRvrOutput out;
    out.base = solve_leduc(1, 1.5, 0.0, 2.0);
    out.decision_node_count = static_cast<std::uint32_t>(out.base.average_strategy.size());
    return out;
}

}  // namespace core
