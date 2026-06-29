#include "games/hunl_solver.hpp"

#include "solver/exploit.hpp"
#include "solver/solver.hpp"

#include <chrono>
#include <stdexcept>

namespace core {

namespace {

std::unordered_map<std::string, std::vector<double>> to_strategy_map(
    const std::vector<std::pair<InfosetKey, std::vector<Probability>>>& average_strategy) {
    std::unordered_map<std::string, std::vector<double>> out;
    out.reserve(average_strategy.size());
    for (const auto& [key, probs] : average_strategy) {
        out.emplace(key, probs);
    }
    return out;
}

std::size_t expected_board_len(Street street) {
    switch (street) {
        case Street::Flop:
            return 3;
        case Street::Turn:
            return 4;
        case Street::River:
            return 5;
        default:
            return 0;
    }
}

}  // namespace

void validate_config(const HUNLConfig& config) {
    if (config.starting_street == Street::Preflop) {
        throw std::invalid_argument("solve_hunl_postflop requires starting_street >= Flop");
    }
    if (!config.initial_hole_cards.has_value()) {
        throw std::invalid_argument(
            "solve_hunl_postflop requires initial_hole_cards = Some([[c0,c1],[c2,c3]])");
    }
    if (config.rake_rate != 0.0 || config.rake_cap != 0) {
        throw std::invalid_argument("solve_hunl_postflop does not support rake");
    }
    const auto expected = expected_board_len(config.starting_street);
    if (config.initial_board.size() != expected) {
        throw std::invalid_argument("initial_board length does not match starting_street");
    }
    config.validate();
}

HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers,
    std::size_t frontier_multiplier,
    bool force_parallel) {
    validate_config(config);

    const auto start = std::chrono::steady_clock::now();
    auto shared = std::make_shared<const HUNLConfig>(config);
    const auto root = HUNLState::initial(shared);

    SolveOutput solve_output;
    if (force_parallel ||
        detail::should_use_parallel_solver(workers, frontier_multiplier, detail::estimated_root_branch_count(root))) {
        ParallelDCFRSolver<HUNLState> solver(
            DCFRConfig{alpha, beta, gamma}, root, workers, frontier_multiplier);
        solve_output = solver.solve(iterations);
    } else {
        DCFRSolver<HUNLState> solver(DCFRConfig{alpha, beta, gamma}, root);
        solve_output = solver.solve(iterations);
    }
    const auto postprocess_start = std::chrono::steady_clock::now();
    HUNLSolveOutput out;
    out.average_strategy = to_strategy_map(solve_output.average_strategy);
    out.exploitability = solve_output.exploitability;
    out.game_value = solve_output.game_value;
    out.iterations = solve_output.iterations;
    out.used_parallel = solve_output.used_parallel;
    out.traversal_seconds = solve_output.traversal_seconds;
    out.solver_finalize_seconds = solve_output.finalize_seconds;
    out.infoset_count = static_cast<std::uint32_t>(out.average_strategy.size());
    const auto finish = std::chrono::steady_clock::now();
    out.wrapper_postprocess_seconds =
        std::chrono::duration<double>(finish - postprocess_start).count();
    out.wallclock_seconds =
        std::chrono::duration<double>(finish - start).count();
    return out;
}

}  // namespace core


