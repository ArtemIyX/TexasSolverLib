#include "games/hunl_solver.hpp"

#include "solver/exploit.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/solver.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace core {

namespace {

std::string trim_copy(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

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

bool should_use_flat_hunl_backend(
    const HUNLConfig& config,
    std::size_t workers,
    bool force_parallel) {
    if (config.starting_street == Street::River) {
        return false;
    }
    return force_parallel || workers > 1 || config.starting_street == Street::Turn;
}

WorkerProfile to_worker_profile(const HUNLFlatStageProfile& flat_profile) {
    WorkerProfile profile;
    profile.cfr_seconds =
        flat_profile.discount_seconds +
        flat_profile.strategy_seconds +
        flat_profile.reach_seconds +
        flat_profile.terminal_seconds +
        flat_profile.backward_seconds +
        flat_profile.regret_seconds +
        flat_profile.average_strategy_seconds;
    return profile;
}

std::vector<WorkerProfile> to_worker_profiles(
    const std::vector<HUNLFlatStageProfile>& flat_profiles) {
    std::vector<WorkerProfile> out;
    out.reserve(flat_profiles.size());
    for (const auto& flat_profile : flat_profiles) {
        out.push_back(to_worker_profile(flat_profile));
    }
    return out;
}

}  // namespace

HUNLBackendSelection hunl_backend_selection_from_env() {
    char* raw_value = nullptr;
#if defined(_MSC_VER)
    std::size_t len = 0;
    if (_dupenv_s(&raw_value, &len, "TEXASSOLVER_HUNL_FLAT_BACKEND") != 0) {
        raw_value = nullptr;
    }
#else
    raw_value = std::getenv("TEXASSOLVER_HUNL_FLAT_BACKEND");
#endif
    if (raw_value == nullptr) {
        return HUNLBackendSelection::Auto;
    }

    std::string value(raw_value);
#if defined(_MSC_VER)
    free(raw_value);
#endif
    value = trim_copy(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "flat" || value == "1" || value == "true" || value == "on") {
        return HUNLBackendSelection::Flat;
    }
    if (value == "recursive" || value == "legacy" || value == "0" || value == "false" || value == "off") {
        return HUNLBackendSelection::Recursive;
    }
    return HUNLBackendSelection::Auto;
}

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
    SolveOutput solve_output;
    const auto selection = hunl_backend_selection_from_env();
    const bool use_flat_backend =
        selection == HUNLBackendSelection::Flat ||
        (selection == HUNLBackendSelection::Auto &&
         should_use_flat_hunl_backend(config, workers, force_parallel));

    if (use_flat_backend) {
        auto shared = std::make_shared<const HUNLConfig>(config);
        const auto graph = HUNLFlatSolveGraph::build(shared);
        std::array<std::size_t, 2> hand_count_per_player = {1326, 1326};
        if (config.initial_hole_cards.has_value()) {
            hand_count_per_player = {1, 1};
        }

        HUNLFlatDCFR solver(
            std::move(graph),
            hand_count_per_player,
            HUNLFlatValueLayout::InfosetHandAction,
            workers,
            alpha,
            beta,
            gamma);
        solver.run_iterations(iterations);

        solve_output.iterations = solver.iterations();
        solve_output.used_parallel = solver.worker_count() > 1;
        const auto exported = solver.export_average_strategy();
        solve_output.average_strategy.reserve(exported.size());
        for (auto& [key, probs] : exported) {
            solve_output.average_strategy.emplace_back(std::move(key), std::move(probs));
        }
        std::sort(
            solve_output.average_strategy.begin(),
            solve_output.average_strategy.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        std::unordered_map<InfosetKey, std::vector<Probability>> strategy;
        strategy.reserve(solve_output.average_strategy.size());
        for (const auto& [key, probs] : solve_output.average_strategy) {
            strategy.emplace(key, probs);
        }
        const auto state = HUNLState::initial(shared);
        const auto value = detail::expected_value(state, strategy);
        solve_output.game_value = value[0];
        solve_output.exploitability = detail::exploitability<HUNLState>(strategy);

        solve_output.profile.enabled = true;
        solve_output.profile.discount_seconds = solver.profile().discount_seconds;
        solve_output.profile.strategy_seconds = solver.profile().strategy_seconds;
        solve_output.profile.reach_seconds = solver.profile().reach_seconds;
        solve_output.profile.terminal_seconds = solver.profile().terminal_seconds;
        solve_output.profile.backward_seconds = solver.profile().backward_seconds;
        solve_output.profile.regret_seconds = solver.profile().regret_seconds;
        solve_output.profile.average_strategy_seconds = solver.profile().average_strategy_seconds;
        solve_output.profile.workers = to_worker_profiles(solver.scheduler_diagnostics().worker_profiles);
    } else {
        auto shared = std::make_shared<const HUNLConfig>(config);
        const auto root = HUNLState::initial(shared);
        const bool use_parallel =
            selection != HUNLBackendSelection::Recursive &&
            (force_parallel ||
             detail::should_use_parallel_solver(workers, frontier_multiplier, detail::estimated_root_branch_count(root)));

        if (use_parallel) {
            ParallelDCFRSolver<HUNLState> solver(
                DCFRConfig{alpha, beta, gamma}, root, workers, frontier_multiplier);
            solve_output = solver.solve(iterations);
        } else {
            DCFRSolver<HUNLState> solver(DCFRConfig{alpha, beta, gamma}, root);
            solve_output = solver.solve(iterations);
        }
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
    out.profile = solve_output.profile;
    out.infoset_count = static_cast<std::uint32_t>(out.average_strategy.size());
    const auto finish = std::chrono::steady_clock::now();
    out.wrapper_postprocess_seconds =
        std::chrono::duration<double>(finish - postprocess_start).count();
    out.wallclock_seconds =
        std::chrono::duration<double>(finish - start).count();
    return out;
}

}  // namespace core
