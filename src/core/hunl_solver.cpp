#include "core/hunl_solver.hpp"

#include "core/dcfr.hpp"
#include "core/exploit.hpp"

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
    double gamma) {
    validate_config(config);

    const auto start = std::chrono::steady_clock::now();
    auto shared = std::make_shared<const HUNLConfig>(config);
    const auto root = HUNLState::initial(shared);

    DCFRSolver<HUNLState> solver(DCFRConfig{alpha, beta, gamma}, root);
    const auto solve_output = solver.solve(iterations);
    const auto finish = std::chrono::steady_clock::now();

    HUNLSolveOutput out;
    out.average_strategy = to_strategy_map(solve_output.average_strategy);
    out.exploitability = solve_output.exploitability;
    out.game_value = solve_output.game_value;
    out.iterations = solve_output.iterations;
    out.wallclock_seconds =
        std::chrono::duration<double>(finish - start).count();
    out.infoset_count = static_cast<std::uint32_t>(out.average_strategy.size());
    return out;
}

}  // namespace core
