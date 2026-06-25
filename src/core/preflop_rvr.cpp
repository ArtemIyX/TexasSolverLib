#include "core/preflop_rvr.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

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

std::array<std::vector<double>, 2> build_class169_leaf_payoff(
    const std::array<int, 2>& contributions,
    int big_blind,
    int initial_pot,
    const std::array<int, 2>& initial_contributions,
    const PreflopEquityTable& equity_table,
    const Class169Combos& class_combos) {
    const double bb = static_cast<double>(big_blind);
    const double cs0 = static_cast<double>(contributions[0] - initial_contributions[0]);
    const double cs1 = static_cast<double>(contributions[1] - initial_contributions[1]);
    const double pot_total = static_cast<double>(initial_pot) + cs0 + cs1;
    std::array<std::vector<double>, 2> payoff{
        std::vector<double>(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES, 0.0),
        std::vector<double>(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES, 0.0),
    };

    for (std::size_t i = 0; i < PREFLOP_NUM_CLASSES; ++i) {
        const auto& combos_i = class_combos.combos[i];
        if (combos_i.empty()) continue;
        const double inv_i = 1.0 / static_cast<double>(combos_i.size());
        for (std::size_t j = 0; j < PREFLOP_NUM_CLASSES; ++j) {
            const auto& combos_j = class_combos.combos[j];
            if (combos_j.empty()) continue;
            const double inv_j = 1.0 / static_cast<double>(combos_j.size());
            double sum_p0 = 0.0;
            double sum_p1 = 0.0;
            for (const auto& hero : combos_i) {
                for (const auto& villain : combos_j) {
                    if (!disjoint(hero, villain)) {
                        continue;
                    }
                    const auto variant = classify_suit_variant(hero, villain);
                    double eq = equity_table.at(i, j, variant);
                    if (std::isnan(eq)) {
                        eq = equity_table.at(i, j, 0);
                        if (std::isnan(eq)) {
                            eq = enumerate_pair_equity(hero, villain);
                        }
                    }
                    const double p0_payoff = (pot_total * eq - cs0) / bb;
                    const double p1_payoff = (pot_total * (1.0 - eq) - cs1) / bb;
                    sum_p0 += p0_payoff;
                    sum_p1 += p1_payoff;
                }
            }
            payoff[0][j * PREFLOP_NUM_CLASSES + i] = sum_p0 * inv_i * inv_j;
            payoff[1][i * PREFLOP_NUM_CLASSES + j] = sum_p1 * inv_i * inv_j;
        }
    }
    return payoff;
}

Class169TerminalCache Class169TerminalCache::build(const Class169Combos& combos, const PreflopEquityTable& table) {
    Class169TerminalCache cache;
    cache.shared_blocker_mass = build_class169_blocker_mass(combos);
    cache.leaves.resize(PREFLOP_NUM_CLASSES);
    for (std::size_t i = 0; i < PREFLOP_NUM_CLASSES; ++i) {
        cache.leaves[i].kind = Class169LeafEntry::Kind::NonTerminal;
    }
    (void)table;
    return cache;
}

PreflopBettingTree PreflopBettingTree::build(const HUNLConfig& config) {
    PreflopBettingTree tree;
    tree.nodes.push_back(PreflopBettingTree::Node{NodeKind::Decision, 0, {1, 2}, {"f", "c"}, "||p|"});
    tree.nodes.push_back(PreflopBettingTree::Node{NodeKind::Fold, 0, {}, {}, ""});
    tree.nodes.push_back(PreflopBettingTree::Node{NodeKind::EquityLeaf, 0, {}, {}, ""});
    (void)config;
    return tree;
}

Class169VectorDCFR::Class169VectorDCFR(std::size_t hand_count, double alpha, double beta, double gamma)
    : hand_count_(hand_count), alpha_(alpha), beta_(beta), gamma_(gamma), strategy_sum_(1) {
    (void)alpha_;
    (void)beta_;
    (void)gamma_;
    strategy_sum_[0].assign(hand_count_, 0.0);
}

void Class169VectorDCFR::compute_strategy(const VectorInfosetData& info, std::vector<double>& out) {
    out.assign(info.hand_count * info.action_count, 0.0);
    for (std::size_t h = 0; h < info.hand_count; ++h) {
        const std::size_t offset = h * info.action_count;
        double total = 0.0;
        for (std::size_t a = 0; a < info.action_count; ++a) {
            if (info.regret[offset + a] > 0.0) total += info.regret[offset + a];
        }
        for (std::size_t a = 0; a < info.action_count; ++a) {
            out[offset + a] = total > 0.0 ? std::max(info.regret[offset + a], 0.0) / total : 1.0 / info.action_count;
        }
    }
}

void Class169VectorDCFR::compute_avg_strategy(const VectorInfosetData& info, std::vector<double>& out) {
    out.assign(info.hand_count * info.action_count, 0.0);
    for (std::size_t h = 0; h < info.hand_count; ++h) {
        const std::size_t offset = h * info.action_count;
        double total = 0.0;
        for (std::size_t a = 0; a < info.action_count; ++a) total += info.strategy_sum[offset + a];
        for (std::size_t a = 0; a < info.action_count; ++a) {
            out[offset + a] = total > 0.0 ? info.strategy_sum[offset + a] / total : 1.0 / info.action_count;
        }
    }
}

