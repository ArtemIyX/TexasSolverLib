#include "games/hunl.hpp"
#include "games/hunl_solver.hpp"
#include "games/hunl_flat_graph.hpp"
#include "core/lib.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/solver.hpp"
#include "util/profiling.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kMemoryWarningBytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMemoryFailBytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;

struct RandomConfig {
    std::size_t workers = 16;
    std::uint32_t iterations = 10;
    std::uint32_t depth_limit_plies = 0;
    std::size_t hand_buckets = 1326;
    core::Street street = core::Street::River;
    core::HUNLFlatStoragePrecision precision = core::HUNLFlatStoragePrecision::Float64;
    std::uint64_t seed = 0;
    bool debug = false;
};

bool parse_uint64(std::string_view text, std::uint64_t& out) {
    try {
        const auto value = std::stoull(std::string(text));
        out = static_cast<std::uint64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
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

bool parse_workers(std::string_view text, std::size_t& out) {
    try {
        const auto value = std::stoul(std::string(text));
        if (value == 0) {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<core::Street> street_from_text(std::string_view text) {
    if (text == "flop") return core::Street::Flop;
    if (text == "turn") return core::Street::Turn;
    if (text == "river") return core::Street::River;
    return std::nullopt;
}

std::optional<core::HUNLFlatStoragePrecision> precision_from_text(std::string_view text) {
    if (text == "double" || text == "float64") return core::HUNLFlatStoragePrecision::Float64;
    if (text == "float" || text == "float32") return core::HUNLFlatStoragePrecision::Float32;
    if (text == "compressed16" || text == "fp16") return core::HUNLFlatStoragePrecision::Compressed16;
    return std::nullopt;
}

std::optional<RandomConfig> parse_args(int argc, char* argv[]) {
    RandomConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--workers" && i + 1 < argc) {
            if (!parse_workers(argv[++i], cfg.workers)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_uint32(argv[++i], cfg.iterations)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--depth-limit" && i + 1 < argc) {
            if (!parse_uint32(argv[++i], cfg.depth_limit_plies)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--buckets" && i + 1 < argc) {
            if (!parse_workers(argv[++i], cfg.hand_buckets)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--streets" && i + 1 < argc) {
            const auto street = street_from_text(argv[++i]);
            if (!street.has_value()) {
                return std::nullopt;
            }
            cfg.street = *street;
            continue;
        }
        if (arg == "--seed" && i + 1 < argc) {
            if (!parse_uint64(argv[++i], cfg.seed)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--precision" && i + 1 < argc) {
            const auto precision = precision_from_text(argv[++i]);
            if (!precision.has_value()) {
                return std::nullopt;
            }
            cfg.precision = *precision;
            continue;
        }
        if (arg == "--debug") {
            cfg.debug = true;
            continue;
        }
        return std::nullopt;
    }
    return cfg;
}

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " [--workers N] [--iterations N] [--depth-limit N] [--buckets N] [--streets flop|turn|river] [--precision double|float] [--seed N] [--debug]\n\n"
              << "Defaults:\n"
              << "  workers=16 iterations=10 depth-limit=0 buckets=1326 streets=river precision=double seed=0\n";
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

std::string street_name(core::Street street) {
    switch (street) {
        case core::Street::Flop: return "flop";
        case core::Street::Turn: return "turn";
        case core::Street::River: return "river";
        default: return "unknown";
    }
}

std::string cards_to_string(const std::array<std::uint8_t, 2>& cards) {
    return core::card_to_string(cards[0]) + core::card_to_string(cards[1]);
}

std::string board_to_string(const std::vector<std::uint8_t>& cards) {
    std::string out;
    for (const auto card : cards) {
        out += core::card_to_string(card);
    }
    return out;
}

using StrategyMap = std::unordered_map<std::string, std::vector<double>>;

StrategyMap to_strategy_map(const std::unordered_map<std::string, std::vector<double>>& entries) {
    StrategyMap strategy;
    strategy.reserve(entries.size());
    for (const auto& [key, probs] : entries) {
        strategy.emplace(key, probs);
    }
    return strategy;
}

struct RandomState {
    core::HUNLConfig config;
    core::HUNLState state;
};

struct TimedBenchmarkResult {
    core::HUNLFlatAverageStrategyTable strategy_table;
    std::unordered_map<std::string, std::vector<double>> exported_strategy;
    StrategyMap strategy;
    std::array<double, 2> expected_value = {0.0, 0.0};
    double exploitability = 0.0;
    std::uint32_t iterations = 0;
    std::size_t worker_count = 0;
    double setup_seconds = 0.0;
    double solve_seconds = 0.0;
    double export_seconds = 0.0;
    double ev_seconds = 0.0;
    double exploit_seconds = 0.0;
    core::HUNLFlatStageProfile profile{};
};

RandomState make_random_state(const RandomConfig& cfg) {
    std::mt19937_64 rng(cfg.seed == 0 ? std::random_device{}() : cfg.seed);
    const std::size_t board_count = cfg.street == core::Street::Flop ? 3 : cfg.street == core::Street::Turn ? 4 : 5;

    std::vector<std::uint8_t> deck;
    deck.reserve(52);
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            deck.push_back(core::card_to_int(rank, suit));
        }
    }
    std::shuffle(deck.begin(), deck.end(), rng);

    core::HUNLConfig config;
    config.starting_stack = 1000;
    config.starting_street = cfg.street;
    config.initial_pot = 150;
    config.initial_contributions = {50, 100};
    config.initial_board.assign(deck.begin(), deck.begin() + static_cast<std::ptrdiff_t>(board_count));

    std::array<std::array<std::uint8_t, 2>, 2> hole = {{
        {deck[board_count + 0], deck[board_count + 1]},
        {deck[board_count + 2], deck[board_count + 3]},
    }};
    for (auto& hand : hole) {
        if (hand[0] > hand[1]) {
            std::swap(hand[0], hand[1]);
        }
    }
    config.initial_hole_cards = hole;

    const int small_extra = static_cast<int>(rng() % 41ULL) - 20;
    const int big_extra = static_cast<int>(rng() % 41ULL) - 20;
    config.initial_contributions = {50 + small_extra, 100 + big_extra};
    config.initial_contributions[0] = std::clamp(config.initial_contributions[0], 1, config.starting_stack - 1);
    config.initial_contributions[1] = std::clamp(config.initial_contributions[1], 1, config.starting_stack - 1);
    config.initial_pot = config.initial_contributions[0] + config.initial_contributions[1];

    auto state = core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(config));
    return RandomState{std::move(config), std::move(state)};
}

void print_state(const core::HUNLConfig& config, const core::HUNLState& state) {
    std::cout << "config:\n";
    std::cout << "  street=" << street_name(config.starting_street) << "\n";
    std::cout << "  stacks=" << config.starting_stack << "," << config.starting_stack << "\n";
    std::cout << "  contributions=" << config.initial_contributions[0] << "," << config.initial_contributions[1] << "\n";
    std::cout << "  pot=" << config.initial_pot << "\n";
    std::cout << "state:\n";
    std::cout << "  cur_player=" << state.cur_player << "\n";
    std::cout << "  stacks=" << state.stacks[0] << "," << state.stacks[1] << "\n";
    std::cout << "  contributions=" << state.contributions[0] << "," << state.contributions[1] << "\n";
    std::cout << "  board=" << board_to_string(state.board) << "\n";
    if (state.hole_cards.has_value()) {
        std::cout << "  hole0=" << cards_to_string((*state.hole_cards)[0]) << "\n";
        std::cout << "  hole1=" << cards_to_string((*state.hole_cards)[1]) << "\n";
    }
    std::cout << "  infoset0=" << state.infoset_key(0) << "\n";
    if (state.cur_player >= 0) {
        std::cout << "  legal_actions=";
        const auto actions = state.legal_actions();
        for (std::size_t i = 0; i < actions.size(); ++i) {
            if (i > 0) {
                std::cout << ",";
            }
            std::cout << actions[i];
        }
        std::cout << "\n";
    }
}

double seconds_per_iteration(double seconds, std::uint32_t iterations) {
    return iterations > 0 ? seconds / static_cast<double>(iterations) : 0.0;
}

void set_flat_backend_env() {
#if defined(_MSC_VER)
    _putenv_s("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat");
#else
    setenv("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat", 1);
#endif
}

std::string format_bytes(std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    std::ostringstream oss;
    if (bytes >= static_cast<std::uint64_t>(gib)) {
        oss << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / gib << " GiB";
    } else if (bytes >= static_cast<std::uint64_t>(mib)) {
        oss << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / mib << " MiB";
    } else if (bytes >= static_cast<std::uint64_t>(kib)) {
        oss << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / kib << " KiB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string precision_name(core::HUNLFlatStoragePrecision precision) {
    switch (precision) {
        case core::HUNLFlatStoragePrecision::Float64:
            return "double";
        case core::HUNLFlatStoragePrecision::Float32:
            return "float";
        case core::HUNLFlatStoragePrecision::Compressed16:
            return "compressed16";
    }
    return "unknown";
}

std::size_t max_backward_row_width(const core::HUNLFlatSolveGraph& graph) {
    std::size_t max_width = 0;
    for (const auto& meta : graph.node_meta) {
        max_width = std::max<std::size_t>(
            max_width,
            std::max<std::size_t>(meta.child_count, meta.chance_count));
    }
    return max_width;
}

std::size_t max_bucket_width(const core::HUNLFlatInfosetTable& table) {
    std::size_t max_width = 0;
    for (const auto& meta : table.meta()) {
        max_width = std::max<std::size_t>(max_width, meta.bucket_count);
    }
    return max_width;
}

void print_memory_estimate(const core::HUNLFlatMemoryEstimate& estimate) {
    std::cout << "memory_preflight:\n";
    std::cout << "  graph=" << format_bytes(estimate.graph_bytes) << "\n";
    std::cout << "  infoset_table=" << format_bytes(estimate.infoset_table_bytes) << "\n";
    std::cout << "  solver_buffers=" << format_bytes(estimate.solver_buffers_bytes) << "\n";
    std::cout << "  worker_scratch=" << format_bytes(estimate.worker_scratch_bytes) << "\n";
    std::cout << "  parallel_plan=" << format_bytes(estimate.parallel_plan_bytes) << "\n";
    std::cout << "  auxiliary=" << format_bytes(estimate.auxiliary_bytes) << "\n";
    std::cout << "  total=" << format_bytes(estimate.total_bytes()) << "\n";
}

bool enforce_memory_guardrails(const core::HUNLFlatMemoryEstimate& estimate) {
    if (estimate.total_bytes() > kMemoryFailBytes) {
        std::cerr << "fatal: estimated memory " << format_bytes(estimate.total_bytes())
                  << " exceeds hard limit of " << format_bytes(kMemoryFailBytes)
                  << "; sampled mode is not implemented for this benchmark, aborting.\n";
        return false;
    }
    if (estimate.total_bytes() > kMemoryWarningBytes) {
        std::cerr << "warning: estimated memory " << format_bytes(estimate.total_bytes())
                  << " exceeds warning threshold of " << format_bytes(kMemoryWarningBytes) << ".\n";
    }
    return true;
}

void set_profile_env(bool enabled) {
#if defined(_MSC_VER)
    _putenv_s("TEXASSOLVER_PROFILE", enabled ? "1" : "0");
#else
    setenv("TEXASSOLVER_PROFILE", enabled ? "1" : "0", 1);
#endif
}

void set_profile_dir_env() {
#if defined(_MSC_VER)
    _putenv_s("TEXASSOLVER_PROFILE_DIR", "artifacts/prof");
#else
    setenv("TEXASSOLVER_PROFILE_DIR", "artifacts/prof", 1);
#endif
}

TimedBenchmarkResult run_timed_flat_benchmark(
    const RandomState& random_state,
    std::uint32_t iterations,
    std::size_t workers,
    std::size_t hand_buckets,
    core::HUNLFlatStoragePrecision precision,
    double alpha,
    double beta,
    double gamma) {
    using clock = std::chrono::steady_clock;

    const auto setup_start = clock::now();
    auto shared = std::make_shared<const core::HUNLConfig>(random_state.config);
    const auto graph = core::HUNLFlatSolveGraph::build(shared);
    const std::array<std::size_t, 2> buckets = {hand_buckets, hand_buckets};
    const auto table = core::HUNLFlatInfosetTable::build(
        graph,
        buckets,
        core::HUNLFlatValueLayout::InfosetHandAction,
        precision);
    core::HUNLFlatMemoryEstimateOptions memory_options;
    memory_options.max_child_count = max_backward_row_width(graph);
    memory_options.max_bucket_count = max_bucket_width(table);
    const auto memory = core::estimate_memory(graph, table, workers, memory_options);
    print_memory_estimate(memory);
    if (!enforce_memory_guardrails(memory)) {
        throw std::runtime_error("estimated memory exceeds configured benchmark limit");
    }
    core::HUNLFlatDCFR solver(
        graph,
        buckets,
        core::HUNLFlatValueLayout::InfosetHandAction,
        workers,
        alpha,
        beta,
        gamma,
        precision);
    const auto setup_end = clock::now();
    core::profiling::mark(
        "hunl.bench.setup",
        std::chrono::duration<double>(setup_end - setup_start).count());

    const auto solve_start = setup_end;
    solver.run_iterations(iterations);
    const auto solve_end = clock::now();
    core::profiling::mark(
        "hunl.bench.solve",
        std::chrono::duration<double>(solve_end - solve_start).count());

    const auto export_start = solve_end;
    const auto strategy_table = solver.export_average_strategy_table();
    const auto exported = solver.export_average_strategy();
    StrategyMap strategy;
    strategy.reserve(exported.size());
    for (const auto& [key, probs] : exported) {
        strategy.emplace(key, probs);
    }
    const auto export_end = clock::now();
    core::profiling::mark(
        "hunl.bench.export",
        std::chrono::duration<double>(export_end - export_start).count());

    const auto ev_start = export_end;
    const auto terminal_values_p0 = core::build_flat_terminal_value_table_p0_for_benchmark(graph);
    const auto game_value = core::compute_flat_expected_value_p0_benchmark(
        graph,
        strategy_table.view(),
        terminal_values_p0);
    const std::array<double, 2> expected_value = {game_value, -game_value};
    const auto ev_end = clock::now();
    core::profiling::mark(
        "hunl.bench.expected_value",
        std::chrono::duration<double>(ev_end - ev_start).count());

    const auto exploit_start = ev_end;
    const auto exploitability = core::detail::exploitability<core::HUNLState>(strategy);
    const auto exploit_end = clock::now();
    core::profiling::mark(
        "hunl.bench.exploitability",
        std::chrono::duration<double>(exploit_end - exploit_start).count());
    core::profiling::mark(
        "hunl.bench.total",
        std::chrono::duration<double>(exploit_end - setup_start).count());

    TimedBenchmarkResult result{
        std::move(strategy_table),
        std::move(exported),
        std::move(strategy),
        expected_value,
        exploitability,
        solver.iterations(),
        solver.worker_count(),
        std::chrono::duration<double>(setup_end - setup_start).count(),
        std::chrono::duration<double>(solve_end - solve_start).count(),
        std::chrono::duration<double>(export_end - export_start).count(),
        std::chrono::duration<double>(ev_end - ev_start).count(),
        std::chrono::duration<double>(exploit_end - exploit_start).count(),
        solver.profile(),
    };
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto parsed = parse_args(argc, argv);
        if (!parsed) {
            print_usage(argv[0]);
            return 1;
        }

        const auto cfg = *parsed;
        set_flat_backend_env();
        set_profile_env(cfg.debug);
        if (cfg.debug) {
            set_profile_dir_env();
        }

        const auto random_state = make_random_state(cfg);
        const auto start = std::chrono::steady_clock::now();

        std::cout << "HUNL random flat benchmark\n";
        std::cout << "workers=" << cfg.workers
                  << " iterations=" << cfg.iterations
                  << " depth_limit=" << cfg.depth_limit_plies
                  << " buckets=" << cfg.hand_buckets
                  << " street=" << street_name(cfg.street)
                  << " precision=" << precision_name(cfg.precision)
                  << " seed=" << cfg.seed
                  << " backend=flat\n";
        print_state(random_state.config, random_state.state);

        if (cfg.debug) {
            std::cout << "debug: starting solve with flat backend forced via env\n";
            std::cout << "debug: TEXASSOLVER_PROFILE=1\n";
            std::cout << "debug: TEXASSOLVER_PROFILE_DIR=artifacts/prof\n";
        }

        auto solve_config = random_state.config;
        solve_config.depth_limit_plies = cfg.depth_limit_plies;
        auto timed = run_timed_flat_benchmark(
            RandomState{std::move(solve_config), random_state.state},
            cfg.iterations,
            cfg.workers,
            cfg.hand_buckets,
            cfg.precision,
            1.5,
            0.0,
            2.0);

        const auto finish = std::chrono::steady_clock::now();
        const auto wallclock = std::chrono::duration<double>(finish - start).count();
        const auto per_iter = seconds_per_iteration(wallclock, timed.iterations);

        std::cout << "\nresults:\n";
        std::cout << "  iterations=" << timed.iterations << "\n";
        std::cout << "  wallclock=" << format_seconds(wallclock) << "\n";
        std::cout << "  per_iteration=" << format_seconds(per_iter) << "\n";
        std::cout << "  setup_seconds=" << format_seconds(timed.setup_seconds) << "\n";
        std::cout << "  solve_seconds=" << format_seconds(timed.solve_seconds) << "\n";
        std::cout << "  export_seconds=" << format_seconds(timed.export_seconds) << "\n";
        std::cout << "  expected_value_seconds=" << format_seconds(timed.ev_seconds) << "\n";
        std::cout << "  exploitability_seconds=" << format_seconds(timed.exploit_seconds) << "\n";
        std::cout << "  exploitability=" << std::fixed << std::setprecision(9) << timed.exploitability << "\n";
        std::cout << "  game_value=" << std::fixed << std::setprecision(9) << timed.expected_value[0] << "\n";
        std::cout << "  computed_game_value=" << std::fixed << std::setprecision(9) << timed.expected_value[0] << "\n";
        std::cout << "  computed_exploitability=" << std::fixed << std::setprecision(9) << timed.exploitability << "\n";
        std::cout << "  infosets=" << timed.strategy.size() << "\n";
        std::cout << "  used_parallel=" << (timed.worker_count > 1 ? "true" : "false") << "\n";
        std::cout << "  strategy_root:\n";

        const auto root_key = random_state.state.infoset_key(static_cast<std::uint8_t>(random_state.state.cur_player)) +
            "|board:" + core::sorted_card_string(random_state.state.board) +
            "|street:" + core::street_token(random_state.state.street);
        const auto it = std::find_if(timed.exported_strategy.begin(), timed.exported_strategy.end(), [&](const auto& item) {
            return item.first == root_key;
        });
        if (it != timed.exported_strategy.end()) {
            const auto actions = random_state.state.legal_actions();
            std::cout << "    key=" << root_key << "\n";
            for (std::size_t i = 0; i < actions.size() && i < it->second.size(); ++i) {
                std::cout << "    action[" << actions[i] << "]=" << std::fixed << std::setprecision(6) << it->second[i] << "\n";
            }
        } else {
            std::cout << "    root strategy not found in output table\n";
        }

        if (cfg.debug) {
            std::cout << "\nprofile:\n";
            std::cout << "  discount_seconds=" << format_seconds(timed.profile.discount_seconds) << "\n";
            std::cout << "  strategy_seconds=" << format_seconds(timed.profile.strategy_seconds) << "\n";
            std::cout << "  reach_seconds=" << format_seconds(timed.profile.reach_seconds) << "\n";
            std::cout << "  terminal_seconds=" << format_seconds(timed.profile.terminal_seconds) << "\n";
            std::cout << "  backward_seconds=" << format_seconds(timed.profile.backward_seconds) << "\n";
            std::cout << "  regret_seconds=" << format_seconds(timed.profile.regret_seconds) << "\n";
            std::cout << "  average_strategy_seconds=" << format_seconds(timed.profile.average_strategy_seconds) << "\n";
            core::profiling::print_profiler_report();
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal error: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "fatal error: unknown exception\n";
        return 1;
    }
}
