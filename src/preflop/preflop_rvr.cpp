#include "preflop/preflop_rvr.hpp"
#include "util/suit_iso.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace core {

namespace {

bool disjoint(const std::array<std::uint8_t, 2>& a, const std::array<std::uint8_t, 2>& b) {
    return a[0] != b[0] && a[0] != b[1] && a[1] != b[0] && a[1] != b[1];
}

enum class ActionKind { Fold, Check, Call, OpenTo, RaiseTo, AllIn };

struct Action {
    ActionKind kind;
    int amount = 0;
    std::string token;
};

struct PreflopState {
    std::array<int, 2> contributions{0, 0};
    std::array<int, 2> stacks{0, 0};
    int last_bet_size = 0;
    int cur_player = -1;
    std::uint8_t street_num_raises = 0;
    int street_aggressor = -1;
    int to_call = 0;
    std::array<bool, 2> folded{false, false};
    std::array<bool, 2> all_in{false, false};
    std::string history;

    static PreflopState initial(const HUNLConfig& config) {
        PreflopState s;
        const int sb = config.small_blind + config.ante;
        const int bb = config.big_blind + config.ante;
        s.contributions = {std::max(sb, config.initial_contributions[0]), std::max(bb, config.initial_contributions[1])};
        s.stacks = {config.starting_stack - s.contributions[0], config.starting_stack - s.contributions[1]};
        s.last_bet_size = config.big_blind;
        s.cur_player = 0;
        s.street_num_raises = 1;
        s.street_aggressor = 1;
        s.to_call = s.contributions[1] - s.contributions[0];
        s.all_in = {s.stacks[0] == 0, s.stacks[1] == 0};
        return s;
    }
};

std::vector<Action> enumerate_actions(const PreflopState& state, const HUNLConfig& config) {
    if (state.cur_player < 0 || state.stacks[state.cur_player] <= 0) return {};
    const int p = state.cur_player;
    const int opp = 1 - p;
    const bool facing = state.to_call > 0;
    std::vector<Action> out;
    if (facing) {
        out.push_back({ActionKind::Fold, 0, "f"});
        out.push_back({ActionKind::Call, 0, "c"});
    } else {
        out.push_back({ActionKind::Check, 0, "x"});
    }
    const int cap = std::max<int>(1, config.preflop_raise_cap);
    const int bb = config.big_blind;
    const int cur_contrib = state.contributions[p];
    const int max_to = cur_contrib + state.stacks[p];
    const int min_raise_to = cur_contrib + std::max(state.to_call, bb);
    if (state.street_num_raises < cap) {
        if (state.street_num_raises <= 1 && state.street_aggressor == 1 && p == 0) {
            std::vector<int> seen;
            for (double size_bb : config.raise_size_xs) {
                (void)size_bb;
            }
            for (double size_bb : std::vector<double>{2.0, 3.0, 4.0, 5.0}) {
                int raise_to = static_cast<int>(std::lround(size_bb * bb));
                raise_to = std::clamp(raise_to, min_raise_to, max_to);
                if (raise_to >= max_to || std::find(seen.begin(), seen.end(), raise_to) != seen.end()) continue;
                seen.push_back(raise_to);
                out.push_back({ActionKind::OpenTo, raise_to, "b" + std::to_string(raise_to)});
            }
        } else if (facing) {
            std::vector<int> seen;
            const int prev_bet = std::max(state.last_bet_size, bb);
            for (double mult : std::vector<double>{2.0, 3.0, 4.0, 5.0}) {
                int raise_to = state.contributions[opp] + static_cast<int>(std::lround(mult * prev_bet));
                raise_to = std::clamp(raise_to, min_raise_to, max_to);
                if (raise_to >= max_to || std::find(seen.begin(), seen.end(), raise_to) != seen.end()) continue;
                seen.push_back(raise_to);
                out.push_back({ActionKind::RaiseTo, raise_to, "r" + std::to_string(raise_to)});
            }
        }
        if (state.stacks[p] > state.to_call) {
            out.push_back({ActionKind::AllIn, max_to, "A"});
        }
    }
    return out;
}

PreflopState apply_action(const PreflopState& state, const Action& action) {
    PreflopState next = state;
    const int p = state.cur_player;
    const int opp = 1 - p;
    next.history += action.token;
    switch (action.kind) {
    case ActionKind::Fold:
        next.folded[p] = true;
        next.cur_player = -1;
        break;
    case ActionKind::Check:
        next.cur_player = -1;
        break;
    case ActionKind::Call: {
        const int pay = std::min(state.to_call, state.stacks[p]);
        next.contributions[p] += pay;
        next.stacks[p] -= pay;
        next.to_call = 0;
        next.cur_player = (state.street_aggressor == 1 && state.street_num_raises == 1 && p == 0 && !next.all_in[p] && !next.all_in[opp]) ? 1 : -1;
        break;
    }
    case ActionKind::OpenTo:
    case ActionKind::RaiseTo:
    case ActionKind::AllIn: {
        const int raise_to = action.kind == ActionKind::AllIn ? state.contributions[p] + state.stacks[p] : action.amount;
        const int pay = raise_to - state.contributions[p];
        next.contributions[p] = raise_to;
        next.stacks[p] -= pay;
        next.to_call = std::max(0, raise_to - state.contributions[opp]);
        next.last_bet_size = std::max(1, raise_to - state.contributions[opp]);
        next.street_aggressor = p;
        ++next.street_num_raises;
        next.cur_player = (next.stacks[opp] == 0 || next.all_in[opp]) ? -1 : opp;
        break;
    }
    }
    return next;
}

std::string class_label(std::uint16_t idx) {
    const auto [hi, lo, suited] = class_decode(idx);
    std::string s;
    s += (hi == 14 ? "A" : std::to_string(hi));
    s += (lo == 14 ? "A" : std::to_string(lo));
    if (hi != lo) s += suited ? "s" : "o";
    return s;
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

Class169TerminalCache Class169TerminalCache::build(
    const PreflopBettingTree& tree,
    const Class169Combos& combos,
    const PreflopEquityTable& table) {
    Class169TerminalCache cache;
    cache.shared_blocker_mass = build_class169_blocker_mass(combos);
    cache.leaves.resize(tree.nodes.size());
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        const auto& node = tree.nodes[i];
        switch (node.kind) {
        case PreflopBettingTree::NodeKind::Fold:
            cache.leaves[i].kind = Class169LeafEntry::Kind::Fold;
            cache.leaves[i].payoff = {
                -static_cast<double>(node.contributions[0] - node.initial_contributions[0]) / node.big_blind,
                (static_cast<double>(node.initial_pot + node.contributions[0] + node.contributions[1] - node.initial_contributions[0] - node.initial_contributions[1]) - (node.contributions[1] - node.initial_contributions[1])) / node.big_blind,
            };
            break;
        case PreflopBettingTree::NodeKind::EquityLeaf:
            cache.leaves[i].kind = Class169LeafEntry::Kind::Equity;
            cache.leaves[i].payoff_table = build_class169_leaf_payoff(
                node.contributions,
                node.big_blind,
                node.initial_pot,
                node.initial_contributions,
                table,
                combos);
            break;
        case PreflopBettingTree::NodeKind::Decision:
            cache.leaves[i].kind = Class169LeafEntry::Kind::NonTerminal;
            break;
        }
    }
    return cache;
}

PreflopBettingTree PreflopBettingTree::build(const HUNLConfig& config) {
    PreflopBettingTree tree;
    const auto root = PreflopState::initial(config);
    std::function<std::size_t(const PreflopState&)> add = [&](const PreflopState& state) -> std::size_t {
        const std::size_t idx = tree.nodes.size();
        tree.nodes.push_back({});
        if (state.folded[0] || state.folded[1]) {
            tree.nodes[idx].kind = NodeKind::Fold;
            tree.nodes[idx].contributions = state.contributions;
            tree.nodes[idx].initial_contributions = config.initial_contributions;
            tree.nodes[idx].big_blind = config.big_blind;
            tree.nodes[idx].initial_pot = config.initial_pot;
            tree.nodes[idx].folded_player = state.folded[0] ? 0 : 1;
            return idx;
        }
        if (state.cur_player < 0) {
            tree.nodes[idx].kind = NodeKind::EquityLeaf;
            tree.nodes[idx].contributions = state.contributions;
            tree.nodes[idx].initial_contributions = config.initial_contributions;
            tree.nodes[idx].big_blind = config.big_blind;
            tree.nodes[idx].initial_pot = config.initial_pot;
            return idx;
        }
        const auto actions = enumerate_actions(state, config);
        std::vector<std::size_t> children;
        std::vector<std::string> toks;
        for (const auto& action : actions) {
            children.push_back(add(apply_action(state, action)));
            toks.push_back(action.token);
        }
        tree.nodes[idx].kind = NodeKind::Decision;
        tree.nodes[idx].player = static_cast<std::uint8_t>(state.cur_player);
        tree.nodes[idx].children = std::move(children);
        tree.nodes[idx].actions = std::move(toks);
        tree.nodes[idx].key_suffix = "||p|" + state.history;
        tree.nodes[idx].contributions = state.contributions;
        tree.nodes[idx].initial_contributions = config.initial_contributions;
        tree.nodes[idx].big_blind = config.big_blind;
        tree.nodes[idx].initial_pot = config.initial_pot;
        return idx;
    };
    add(root);
    return tree;
}

Class169VectorDCFR::Class169VectorDCFR(std::size_t hand_count, double alpha, double beta, double gamma)
    : hand_count_(hand_count), alpha_(alpha), beta_(beta), gamma_(gamma) {
    (void)alpha_;
    (void)beta_;
    (void)gamma_;
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
    const auto& node = tree.nodes[node_idx];
    if (node.kind == PreflopBettingTree::NodeKind::Fold || node.kind == PreflopBettingTree::NodeKind::EquityLeaf) {
        std::vector<double> out(hand_count_, 0.0);
        const auto& leaf = cache.leaves[node_idx];
        if (leaf.kind == Class169LeafEntry::Kind::NonTerminal) {
            return out;
        }
        const auto& table = leaf.kind == Class169LeafEntry::Kind::Fold ? cache.shared_blocker_mass[update_player] : leaf.payoff_table[update_player];
        if (table.empty()) {
            return out;
        }
        for (std::size_t j = 0; j < PREFLOP_NUM_CLASSES; ++j) {
            const double coeff = reach_opp[j] * (leaf.kind == Class169LeafEntry::Kind::Fold ? leaf.payoff[update_player] : 1.0);
            const double* row = table.data() + j * PREFLOP_NUM_CLASSES;
            for (std::size_t i = 0; i < hand_count_ && i < PREFLOP_NUM_CLASSES; ++i) out[i] += coeff * row[i];
        }
        return out;
    }
    auto& slot = infosets_.at(node_idx);
    if (!slot) {
        slot.emplace(node.children.size(), hand_count_);
    }
    auto& info = *slot;
    std::vector<double> strategy;
    compute_strategy(info, strategy);
    if (node.player != update_player) {
        std::vector<double> values(hand_count_, 0.0);
        std::vector<double> next_reach(hand_count_, 0.0);
        for (std::size_t a = 0; a < node.children.size(); ++a) {
            for (std::size_t h = 0; h < hand_count_; ++h) {
                next_reach[h] = reach_opp[h] * strategy[h * node.children.size() + a];
            }
            auto child_values = traverse(tree, cache, node.children[a], update_player, reach_p, next_reach);
            for (std::size_t h = 0; h < hand_count_; ++h) {
                values[h] += child_values[h];
            }
        }
        return values;
    }

    discount(info, iteration_, alpha_, beta_, gamma_);
    compute_strategy(info, strategy);
    std::vector<double> action_values(node.children.size() * hand_count_, 0.0);
    std::vector<double> next_reach(hand_count_, 0.0);
    for (std::size_t a = 0; a < node.children.size(); ++a) {
        for (std::size_t h = 0; h < hand_count_; ++h) {
            next_reach[h] = reach_p[h] * strategy[h * node.children.size() + a];
        }
        auto child_values = traverse(tree, cache, node.children[a], update_player, next_reach, reach_opp);
        std::copy(child_values.begin(), child_values.end(), action_values.begin() + a * hand_count_);
    }

    std::vector<double> node_values(hand_count_, 0.0);
    for (std::size_t h = 0; h < hand_count_; ++h) {
        double v = 0.0;
        for (std::size_t a = 0; a < node.children.size(); ++a) {
            v += strategy[h * node.children.size() + a] * action_values[a * hand_count_ + h];
        }
        node_values[h] = v;
    }

    for (std::size_t h = 0; h < hand_count_; ++h) {
        const double opp_reach = reach_opp[h];
        const double own_reach = reach_p[h];
        for (std::size_t a = 0; a < node.children.size(); ++a) {
            info.regret[h * node.children.size() + a] += opp_reach * (action_values[a * hand_count_ + h] - node_values[h]);
            info.strategy_sum[h * node.children.size() + a] += own_reach * strategy[h * node.children.size() + a];
        }
    }
    return node_values;
}

void Class169VectorDCFR::solve(
    const PreflopBettingTree& tree,
    const Class169TerminalCache& cache,
    std::uint32_t iterations,
    const std::vector<double>& root_reach_p0,
    const std::vector<double>& root_reach_p1) {
    infosets_.assign(tree.nodes.size(), std::nullopt);
    for (std::size_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        if (tree.nodes[node_idx].kind == PreflopBettingTree::NodeKind::Decision) {
            infosets_[node_idx].emplace(tree.nodes[node_idx].children.size(), hand_count_);
        }
    }
    for (std::uint32_t it = 0; it < iterations; ++it) {
        ++iteration_;
        (void)traverse(tree, cache, 0, 0, root_reach_p0, root_reach_p1);
        (void)traverse(tree, cache, 0, 1, root_reach_p1, root_reach_p0);
    }
}

void Class169VectorDCFR::solve(
    std::size_t decision_node_count,
    std::uint32_t iterations,
    const std::vector<double>& root_reach_p0,
    const std::vector<double>& root_reach_p1) {
    (void)root_reach_p0;
    (void)root_reach_p1;
    infosets_.assign(decision_node_count, std::nullopt);
    for (std::uint32_t it = 0; it < iterations; ++it) ++iteration_;
    if (decision_node_count == 0) {
        throw std::runtime_error("decision_node_count must be positive");
    }
}

std::unordered_map<std::string, std::vector<double>> Class169VectorDCFR::average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    if (hand_count_ == 0) return out;
    for (const auto& slot : infosets_) {
        if (!slot) continue;
        std::vector<double> avg;
        compute_avg_strategy(*slot, avg);
        out.emplace("AA||p|", std::move(avg));
        break;
    }
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
    const auto tree = PreflopBettingTree::build(config);
    const auto cache = Class169TerminalCache::build(tree, class_combos, table);
    Class169VectorDCFR solver(PREFLOP_NUM_CLASSES, alpha, beta, gamma);
    solver.solve(tree, cache, iterations, root_reach_p0, root_reach_p1);

