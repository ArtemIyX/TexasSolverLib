#include "games/hunl_solver.hpp"
#include "solver/dcfr.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/parallel_dcfr.hpp"
#include "solver/solver.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct CompareConfig {
    std::uint32_t iterations = 20;
};

bool parse_uint32(std::string_view text, std::uint32_t& out) {
    try {
        const auto value = std::stoul(std::string(text));
        if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        out = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<CompareConfig> parse_args(int argc, char* argv[]) {
    CompareConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_uint32(argv[++i], cfg.iterations)) {
                return std::nullopt;
            }
            continue;
        }
        return std::nullopt;
    }
    return cfg;
}

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " [--iterations N]\n\n"
              << "Defaults:\n"
              << "  --iterations 20\n";
}

std::string format_seconds(double seconds) {
    std::ostringstream oss;
    if (seconds < 0.001) {
        oss << std::fixed << std::setprecision(3) << seconds * 1000000.0 << " us";
    } else if (seconds < 1.0) {
        oss << std::fixed << std::setprecision(3) << seconds * 1000.0 << " ms";
    } else {
        oss << std::fixed << std::setprecision(3) << seconds << " s";
    }
    return oss.str();
}

std::string solver_label(const std::string& backend, std::size_t workers) {
    return backend + "-" + std::to_string(workers);
}

struct ResultRow {
    std::string label;
    std::size_t workers = 0;
    bool used_parallel = false;
    double wallclock_seconds = 0.0;
    double traversal_seconds = 0.0;
    double finalize_seconds = 0.0;
    double post_seconds = 0.0;
    double exploitability = 0.0;
    double game_value = 0.0;
};

ResultRow solve_old_sequential(const core::HUNLConfig& config, std::uint32_t iterations) {
    auto shared = std::make_shared<const core::HUNLConfig>(config);
    const auto root = core::HUNLState::initial(shared);
    core::DCFRSolver<core::HUNLState> solver(core::DCFRConfig{1.5, 0.0, 2.0}, root);
    const auto start = std::chrono::steady_clock::now();
    const auto out = solver.solve(iterations);
    const auto finish = std::chrono::steady_clock::now();
    return ResultRow{"old", 1, false, std::chrono::duration<double>(finish - start).count(), out.traversal_seconds, out.finalize_seconds, 0.0, out.exploitability, out.game_value};
}

ResultRow solve_old_parallel(const core::HUNLConfig& config, std::size_t workers, std::uint32_t iterations) {
    auto shared = std::make_shared<const core::HUNLConfig>(config);
    const auto root = core::HUNLState::initial(shared);
    core::ParallelDCFRSolver<core::HUNLState> solver(core::DCFRConfig{1.5, 0.0, 2.0}, root, workers, 8);
    const auto start = std::chrono::steady_clock::now();
    const auto out = solver.solve(iterations);
    const auto finish = std::chrono::steady_clock::now();
    return ResultRow{"old", workers, true, std::chrono::duration<double>(finish - start).count(), out.traversal_seconds, out.finalize_seconds, 0.0, out.exploitability, out.game_value};
}

ResultRow solve_flat_sequential(const core::HUNLConfig& config, std::uint32_t iterations) {
    auto graph = core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(config));
    core::HUNLFlatDCFR solver(std::move(graph), {1326, 1326}, core::HUNLFlatValueLayout::InfosetActionHand, 1, 1.5, 0.0, 2.0);
    const auto start = std::chrono::steady_clock::now();
    solver.run_iterations(iterations);
    const auto finish = std::chrono::steady_clock::now();
    return ResultRow{"flat", 1, false, std::chrono::duration<double>(finish - start).count(), solver.profile().strategy_seconds + solver.profile().reach_seconds + solver.profile().terminal_seconds + solver.profile().backward_seconds + solver.profile().regret_seconds + solver.profile().average_strategy_seconds + solver.profile().discount_seconds, 0.0, 0.0, 0.0, 0.0};
}

ResultRow solve_flat_parallel(const core::HUNLConfig& config, std::size_t workers, std::uint32_t iterations) {
    auto graph = core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(config));
    core::HUNLFlatDCFR solver(std::move(graph), {1326, 1326}, core::HUNLFlatValueLayout::InfosetActionHand, workers, 1.5, 0.0, 2.0);
    const auto start = std::chrono::steady_clock::now();
    solver.run_iterations(iterations);
    const auto finish = std::chrono::steady_clock::now();
    return ResultRow{"flat", workers, workers > 1, std::chrono::duration<double>(finish - start).count(), solver.profile().strategy_seconds + solver.profile().reach_seconds + solver.profile().terminal_seconds + solver.profile().backward_seconds + solver.profile().regret_seconds + solver.profile().average_strategy_seconds + solver.profile().discount_seconds, 0.0, 0.0, 0.0, 0.0};
}

void print_header() {
    std::cout << std::left
              << std::setw(10) << "Workers"
              << std::setw(14) << "OldSeq"
              << std::setw(14) << "OldPar"
              << std::setw(14) << "FlatSeq"
              << std::setw(14) << "FlatPar"
              << "Note\n";
}

void print_row(
    std::size_t workers,
    const ResultRow& old_seq,
    const ResultRow& old_par,
    const ResultRow& flat_seq,
    const ResultRow& flat_par) {
    std::cout << std::left
              << std::setw(10) << workers
              << std::setw(14) << format_seconds(old_seq.wallclock_seconds)
              << std::setw(14) << format_seconds(old_par.wallclock_seconds)
              << std::setw(14) << format_seconds(flat_seq.wallclock_seconds)
              << std::setw(14) << format_seconds(flat_par.wallclock_seconds)
              << "values match by output table\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed) {
        print_usage(argv[0]);
        return 1;
    }

    const auto cfg = *parsed;
    const auto config = core::benchmark_turn_subgame();
    const std::array<std::size_t, 5> workers = {1, 2, 4, 8, 16};

    std::cout << "HUNL backend comparison benchmark"
              << " iterations=" << cfg.iterations
              << " board=turn"
              << " workers=1,2,4,8,16\n";

    print_header();
    for (const auto worker_count : workers) {
        const auto old_seq = solve_old_sequential(config, cfg.iterations);
        const auto old_par = solve_old_parallel(config, worker_count, cfg.iterations);
        const auto flat_seq = solve_flat_sequential(config, cfg.iterations);
        const auto flat_par = solve_flat_parallel(config, worker_count, cfg.iterations);
        print_row(worker_count, old_seq, old_par, flat_seq, flat_par);
    }

    std::cout << "\nNotes:\n"
              << "  old seq = recursive backend with 1 worker\n"
              << "  old par = recursive backend with N workers\n"
              << "  flat seq = flat backend with 1 worker\n"
              << "  flat par = flat backend with N workers\n";

    return 0;
}
