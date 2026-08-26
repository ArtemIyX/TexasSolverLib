#include "preflop/preflop_rvr.hpp"
#include "util/iteration_range.hpp"
#include "util/suit_iso.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace texas::preflop {

namespace {

bool disjoint(const std::array<std::uint8_t, 2>& a, const std::array<std::uint8_t, 2>& b) {
    return a[0] != b[0] && a[0] != b[1] && a[1] != b[0] && a[1] != b[1];
}

bool is_aggressive_preflop_action(ActionId action) {
    return action == ACTION_ALL_IN || is_opening_bet(action) || is_raise(action);
}

std::vector<ActionId> actionable_preflop_actions(const HUNLState& state) {
    auto actions = state.legal_actions();
    if (state.cur_player < 0 || state.cur_player > 1) {
        return {};
    }
    const auto opponent = static_cast<std::size_t>(1 - state.cur_player);
    if (!state.all_in[opponent] && state.stacks[opponent] > 0) {
        return actions;
    }
    actions.erase(
        std::remove_if(
            actions.begin(),
            actions.end(),
            [](ActionId action) {
                return is_aggressive_preflop_action(action);
            }),
        actions.end());
    return actions;
}

std::string applied_action_token(const HUNLState& next) {
    if (!next.current_street_tokens.empty()) {
        return next.current_street_tokens.back();
    }
    if (!next.betting_tokens.empty() && !next.betting_tokens.back().empty()) {
        return next.betting_tokens.back().back();
    }
    throw std::logic_error("preflop action did not produce a history token");
}

std::string class_label(std::uint16_t idx) {
    const auto [hi, lo, suited] = class_decode(idx);
    const auto rank_token = [](std::uint8_t rank) -> char {
        if (rank >= 2 && rank <= 9) {
            return static_cast<char>('0' + rank);
        }
        switch (rank) {
        case 10:
            return 'T';
        case 11:
            return 'J';
        case 12:
            return 'Q';
        case 13:
            return 'K';
        case 14:
            return 'A';
        default:
            throw std::logic_error("class rank is out of bounds");
        }
    };
    std::string s;
    s += rank_token(hi);
    s += rank_token(lo);
    if (hi != lo) s += suited ? "s" : "o";
    return s;
}

constexpr std::size_t class169_pair_count() {
    return PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES;
}

void validate_class169_reach(
    const std::vector<double>& reach,
    const char* name) {
    if (reach.size() != PREFLOP_NUM_CLASSES) {
        throw std::invalid_argument(
            std::string(name) + " must contain exactly 169 entries");
    }
    double total = 0.0;
    for (const auto value : reach) {
        if (!std::isfinite(value) || value < 0.0) {
            throw std::invalid_argument(
                std::string(name) +
                " must contain finite non-negative values");
        }
        total += value;
        if (!std::isfinite(total)) {
            throw std::invalid_argument(
                std::string(name) + " total must be finite");
        }
    }
    if (total <= 0.0) {
        throw std::invalid_argument(
            std::string(name) + " must contain positive mass");
    }
}

void validate_finite_table(
    const std::vector<double>& values,
    std::size_t expected_size,
    const char* name,
    bool require_non_negative) {
    if (values.size() != expected_size) {
        throw std::invalid_argument(
            std::string(name) + " has an invalid shape");
    }
    for (const auto value : values) {
        if (!std::isfinite(value) ||
            (require_non_negative && value < 0.0)) {
            throw std::invalid_argument(
                std::string(name) + " contains an invalid value");
        }
    }
}

void validate_class169_tree_and_cache(
    const PreflopBettingTree& tree,
    const Class169TerminalCache& cache) {
    if (tree.nodes.empty()) {
        throw std::invalid_argument(
            "Class169VectorDCFR requires a non-empty tree");
    }
    if (cache.leaves.size() != tree.nodes.size()) {
        throw std::invalid_argument(
            "Class169VectorDCFR cache must be node-aligned");
    }

    std::vector<std::uint8_t> color(tree.nodes.size(), 0U);
    std::vector<std::size_t> parent_count(tree.nodes.size(), 0U);
    std::unordered_set<std::string> decision_keys;
    bool has_fold = false;
    const auto visit = [&](const auto& self, std::size_t node_index) -> void {
        if (node_index >= tree.nodes.size()) {
            throw std::invalid_argument(
                "Class169VectorDCFR child index is out of bounds");
        }
        if (color[node_index] == 1U) {
            throw std::invalid_argument(
                "Class169VectorDCFR tree contains a cycle");
        }
        if (color[node_index] == 2U) {
            return;
        }
        color[node_index] = 1U;
        const auto& node = tree.nodes[node_index];
        const auto& leaf = cache.leaves[node_index];
        if (node.big_blind <= 0 ||
            node.contributions[0] < 0 ||
            node.contributions[1] < 0 ||
            node.initial_contributions[0] < 0 ||
            node.initial_contributions[1] < 0) {
            throw std::invalid_argument(
                "Class169VectorDCFR tree contains invalid chip metadata");
        }

        switch (node.kind) {
        case PreflopBettingTree::NodeKind::Decision:
            if (leaf.kind != Class169LeafEntry::Kind::NonTerminal ||
                node.player > 1U ||
                node.actions.empty() ||
                node.actions.size() != node.children.size() ||
                node.key_suffix.empty() ||
                !decision_keys.insert(node.key_suffix).second) {
                throw std::invalid_argument(
                    "Class169VectorDCFR decision node contract is invalid");
            }
            for (const auto child : node.children) {
                if (child >= tree.nodes.size()) {
                    throw std::invalid_argument(
                        "Class169VectorDCFR child index is out of bounds");
                }
                ++parent_count[child];
                if (parent_count[child] > 1U) {
                    throw std::invalid_argument(
                        "Class169VectorDCFR input must be a tree");
                }
                self(self, child);
            }
            break;
        case PreflopBettingTree::NodeKind::Fold:
            if (leaf.kind != Class169LeafEntry::Kind::Fold ||
                !node.actions.empty() ||
                !node.children.empty() ||
                node.folded_player > 1U ||
                !std::isfinite(leaf.payoff[0]) ||
                !std::isfinite(leaf.payoff[1])) {
                throw std::invalid_argument(
                    "Class169VectorDCFR fold leaf contract is invalid");
            }
            has_fold = true;
            break;
        case PreflopBettingTree::NodeKind::EquityLeaf:
            if (leaf.kind != Class169LeafEntry::Kind::Equity ||
                !node.actions.empty() ||
                !node.children.empty()) {
                throw std::invalid_argument(
                    "Class169VectorDCFR equity leaf contract is invalid");
            }
            validate_finite_table(
                leaf.payoff_table[0],
                class169_pair_count(),
                "class-169 player-0 payoff table",
                false);
            validate_finite_table(
                leaf.payoff_table[1],
                class169_pair_count(),
                "class-169 player-1 payoff table",
                false);
            break;
        }
        color[node_index] = 2U;
    };
    visit(visit, 0U);

    if (std::any_of(
            color.begin(),
            color.end(),
            [](std::uint8_t value) { return value != 2U; })) {
        throw std::invalid_argument(
            "Class169VectorDCFR tree contains unreachable nodes");
    }
    if (has_fold) {
        validate_finite_table(
            cache.shared_blocker_mass[0],
            class169_pair_count(),
            "class-169 player-0 blocker table",
            true);
        validate_finite_table(
            cache.shared_blocker_mass[1],
            class169_pair_count(),
            "class-169 player-1 blocker table",
            true);
    }
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
    config.validate();
    if (config.starting_street != Street::Preflop) {
        throw std::invalid_argument(
            "PreflopBettingTree requires starting_street = Preflop");
    }
    if (config.initial_hole_cards.has_value()) {
        throw std::invalid_argument(
            "PreflopBettingTree requires unresolved private cards");
    }

    auto initial = HUNLState::initial(
        std::make_shared<const HUNLConfig>(config));
    initial = initial.clone_with_hole_cards({{
        {card_to_int(14, 0), card_to_int(14, 1)},
        {card_to_int(13, 2), card_to_int(13, 3)},
    }});

    PreflopBettingTree tree;
    std::function<std::size_t(const HUNLState&)> add =
        [&](const HUNLState& state) -> std::size_t {
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
        if (state.street != Street::Preflop ||
            state.cur_player < 0 ||
            state.cur_player > 1 ||
            state.stacks[static_cast<std::size_t>(state.cur_player)] <= 0) {
            tree.nodes[idx].kind = NodeKind::EquityLeaf;
            tree.nodes[idx].contributions = state.contributions;
            tree.nodes[idx].initial_contributions = config.initial_contributions;
            tree.nodes[idx].big_blind = config.big_blind;
            tree.nodes[idx].initial_pot = config.initial_pot;
            return idx;
        }
        const auto actions = actionable_preflop_actions(state);
        if (actions.empty()) {
            throw std::logic_error(
                "PreflopBettingTree encountered an empty decision node");
        }
        std::vector<std::size_t> children;
        std::vector<std::string> toks;
        for (const auto& action : actions) {
            const auto next = state.apply(action);
            toks.push_back(applied_action_token(next));
            children.push_back(add(next));
        }
        tree.nodes[idx].kind = NodeKind::Decision;
        tree.nodes[idx].player = static_cast<std::uint8_t>(state.cur_player);
        tree.nodes[idx].children = std::move(children);
        tree.nodes[idx].actions = std::move(toks);
        tree.nodes[idx].key_suffix = "||p|" + state.format_history();
        tree.nodes[idx].contributions = state.contributions;
        tree.nodes[idx].initial_contributions = config.initial_contributions;
        tree.nodes[idx].big_blind = config.big_blind;
        tree.nodes[idx].initial_pot = config.initial_pot;
        return idx;
    };
    add(initial);
    return tree;
}

Class169VectorDCFR::Class169VectorDCFR(std::size_t hand_count, double alpha, double beta, double gamma)
    : hand_count_(hand_count), alpha_(alpha), beta_(beta), gamma_(gamma) {
    if (hand_count_ != PREFLOP_NUM_CLASSES) {
        throw std::invalid_argument(
            "Class169VectorDCFR requires exactly 169 hand classes");
    }
    validate_dcfr_config_values(alpha_, beta_, gamma_);
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
    for_each_u32_after(info.last_discount_iter, t, [&](std::uint32_t tt) {
        const auto scales = dcfr_iteration_scales(tt, alpha, beta, gamma);
        for (double& r : info.regret) {
            if (r > 0.0) r *= scales.positive_regret;
            else if (r < 0.0) r *= scales.negative_regret;
        }
        for (double& s : info.strategy_sum) s *= scales.strategy_sum;
    });
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
    validate_class169_tree_and_cache(tree, cache);
    validate_class169_reach(root_reach_p0, "root_reach_p0");
    validate_class169_reach(root_reach_p1, "root_reach_p1");

    iteration_ = 0;
    infosets_.assign(tree.nodes.size(), std::nullopt);
    infoset_key_suffixes_.assign(tree.nodes.size(), {});
    for (std::size_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        if (tree.nodes[node_idx].kind == PreflopBettingTree::NodeKind::Decision) {
            infosets_[node_idx].emplace(tree.nodes[node_idx].children.size(), hand_count_);
            infoset_key_suffixes_[node_idx] = tree.nodes[node_idx].key_suffix;
        }
    }
    for (std::uint32_t it = 0; it < iterations; ++it) {
        iteration_ = checked_next_u32_iteration(iteration_);
        for (auto& slot : infosets_) {
            if (slot.has_value()) {
                discount(*slot, iteration_, alpha_, beta_, gamma_);
            }
        }
        (void)traverse(tree, cache, 0, 0, root_reach_p0, root_reach_p1);
        (void)traverse(tree, cache, 0, 1, root_reach_p1, root_reach_p0);
    }
}

std::unordered_map<std::string, std::vector<double>> Class169VectorDCFR::average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    for (std::size_t node_index = 0;
         node_index < infosets_.size();
         ++node_index) {
        const auto& slot = infosets_[node_index];
        if (!slot.has_value()) {
            continue;
        }
        std::vector<double> avg;
        compute_avg_strategy(*slot, avg);
        const auto action_count = slot->action_count;
        for (std::size_t hand = 0; hand < hand_count_; ++hand) {
            const auto offset = hand * action_count;
            std::vector<double> row(
                avg.begin() + static_cast<std::ptrdiff_t>(offset),
                avg.begin() + static_cast<std::ptrdiff_t>(
                    offset + action_count));
            const auto inserted = out.emplace(
                class_label(static_cast<std::uint16_t>(hand)) +
                    infoset_key_suffixes_.at(node_index),
                std::move(row));
            if (!inserted.second) {
                throw std::logic_error(
                    "Class169VectorDCFR produced a duplicate strategy key");
            }
        }
    }
    return out;
}