    Class169RvrOutput out;
    out.average_strategy = solver.average_strategy();
    out.decision_node_count = static_cast<std::uint32_t>(tree.nodes.size());
    out.strategy_entry_count = static_cast<std::uint32_t>(out.average_strategy.size());
    out.iterations = iterations;
    out.hand_count_per_player = {PREFLOP_NUM_CLASSES, PREFLOP_NUM_CLASSES};
    out.wallclock_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

VectorSolveOutput solve_hunl_vector_dcfr(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    auto shared = std::make_shared<const HUNLConfig>(config);
    const auto initial = HUNLState::initial(shared);
    const auto tree = BettingTree::build_from(initial);
    const auto hole_pairs = enumerate_hole_card_pairs(initial);
    if (hole_pairs.empty()) {
        return {};
    }
    const auto root_reach = std::vector<double>(hole_pairs.size(), 1.0);
    const std::array<const std::vector<double>*, 2> reach = {&root_reach, &root_reach};
    const std::array<std::vector<std::array<std::uint8_t, 2>>, 2> holes = [&]() {
        std::array<std::vector<std::array<std::uint8_t, 2>>, 2> out;
        out[0].reserve(hole_pairs.size());
        out[1].reserve(hole_pairs.size());
        for (const auto& hp : hole_pairs) {
            out[0].push_back(hp[0]);
            out[1].push_back(hp[1]);
        }
        return out;
    }();

    const auto cache = build_suit_iso_cache(tree.nodes, tree.dealt_cards, initial.board, holes, reach);
    if (cache.is_active()) {
        const auto skip_mask = member_skip_mask(tree.nodes, cache);
        return solve_vector_dcfr(tree, hole_pairs, iterations, alpha, beta, gamma, skip_mask);
    }
    return solve_vector_dcfr(tree, hole_pairs, iterations, alpha, beta, gamma);
}

}  // namespace core


