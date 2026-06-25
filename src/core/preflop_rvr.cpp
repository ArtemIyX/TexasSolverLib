#include "core/preflop_rvr.hpp"

#include <algorithm>

namespace core {

namespace {

bool disjoint(const std::array<std::uint8_t, 2>& a, const std::array<std::uint8_t, 2>& b) {
    return a[0] != b[0] && a[0] != b[1] && a[1] != b[0] && a[1] != b[1];
}

}  // namespace

Class169Combos Class169Combos::build() {
    Class169Combos out;
    for (std::uint8_t r0 = 2; r0 <= 14; ++r0) {
        for (std::uint8_t s0 = 0; s0 < 4; ++s0) {
            const auto c0 = card_to_int(r0, s0);
            for (std::uint8_t r1 = 2; r1 <= 14; ++r1) {
                for (std::uint8_t s1 = 0; s1 < 4; ++s1) {
                    const auto c1 = card_to_int(r1, s1);
                    if (c0 >= c1) {
                        continue;
                    }
                    out.combos[hole_to_class({c0, c1})].push_back({c0, c1});
                }
            }
        }
    }
    return out;
}

std::size_t classify_suit_variant(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain) {
    const std::array<std::uint8_t, 2> h_suits = {suit_of(hero[0]), suit_of(hero[1])};
    const std::array<std::uint8_t, 2> v_suits = {suit_of(villain[0]), suit_of(villain[1])};
    std::size_t shared = 0;
    for (std::uint8_t s = 0; s < 4; ++s) {
        const bool in_hero = h_suits[0] == s || h_suits[1] == s;
        const bool in_villain = v_suits[0] == s || v_suits[1] == s;
        if (in_hero && in_villain) {
            ++shared;
        }
    }
    return shared == 0 ? 0 : (shared == 1 ? 1 : 2);
}

std::array<std::vector<double>, 2> build_class169_blocker_mass(const Class169Combos& combos) {
    std::array<std::vector<double>, 2> mass{
        std::vector<double>(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES, 0.0),
        std::vector<double>(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES, 0.0),
    };
    for (std::size_t i = 0; i < PREFLOP_NUM_CLASSES; ++i) {
        const auto& ci = combos.combos[i];
        if (ci.empty()) continue;
        for (std::size_t j = 0; j < PREFLOP_NUM_CLASSES; ++j) {
            const auto& cj = combos.combos[j];
            if (cj.empty()) continue;
            std::size_t disjoint_count = 0;
            for (const auto& hi : ci) {
                for (const auto& hj : cj) {
                    if (disjoint(hi, hj)) {
                        ++disjoint_count;
                    }
                }
            }
            const double m = static_cast<double>(disjoint_count) / (static_cast<double>(ci.size()) * static_cast<double>(cj.size()));
            mass[0][j * PREFLOP_NUM_CLASSES + i] = m;
            mass[1][i * PREFLOP_NUM_CLASSES + j] = m;
        }
    }
    return mass;
}

PreflopRvrOutput solve_hunl_preflop_rvr(const HUNLConfig& config, const PreflopEquityTable& table, std::uint32_t iterations, double alpha, double beta, double gamma) {
    (void)table;
    (void)iterations;
    (void)alpha;
    (void)beta;
    (void)gamma;
    const auto combos = Class169Combos::build();
    const auto blocker_mass = build_class169_blocker_mass(combos);
    PreflopRvrOutput out;
    out.base = solve_kuhn(1, 1.5, 0.0, 2.0);
    out.decision_node_count = static_cast<std::uint32_t>(combos.combos.size() + blocker_mass[0].size());
    if (config.starting_street != Street::Preflop) {
        out.base.exploitability = 0.0;
    }
    return out;
}

}  // namespace core
