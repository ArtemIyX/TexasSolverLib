#include "core/lib.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_state.hpp"
#include "util/simd.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct BenchmarkConfig {
    std::string game;
    std::uint32_t iterations = 0;
    std::vector<std::size_t> workers;
    std::size_t frontier_multiplier = 8;
    bool force_parallel = false;
    bool profile = false;
    bool layout_benchmark = false;
};

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " <game> <iterations> <workers> [frontier_multiplier] [--force-parallel]\n\n"
              << "  " << exe << " layout [iterations]\n\n"
              << "Arguments:\n"
              << "  <game>        kuhn | leduc | hunl | layout\n"
              << "  <iterations>  positive integer\n"
              << "  <workers>     comma-separated list, e.g. 1,2,4,8\n"
              << "  [frontier_multiplier]  optional positive integer, default 8\n"
              << "  [--force-parallel]  benchmark only; bypass sequential fallback\n"
              << "  [--profile]  print internal solver profiling details\n"
              << "Layout mode:\n"
              << "  iterations    optional positive integer, default 200\n";
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
    if (argc >= 2 && std::string_view(argv[1]) == "layout") {
        BenchmarkConfig cfg;
        cfg.game = "layout";
        cfg.layout_benchmark = true;
        cfg.iterations = 200;
        if (argc >= 3 && !parse_uint32(argv[2], cfg.iterations)) {
            return std::nullopt;
        }
        return cfg;
    }

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

std::string layout_name(core::HUNLFlatValueLayout layout) {
    switch (layout) {
        case core::HUNLFlatValueLayout::InfosetHandAction:
            return "infoset-hand-action";
        case core::HUNLFlatValueLayout::InfosetActionHand:
            return "infoset-action-hand";
    }
    return "unknown";
}

struct LayoutBenchResult {
    core::HUNLFlatValueLayout layout = core::HUNLFlatValueLayout::InfosetHandAction;
    double total_ms = 0.0;
    double strategy_ms = 0.0;
    double strategy_sum_ms = 0.0;
    double regret_ms = 0.0;
    double checksum = 0.0;
};

LayoutBenchResult run_layout_benchmark_variant(
    const core::HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& hand_count_per_player,
    std::uint32_t iterations,
    core::HUNLFlatValueLayout layout) {
    using clock = std::chrono::steady_clock;

    auto table = core::HUNLFlatInfosetTable::build(graph, hand_count_per_player, layout);
    const auto& meta = table.meta();
    for (const auto& row : meta) {
        auto* regret = table.regret_mut(row.id);
        for (std::size_t i = 0; i < row.value_count; ++i) {
            regret[i] = static_cast<double>((i % (row.action_count + 5U)) + 1U) * 0.125;
        }
    }

    std::vector<double> positive_buffer(table.total_value_count(), 0.0);
    std::vector<double> action_values(table.total_value_count(), 0.0);
    std::vector<double> node_values(table.total_value_count(), 0.0);
    for (std::size_t i = 0; i < action_values.size(); ++i) {
        action_values[i] = static_cast<double>((i % 13U) + 1U) * 0.03125;
        node_values[i] = static_cast<double>((i % 7U) + 1U) * 0.015625;
    }

    double strategy_ms = 0.0;
    double strategy_sum_ms = 0.0;
    double regret_ms = 0.0;
    double checksum = 0.0;
    const auto total_start = clock::now();

    for (std::uint32_t iter = 0; iter < iterations; ++iter) {
        const auto strategy_start = clock::now();
        for (const auto& row : meta) {
            auto* regret = table.regret_mut(row.id);
            auto* strategy = table.current_strategy_mut(row.id);
            auto* positive = positive_buffer.data() + row.offset;
            if (layout == core::HUNLFlatValueLayout::InfosetHandAction) {
                for (std::size_t h = 0; h < row.hand_count; ++h) {
                    const auto hand_offset = h * static_cast<std::size_t>(row.action_count);
                    const double total = core::positive_regrets_and_total(
                        regret + hand_offset,
                        positive + hand_offset,
                        row.action_count);
                    if (total > 0.0) {
                        for (std::size_t a = 0; a < row.action_count; ++a) {
                            strategy[hand_offset + a] = positive[hand_offset + a] / total;
                        }
                    } else {
                        const double uniform = 1.0 / static_cast<double>(row.action_count);
                        for (std::size_t a = 0; a < row.action_count; ++a) {
                            strategy[hand_offset + a] = uniform;
                        }
                    }
                }
            } else {
                for (std::size_t h = 0; h < row.hand_count; ++h) {
                    double total = 0.0;
                    for (std::size_t a = 0; a < row.action_count; ++a) {
                        const auto idx = a * static_cast<std::size_t>(row.hand_count) + h;
                        positive[idx] = std::max(regret[idx], 0.0);
                        total += positive[idx];
                    }
                    if (total > 0.0) {
                        for (std::size_t a = 0; a < row.action_count; ++a) {
                            const auto idx = a * static_cast<std::size_t>(row.hand_count) + h;
                            strategy[idx] = positive[idx] / total;
                        }
                    } else {
                        const double uniform = 1.0 / static_cast<double>(row.action_count);
                        for (std::size_t a = 0; a < row.action_count; ++a) {
                            strategy[a * static_cast<std::size_t>(row.hand_count) + h] = uniform;
                        }
                    }
                }
            }
        }
        const auto strategy_finish = clock::now();

        const auto strategy_sum_start = clock::now();
        for (const auto& row : meta) {
            auto* strategy_sum = table.strategy_sum_mut(row.id);
            const auto* strategy = table.current_strategy(row.id);
            if (layout == core::HUNLFlatValueLayout::InfosetHandAction) {
                for (std::size_t h = 0; h < row.hand_count; ++h) {
                    const auto hand_offset = h * static_cast<std::size_t>(row.action_count);
                    core::update_strategy_sum(
                        strategy_sum + hand_offset,
                        strategy + hand_offset,
                        row.action_count,
                        1.0 + static_cast<double>(h) * 0.0001);
                }
            } else {
                for (std::size_t a = 0; a < row.action_count; ++a) {
                    const auto action_offset = a * static_cast<std::size_t>(row.hand_count);
                    for (std::size_t h = 0; h < row.hand_count; ++h) {
                        strategy_sum[action_offset + h] +=
                            (1.0 + static_cast<double>(h) * 0.0001) * strategy[action_offset + h];
                    }
                }
            }
        }
        const auto strategy_sum_finish = clock::now();

        const auto regret_start = clock::now();
        for (const auto& row : meta) {
            auto* regret = table.regret_mut(row.id);
            if (layout == core::HUNLFlatValueLayout::InfosetHandAction) {
                for (std::size_t h = 0; h < row.hand_count; ++h) {
                    const auto hand_offset = h * static_cast<std::size_t>(row.action_count);
                    core::update_regret_sum(
                        regret + hand_offset,
                        action_values.data() + row.offset + hand_offset,
                        row.action_count,
                        node_values[row.offset + hand_offset],
                        0.75);
                }
            } else {
                for (std::size_t h = 0; h < row.hand_count; ++h) {
                    for (std::size_t a = 0; a < row.action_count; ++a) {
                        const auto idx = a * static_cast<std::size_t>(row.hand_count) + h;
                        regret[idx] += 0.75 * (action_values[row.offset + idx] - node_values[row.offset + h]);
                    }
                }
            }
            checksum += regret[iter % row.value_count];
        }
        const auto regret_finish = clock::now();

        strategy_ms += std::chrono::duration<double, std::milli>(strategy_finish - strategy_start).count();
        strategy_sum_ms += std::chrono::duration<double, std::milli>(strategy_sum_finish - strategy_sum_start).count();
        regret_ms += std::chrono::duration<double, std::milli>(regret_finish - regret_start).count();
    }

    const auto total_finish = clock::now();
    return LayoutBenchResult{
        layout,
        std::chrono::duration<double, std::milli>(total_finish - total_start).count(),
        strategy_ms,
        strategy_sum_ms,
        regret_ms,
        checksum,
    };
}