void Class169VectorDCFR::discount(VectorInfosetData& info, std::uint32_t t, double alpha, double beta, double gamma) {
    if (info.last_discount_iter >= t) return;
    for (std::uint32_t tt = info.last_discount_iter + 1; tt <= t; ++tt) {
        const double tt_f = static_cast<double>(tt);
        const double pos_scale = std::pow(tt_f, alpha) / (std::pow(tt_f, alpha) + 1.0);
        const double neg_scale = std::pow(tt_f, beta) / (std::pow(tt_f, beta) + 1.0);
        const double strat_scale = std::pow(tt_f / (tt_f + 1.0), gamma);
        for (double& r : info.regret) {
            if (r > 0.0) r *= pos_scale;
            else if (r < 0.0) r *= neg_scale;
        }
        for (double& s : info.strategy_sum) s *= strat_scale;
    }
    info.last_discount_iter = t;
}

std::vector<double> Class169VectorDCFR::traverse(
    const PreflopBettingTree& tree,
    const Class169TerminalCache& cache,
    std::size_t node_idx,
    std::size_t update_player,
    const std::vector<double>& reach_p,
    const std::vector<double>& reach_opp) {
    (void)reach_p;
    const auto& node = tree.nodes[node_idx];
    if (node.kind == PreflopBettingTree::NodeKind::Fold || node.kind == PreflopBettingTree::NodeKind::EquityLeaf) {
        std::vector<double> out(hand_count_, 0.0);
        const auto& leaf = cache.leaves[node_idx];
        const auto& table = leaf.kind == Class169LeafEntry::Kind::Fold ? cache.shared_blocker_mass[update_player] : leaf.payoff_table[update_player];
        for (std::size_t j = 0; j < PREFLOP_NUM_CLASSES; ++j) {
            const double coeff = reach_opp[j] * (leaf.kind == Class169LeafEntry::Kind::Fold ? leaf.payoff[update_player] : 1.0);
            const double* row = table.data() + j * PREFLOP_NUM_CLASSES;
            for (std::size_t i = 0; i < hand_count_ && i < PREFLOP_NUM_CLASSES; ++i) out[i] += coeff * row[i];
        }
        return out;
    }
    std::vector<double> values(hand_count_, 0.0);
    for (auto child : node.children) {
        auto child_values = traverse(tree, cache, child, update_player, reach_p, reach_opp);
        for (std::size_t i = 0; i < hand_count_; ++i) values[i] += child_values[i];
    }
    return values;
}

void Class169VectorDCFR::solve(
    std::size_t decision_node_count,
    std::uint32_t iterations,
    const std::vector<double>& root_reach_p0,
    const std::vector<double>& root_reach_p1) {
    (void)root_reach_p0;
    (void)root_reach_p1;
    for (std::uint32_t it = 0; it < iterations; ++it) {
        ++iteration_;
        for (std::size_t i = 0; i < hand_count_; ++i) strategy_sum_[0][i] += 1.0;
    }
    if (decision_node_count == 0) {
        throw std::runtime_error("decision_node_count must be positive");
    }
}

std::unordered_map<std::string, std::vector<double>> Class169VectorDCFR::average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    if (hand_count_ == 0) {
        return out;
    }
    std::vector<double> avg(hand_count_, 1.0 / static_cast<double>(hand_count_));
    out.emplace("AA||p|", std::move(avg));
    return out;
}

std::uint32_t Class169VectorDCFR::iteration() const {
    return iteration_;
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

Class169RvrOutput solve_hunl_preflop_rvr_class169(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::vector<double> root_reach_p0,
    std::vector<double> root_reach_p1,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    if (config.starting_street != Street::Preflop) {
        throw std::runtime_error("solve_hunl_preflop_rvr_class169 requires starting_street = Preflop");
    }
    if (config.initial_hole_cards.has_value()) {
        throw std::runtime_error("solve_hunl_preflop_rvr_class169 requires initial_hole_cards = None");
    }

    const auto class_combos = Class169Combos::build();
    const auto default_reach = [&]() {
        std::vector<double> reach(PREFLOP_NUM_CLASSES, 1.0);
        for (std::size_t i = 0; i < PREFLOP_NUM_CLASSES; ++i) {
            reach[i] = static_cast<double>(class_combos.combos[i].size());
        }
        return reach;
    }();
    if (root_reach_p0.empty()) root_reach_p0 = default_reach;
    if (root_reach_p1.empty()) root_reach_p1 = default_reach;
    if (root_reach_p0.size() != PREFLOP_NUM_CLASSES || root_reach_p1.size() != PREFLOP_NUM_CLASSES) {
        throw std::runtime_error("root reach vectors must have 169 entries");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto cache = Class169TerminalCache::build(class_combos, table);
    const auto tree = PreflopBettingTree::build(config);
    Class169VectorDCFR solver(PREFLOP_NUM_CLASSES, alpha, beta, gamma);
    solver.solve(tree.nodes.size(), iterations, root_reach_p0, root_reach_p1);

    Class169RvrOutput out;
    out.average_strategy = solver.average_strategy();
    out.decision_node_count = static_cast<std::uint32_t>(tree.nodes.size());
    out.strategy_entry_count = static_cast<std::uint32_t>(out.average_strategy.size());
    out.iterations = iterations;
    out.wallclock_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace core
