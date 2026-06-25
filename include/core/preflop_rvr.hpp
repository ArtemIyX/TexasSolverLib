#pragma once

#include "core/core.hpp"
#include "core/hunl.hpp"
#include "core/preflop_equity.hpp"
#include "core/dcfr_vector.hpp"
#include "core/solver.hpp"

#include <unordered_map>

namespace core {

struct PreflopRvrOutput {
    SolveOutput base;
    std::uint32_t decision_node_count = 0;
};

struct Class169Combos {
    std::array<std::vector<std::array<std::uint8_t, 2>>, PREFLOP_NUM_CLASSES> combos;

    static Class169Combos build();
};

std::size_t classify_suit_variant(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain);
std::array<std::vector<double>, 2> build_class169_blocker_mass(const Class169Combos& combos);

struct Class169LeafEntry {
    enum class Kind {
        NonTerminal,
        Fold,
        Equity,
    } kind = Kind::NonTerminal;

    std::array<double, 2> payoff = {0.0, 0.0};
    std::array<std::vector<double>, 2> payoff_table = {std::vector<double>{}, std::vector<double>{}};
};

struct PreflopBettingTree {
    enum class NodeKind {
        Fold,
        EquityLeaf,
        Decision,
    };

    struct Node {
        NodeKind kind = NodeKind::Fold;
        std::uint8_t player = 0;
        std::vector<std::size_t> children;
        std::vector<std::string> actions;
        std::string key_suffix;
        std::array<int, 2> contributions = {0, 0};
        std::array<int, 2> initial_contributions = {0, 0};
        int big_blind = 100;
        int initial_pot = 0;
        std::size_t folded_player = 0;
    };

    std::vector<Node> nodes;

    static PreflopBettingTree build(const HUNLConfig& config);
};

struct Class169TerminalCache {
    std::vector<Class169LeafEntry> leaves;
    std::array<std::vector<double>, 2> shared_blocker_mass;

    static Class169TerminalCache build(
        const PreflopBettingTree& tree,
        const Class169Combos& combos,
        const PreflopEquityTable& table);
};

std::array<std::vector<double>, 2> build_class169_leaf_payoff(
    const std::array<int, 2>& contributions,
    int big_blind,
    int initial_pot,
    const std::array<int, 2>& initial_contributions,
    const PreflopEquityTable& equity_table,
    const Class169Combos& class_combos);

struct Class169RvrOutput {
    std::unordered_map<std::string, std::vector<double>> average_strategy;
    std::uint32_t decision_node_count = 0;
    std::uint32_t strategy_entry_count = 0;
    std::uint32_t iterations = 0;
    std::array<std::size_t, 2> hand_count_per_player = {0, 0};
    double wallclock_seconds = 0.0;
};

class Class169VectorDCFR {
public:
    Class169VectorDCFR(std::size_t hand_count, double alpha, double beta, double gamma);

    void solve(
        const PreflopBettingTree& tree,
        const Class169TerminalCache& cache,
        std::uint32_t iterations,
        const std::vector<double>& root_reach_p0,
        const std::vector<double>& root_reach_p1);

    void solve(
        std::size_t decision_node_count,
        std::uint32_t iterations,
        const std::vector<double>& root_reach_p0,
        const std::vector<double>& root_reach_p1);

    std::unordered_map<std::string, std::vector<double>> average_strategy() const;
    std::uint32_t iteration() const;

private:
    std::size_t hand_count_ = 0;
    double alpha_ = 1.5;
    double beta_ = 0.0;
    double gamma_ = 2.0;
    std::uint32_t iteration_ = 0;
    std::vector<std::optional<VectorInfosetData>> infosets_;

    static void compute_strategy(const VectorInfosetData& info, std::vector<double>& out);
    static void compute_avg_strategy(const VectorInfosetData& info, std::vector<double>& out);
    static void discount(VectorInfosetData& info, std::uint32_t t, double alpha, double beta, double gamma);
    std::vector<double> traverse(
        const PreflopBettingTree& tree,
        const Class169TerminalCache& cache,
        std::size_t node_idx,
        std::size_t update_player,
        const std::vector<double>& reach_p,
        const std::vector<double>& reach_opp);
};

PreflopRvrOutput solve_hunl_preflop_rvr(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma);

Class169RvrOutput solve_hunl_preflop_rvr_class169(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::vector<double> root_reach_p0,
    std::vector<double> root_reach_p1,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma);

}  // namespace core