void run_layout_benchmark(std::uint32_t iterations) {
    const auto config = std::make_shared<const core::HUNLConfig>(make_benchmark_hunl_config());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const std::array<std::size_t, 2> hand_count_per_player = {1326, 1326};

    const auto hand_action = run_layout_benchmark_variant(
        graph, hand_count_per_player, iterations, core::HUNLFlatValueLayout::InfosetHandAction);
    const auto action_hand = run_layout_benchmark_variant(
        graph, hand_count_per_player, iterations, core::HUNLFlatValueLayout::InfosetActionHand);

    std::cout << std::left
              << std::setw(22) << "Layout"
              << std::setw(14) << "Total"
              << std::setw(14) << "Strategy"
              << std::setw(14) << "StratSum"
              << std::setw(14) << "Regret"
              << "Checksum\n";

    const auto print_result = [](const LayoutBenchResult& result) {
        std::cout << std::left
                  << std::setw(22) << layout_name(result.layout)
                  << std::setw(14) << (std::to_string(result.total_ms).substr(0, 9) + " ms")
                  << std::setw(14) << (std::to_string(result.strategy_ms).substr(0, 9) + " ms")
                  << std::setw(14) << (std::to_string(result.strategy_sum_ms).substr(0, 9) + " ms")
                  << std::setw(14) << (std::to_string(result.regret_ms).substr(0, 9) + " ms")
                  << result.checksum << "\n";
    };

    print_result(hand_action);
    print_result(action_hand);

    const auto better = hand_action.total_ms <= action_hand.total_ms ? hand_action : action_hand;
    const auto slower = hand_action.total_ms <= action_hand.total_ms ? action_hand : hand_action;
    const auto speedup = slower.total_ms > 0.0 ? slower.total_ms / better.total_ms : 1.0;

    std::cout << "\nAdvice: select `" << layout_name(better.layout) << "`";
    std::cout << " because it is " << std::fixed << std::setprecision(3) << speedup
              << "x faster on this CPU microbenchmark.\n";
    if (better.layout == core::HUNLFlatValueLayout::InfosetHandAction) {
        std::cout << "This is also the simpler layout for regret matching and row-wise SIMD over actions.\n";
    } else {
        std::cout << "Use this only if repeated runs keep it ahead on your target CPU.\n";
    }
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

#if defined(_MSC_VER)
std::string seh_code_to_string(unsigned int code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        default: {
            std::ostringstream oss;
            oss << "SEH_0x" << std::hex << std::uppercase << code;
            return oss.str();
        }
    }
}

LONG WINAPI benchmark_exception_filter(EXCEPTION_POINTERS* info) {
    const auto code = info != nullptr && info->ExceptionRecord != nullptr
        ? static_cast<unsigned int>(info->ExceptionRecord->ExceptionCode)
        : 0U;
    std::cerr << "benchmark crashed with " << seh_code_to_string(code) << "\n";
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char* argv[]) {
#if defined(_MSC_VER)
    SetUnhandledExceptionFilter(benchmark_exception_filter);
#endif
    const auto parsed = parse_args(argc, argv);
    if (!parsed) {
        print_usage(argv[0]);
        return 1;
    }

    const auto& cfg = *parsed;
    if (cfg.layout_benchmark) {
        run_layout_benchmark(cfg.iterations);
        return 0;
    }
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