std::uint32_t Class169VectorDCFR::iteration() const {
    return iteration_;
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
    validate_class169_reach(root_reach_p0, "root_reach_p0");
    validate_class169_reach(root_reach_p1, "root_reach_p1");

    const auto started = std::chrono::steady_clock::now();
    const auto tree = PreflopBettingTree::build(config);
    const auto cache = Class169TerminalCache::build(tree, class_combos, table);
    Class169VectorDCFR solver(PREFLOP_NUM_CLASSES, alpha, beta, gamma);
    solver.solve(tree, cache, iterations, root_reach_p0, root_reach_p1);

    Class169RvrOutput out;
    out.average_strategy = solver.average_strategy();
    const auto decision_node_count = static_cast<std::size_t>(std::count_if(
        tree.nodes.begin(),
        tree.nodes.end(),
        [](const auto& node) {
            return node.kind == PreflopBettingTree::NodeKind::Decision;
        }));
    if (decision_node_count > std::numeric_limits<std::uint32_t>::max() ||
        out.average_strategy.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error(
            "class-169 output metadata exceeds uint32_t");
    }
    out.decision_node_count =
        static_cast<std::uint32_t>(decision_node_count);
    out.strategy_entry_count =
        static_cast<std::uint32_t>(out.average_strategy.size());
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

}  // namespace texas::preflop


