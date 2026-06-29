#include "core/lib.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

struct BenchmarkConfig {
    std::string game;
    std::uint32_t iterations = 0;
    std::vector<std::size_t> workers;
    std::size_t frontier_multiplier = 8;
    bool force_parallel = false;
    bool profile = false;
};

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " <game> <iterations> <workers> [frontier_multiplier] [--force-parallel]\n\n"
              << "Arguments:\n"
              << "  <game>        kuhn | leduc | hunl\n"
              << "  <iterations>  positive integer\n"
              << "  <workers>     comma-separated list, e.g. 1,2,4,8\n"
              << "  [frontier_multiplier]  optional positive integer, default 8\n"
              << "  [--force-parallel]  benchmark only; bypass sequential fallback\n"
              << "  [--profile]  print internal solver profiling details\n";
}

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

bool parse_worker_list(std::string_view text, std::vector<std::size_t>& workers) {
    std::stringstream ss{std::string(text)};
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            return false;
        }
        try {
            const auto value = std::stoul(token);
            if (value == 0) {
                return false;
            }
            workers.push_back(static_cast<std::size_t>(value));
        } catch (...) {
            return false;
        }
    }
    std::sort(workers.begin(), workers.end());
    workers.erase(std::unique(workers.begin(), workers.end()), workers.end());
    return !workers.empty();
}

std::optional<BenchmarkConfig> parse_args(int argc, char* argv[]) {
    if (argc < 4 || argc > 7) {
        return std::nullopt;
    }

    BenchmarkConfig cfg;
    cfg.game = argv[1];
    if (!parse_uint32(argv[2], cfg.iterations)) {
        return std::nullopt;
    }
    if (!parse_worker_list(argv[3], cfg.workers)) {
        return std::nullopt;
    }

    for (int i = 4; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--force-parallel") {
            cfg.force_parallel = true;
            continue;
        }
        if (arg == "--profile") {
            cfg.profile = true;
            continue;
        }

        std::uint32_t parsed_multiplier = 0;
        if (!parse_uint32(arg, parsed_multiplier)) {
            return std::nullopt;
        }
        cfg.frontier_multiplier = static_cast<std::size_t>(parsed_multiplier);
    }
    return cfg;
}

std::string format_duration(std::chrono::steady_clock::duration elapsed) {
    using namespace std::chrono;
    const auto us = duration_cast<microseconds>(elapsed);
    if (us < milliseconds(1)) {
        return std::to_string(us.count()) + " us";
    }
    const auto ms = duration_cast<duration<double, std::milli>>(elapsed);
    if (ms.count() < 1000.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << ms.count() << " ms";
        return oss.str();
    }
    const auto sec = duration_cast<duration<double>>(elapsed);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << sec.count() << " s";
    return oss.str();
}

core::HUNLConfig make_benchmark_hunl_config() {
    return core::benchmark_turn_subgame();
}

template <class SolveFn>
void run_benchmark_rows(const BenchmarkConfig& cfg, SolveFn&& solve) {
    std::cout << std::left
              << std::setw(10) << "Workers"
              << std::setw(12) << "Solver"
              << std::setw(14) << "Total"
              << std::setw(14) << "Traverse"
              << std::setw(14) << "Finalize"
              << "Post\n";
    for (const auto workers : cfg.workers) {
        const auto start = std::chrono::steady_clock::now();
        const auto output = solve(workers);
        const auto finish = std::chrono::steady_clock::now();
        const auto total = format_duration(finish - start);
        std::string solver_kind = "n/a";
        std::string traverse = "-";
        std::string finalize = "-";
        std::string post = "-";

        if constexpr (std::is_same_v<std::decay_t<decltype(output)>, core::SolveOutput>) {
            solver_kind = output.used_parallel ? "parallel" : "sequential";
            traverse = format_duration(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(output.traversal_seconds)));
            finalize = format_duration(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(output.finalize_seconds)));
            post = "0 us";
        } else if constexpr (std::is_same_v<std::decay_t<decltype(output)>, core::HUNLSolveOutput>) {
            solver_kind = output.used_parallel ? "parallel" : "sequential";
            traverse = format_duration(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(output.traversal_seconds)));
            finalize = format_duration(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(output.solver_finalize_seconds)));
            post = format_duration(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(output.wrapper_postprocess_seconds)));
        }

        std::cout << std::left
                  << std::setw(10) << workers
                  << std::setw(12) << solver_kind
                  << std::setw(14) << total
                  << std::setw(14) << traverse
                  << std::setw(14) << finalize
                  << post << "\n";

        if (!cfg.profile) {
            continue;
        }

        const core::SolveProfile* profile = nullptr;
        if constexpr (std::is_same_v<std::decay_t<decltype(output)>, core::SolveOutput>) {
            profile = &output.profile;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(output)>, core::HUNLSolveOutput>) {
            profile = &output.profile;
        }

        if (profile == nullptr || !profile->enabled) {
            continue;
        }

        std::cout << "  profile:"
                  << " frontier=" << profile->frontier_seed_count
                  << " batches=" << profile->batch_count
                  << " frontier_build=" << format_duration(
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(profile->frontier_seconds)))
                  << " batch_build=" << format_duration(
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(profile->batch_build_seconds)))
                  << " snapshot=" << format_duration(
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(profile->snapshot_seconds)))
                  << " merge=" << format_duration(
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(profile->merge_seconds)))
                  << "\n";
        for (std::size_t i = 0; i < profile->workers.size(); ++i) {
            const auto& worker = profile->workers[i];
            std::cout << "  worker[" << i << "]"
                      << " cfr=" << format_duration(
                            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(worker.cfr_seconds)))
                      << " batches=" << worker.batches_taken
                      << " seeds=" << worker.seeds_processed
                      << " infosets=" << worker.infoset_count
                      << "\n";
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed) {
        print_usage(argv[0]);
        return 1;
    }

    const auto& cfg = *parsed;
    if (cfg.profile) {
#if defined(_MSC_VER)
        _putenv_s("TEXASSOLVER_PROFILE", "1");
#else
        setenv("TEXASSOLVER_PROFILE", "1", 1);
#endif
    }
    const double alpha = 1.5;
    const double beta = 0.0;
    const double gamma = 2.0;

    if (cfg.game == "kuhn") {
        run_benchmark_rows(cfg, [&](std::size_t workers) {
            return core::lib::solve_kuhn(
                cfg.iterations, alpha, beta, gamma, workers, cfg.frontier_multiplier);
        });
        return 0;
    }

    if (cfg.game == "leduc") {
        run_benchmark_rows(cfg, [&](std::size_t workers) {
            return core::lib::solve_leduc(
                cfg.iterations, alpha, beta, gamma, workers, cfg.frontier_multiplier);
        });
        return 0;
    }

    if (cfg.game == "hunl") {
        const auto hunl_config = make_benchmark_hunl_config();
        run_benchmark_rows(cfg, [&](std::size_t workers) {
            return core::lib::solve_hunl_postflop(
                hunl_config,
                cfg.iterations,
                alpha,
                beta,
                gamma,
                workers,
                cfg.frontier_multiplier,
                cfg.force_parallel);
        });
        return 0;
    }

    std::cerr << "Unknown game: " << cfg.game << "\n";
    print_usage(argv[0]);
    return 1;
}
