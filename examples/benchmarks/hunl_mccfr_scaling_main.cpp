#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_flat_mccfr.hpp"
#include "solver/hunl_sampled_config.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct BenchmarkConfig {
    std::string preset = "tiny";
    std::uint32_t iterations = 200;
    std::uint32_t traversals_per_iteration = 2048;
    std::uint32_t batch_size = 64;
    std::uint64_t seed = 1;
    std::vector<std::size_t> workers = {1, 2, 4, 8, 16};
    core::HUNLFlatSamplingMode mode = core::HUNLFlatSamplingMode::External;
    core::HUNLFlatValueLayout layout = core::HUNLFlatValueLayout::InfosetActionHand;
    core::HUNLFlatStoragePrecision precision = core::HUNLFlatStoragePrecision::Float64;
    bool update_both_players = true;
    bool use_sparse_storage = false;
    bool use_discounting = false;
};

struct BenchmarkResult {
    std::string graph_name;
    std::uint32_t node_count = 0;
    std::size_t workers = 1;
    double total_seconds = 0.0;
    double seconds_per_iteration = 0.0;
    double iterations_per_second = 0.0;
    double expected_value_p0 = 0.0;
    double traverse_seconds = 0.0;
    double merge_seconds = 0.0;
    std::uint64_t total_nodes_visited = 0;
    std::uint64_t sampled_opponent_actions = 0;
    std::uint64_t traversing_action_expansions = 0;
};

struct GraphPresetSpec {
    std::string name;
    std::uint32_t root_actions = 0;
    std::uint32_t chance_outcomes = 0;
    std::uint32_t opponent_actions = 0;
    std::uint32_t reply_actions = 0;
};

bool parse_positive_u32(std::string_view text, std::uint32_t& out) {
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

bool parse_u64(std::string_view text, std::uint64_t& out) {
    try {
        out = static_cast<std::uint64_t>(std::stoull(std::string(text)));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_workers_csv(std::string_view text, std::vector<std::size_t>& out) {
    std::vector<std::size_t> parsed;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find(',', begin);
        const auto token = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
        if (token.empty()) {
            return false;
        }
        try {
            const auto value = static_cast<std::size_t>(std::stoul(std::string(token)));
            if (value == 0) {
                return false;
            }
            parsed.push_back(value);
        } catch (...) {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }

    if (parsed.empty()) {
        return false;
    }
    std::sort(parsed.begin(), parsed.end());
    parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
    out = std::move(parsed);
    return true;
}

std::optional<core::HUNLFlatSamplingMode> parse_sampling_mode(std::string_view text) {
    if (text == "exact") {
        return core::HUNLFlatSamplingMode::Exact;
    }
    if (text == "public-chance") {
        return core::HUNLFlatSamplingMode::PublicChance;
    }
    if (text == "external") {
        return core::HUNLFlatSamplingMode::External;
    }
    if (text == "average-strategy") {
        return core::HUNLFlatSamplingMode::AverageStrategy;
    }
    return std::nullopt;
}

std::string sampling_mode_name(core::HUNLFlatSamplingMode mode) {
    switch (mode) {
        case core::HUNLFlatSamplingMode::Exact:
            return "exact";
        case core::HUNLFlatSamplingMode::PublicChance:
            return "public-chance";
        case core::HUNLFlatSamplingMode::External:
            return "external";
        case core::HUNLFlatSamplingMode::AverageStrategy:
            return "average-strategy";
    }
    return "unknown";
}

std::optional<GraphPresetSpec> parse_preset(std::string_view text) {
    if (text == "tiny") {
        return GraphPresetSpec{"synthetic-easy-flop", 4, 2, 4, 4};
    }
    if (text == "scaling") {
        return GraphPresetSpec{"synthetic-scaling-flop", 6, 4, 6, 6};
    }
    return std::nullopt;
}

void print_usage(const char* exe) {
    std::cerr
        << "Usage:\n"
        << "  " << exe
        << " [--preset tiny|scaling] [--iterations N] [--traversals N] [--batch-size N] [--seed N]\n"
        << "      [--workers 1,2,4,8,16] [--mode exact|public-chance|external|average-strategy]\n"
        << "      [--sparse] [--discounting] [--single-player]\n\n"
        << "Defaults:\n"
        << "  preset=tiny iterations=200 traversals=2048 batch-size=64 seed=1\n"
        << "  workers=1,2,4,8,16 mode=external update-both-players=true\n";
}

std::optional<BenchmarkConfig> parse_args(int argc, char* argv[]) {
    BenchmarkConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) {
            const auto preset = parse_preset(argv[++i]);
            if (!preset.has_value()) {
                return std::nullopt;
            }
            config.preset = std::string(argv[i]);
            if (config.preset == "scaling") {
                config.iterations = 100;
                config.traversals_per_iteration = 8192;
                config.batch_size = 256;
            } else {
                config.iterations = 200;
                config.traversals_per_iteration = 2048;
                config.batch_size = 64;
            }
            continue;
        }
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_positive_u32(argv[++i], config.iterations)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--traversals" && i + 1 < argc) {
            if (!parse_positive_u32(argv[++i], config.traversals_per_iteration)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--batch-size" && i + 1 < argc) {
            if (!parse_positive_u32(argv[++i], config.batch_size)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--seed" && i + 1 < argc) {
            if (!parse_u64(argv[++i], config.seed)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--workers" && i + 1 < argc) {
            if (!parse_workers_csv(argv[++i], config.workers)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--mode" && i + 1 < argc) {
            const auto mode = parse_sampling_mode(argv[++i]);
            if (!mode.has_value()) {
                return std::nullopt;
            }
            config.mode = *mode;
            continue;
        }
        if (arg == "--sparse") {
            config.use_sparse_storage = true;
            continue;
        }
        if (arg == "--discounting") {
            config.use_discounting = true;
            continue;
        }
        if (arg == "--single-player") {
            config.update_both_players = false;
            continue;
        }
        return std::nullopt;
    }
    return config;
}

core::HUNLFlatNodeMeta make_terminal_meta(double value) {
    core::HUNLFlatNodeMeta meta;
    meta.type = core::HUNLFlatNodeType::TerminalFold;
    meta.terminal_utility = {value, -value};
    meta.terminal_kind = core::TerminalKind::fold(1, 1);
    meta.street = core::Street::Flop;
    return meta;
}

core::HUNLFlatSolveGraph make_benchmark_graph(const GraphPresetSpec& spec) {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 4;
    graph.max_actions = static_cast<std::uint8_t>(
        std::max({spec.root_actions, spec.opponent_actions, spec.reply_actions}));

    const auto root_infoset = core::InfosetId{0};
    const auto opponent_infoset = core::InfosetId{1};
    const auto reply_infoset = core::InfosetId{2};

    graph.infosets.push_back(core::HUNLFlatInfoset{
        root_infoset,
        0,
        1,
        {},
        0,
        0,
        core::Street::Flop,
        static_cast<std::uint8_t>(spec.root_actions),
    });
    graph.infosets.push_back(core::HUNLFlatInfoset{
        opponent_infoset,
        1U,
        spec.root_actions * spec.chance_outcomes,
        {},
        1,
        1,
        core::Street::Flop,
        static_cast<std::uint8_t>(spec.opponent_actions),
    });
    graph.infosets.push_back(core::HUNLFlatInfoset{
        reply_infoset,
        1U + spec.root_actions * spec.chance_outcomes,
        spec.root_actions * spec.chance_outcomes * spec.opponent_actions,
        {},
        2,
        0,
        core::Street::Flop,
        static_cast<std::uint8_t>(spec.reply_actions),
    });
    graph.infoset_debug_keys = {"root-p0", "opp-p1", "reply-p0"};

    const std::uint32_t chance_begin = 1;
    const std::uint32_t opponent_begin = chance_begin + spec.root_actions;
    const std::uint32_t reply_begin = opponent_begin + spec.root_actions * spec.chance_outcomes;
    const std::uint32_t terminal_begin = reply_begin + spec.root_actions * spec.chance_outcomes * spec.opponent_actions;
    const std::uint32_t total_nodes =
        terminal_begin + spec.root_actions * spec.chance_outcomes * spec.opponent_actions * spec.reply_actions;

    graph.node_meta.resize(total_nodes);
    graph.node_depths.resize(total_nodes, 0U);
    graph.children.reserve(
        spec.root_actions +
        spec.root_actions * spec.chance_outcomes * spec.opponent_actions +
        spec.root_actions * spec.chance_outcomes * spec.opponent_actions * spec.reply_actions);
    graph.chance_outcomes.reserve(spec.root_actions * spec.chance_outcomes);
    graph.infoset_nodes.reserve(
        1U +
        spec.root_actions * spec.chance_outcomes +
        spec.root_actions * spec.chance_outcomes * spec.opponent_actions);

    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = spec.root_actions;
    graph.node_meta[0].infoset_id = root_infoset;
    graph.node_meta[0].player = 0;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Decision;
    graph.node_meta[0].street = core::Street::Flop;
    graph.node_meta[0].action_count = static_cast<std::uint8_t>(spec.root_actions);
    graph.node_meta[0].has_infoset = true;
    graph.infoset_nodes.push_back(0);

    for (std::uint32_t root_action = 0; root_action < spec.root_actions; ++root_action) {
        const auto chance_node = chance_begin + root_action;
        graph.children.push_back(chance_node);
        graph.node_meta[chance_node].chance_begin = static_cast<std::uint32_t>(graph.chance_outcomes.size());
        graph.node_meta[chance_node].chance_count = spec.chance_outcomes;
        graph.node_meta[chance_node].type = core::HUNLFlatNodeType::Chance;
        graph.node_meta[chance_node].street = core::Street::Flop;
        graph.node_depths[chance_node] = 1;

        for (std::uint32_t outcome = 0; outcome < spec.chance_outcomes; ++outcome) {
            const auto opponent_index = root_action * spec.chance_outcomes + outcome;
            const auto opponent_node = opponent_begin + opponent_index;
            graph.chance_outcomes.push_back(core::HUNLFlatChanceOutcome{
                static_cast<std::uint8_t>(outcome),
                1.0 / static_cast<double>(spec.chance_outcomes),
                opponent_node,
                1U,
            });

            graph.node_meta[opponent_node].child_begin = static_cast<std::uint32_t>(graph.children.size());
            graph.node_meta[opponent_node].child_count = spec.opponent_actions;
            graph.node_meta[opponent_node].infoset_id = opponent_infoset;
            graph.node_meta[opponent_node].player = 1;
            graph.node_meta[opponent_node].type = core::HUNLFlatNodeType::Decision;
            graph.node_meta[opponent_node].street = core::Street::Flop;
            graph.node_meta[opponent_node].action_count = static_cast<std::uint8_t>(spec.opponent_actions);
            graph.node_meta[opponent_node].has_infoset = true;
            graph.node_depths[opponent_node] = 2;
            graph.infoset_nodes.push_back(opponent_node);

            for (std::uint32_t opponent_action = 0; opponent_action < spec.opponent_actions; ++opponent_action) {
                const auto reply_index = opponent_index * spec.opponent_actions + opponent_action;
                const auto reply_node = reply_begin + reply_index;
                graph.children.push_back(reply_node);

                graph.node_meta[reply_node].child_begin = static_cast<std::uint32_t>(graph.children.size());
                graph.node_meta[reply_node].child_count = spec.reply_actions;
                graph.node_meta[reply_node].infoset_id = reply_infoset;
                graph.node_meta[reply_node].player = 0;
                graph.node_meta[reply_node].type = core::HUNLFlatNodeType::Decision;
                graph.node_meta[reply_node].street = core::Street::Flop;
                graph.node_meta[reply_node].action_count = static_cast<std::uint8_t>(spec.reply_actions);
                graph.node_meta[reply_node].has_infoset = true;
                graph.node_depths[reply_node] = 3;
                graph.infoset_nodes.push_back(reply_node);

                for (std::uint32_t reply_action = 0; reply_action < spec.reply_actions; ++reply_action) {
                    const auto terminal_index = reply_index * spec.reply_actions + reply_action;
                    const auto terminal_node = terminal_begin + terminal_index;
                    graph.children.push_back(terminal_node);

                    const auto root_term = static_cast<double>((root_action + 1U) * 6U);
                    const auto outcome_term = outcome == 0U ? 1.5 : -0.75;
                    const auto opponent_term = static_cast<double>(opponent_action) * 1.25;
                    const auto reply_term = static_cast<double>(reply_action) * 2.5;
                    const auto parity_term = ((root_action + opponent_action + reply_action + outcome) % 2U) == 0U
                        ? 0.5
                        : -0.5;
                    const auto utility = root_term - opponent_term + reply_term + outcome_term + parity_term;
                    graph.node_meta[terminal_node] = make_terminal_meta(utility);
                    graph.node_depths[terminal_node] = 4;
                }
            }
        }
    }

    graph.depth_order.reserve(total_nodes);
    graph.forward_order.reserve(total_nodes);
    graph.reverse_order.reserve(total_nodes);
    graph.street_order.reserve(total_nodes);
    for (std::uint32_t node = 0; node < total_nodes; ++node) {
        graph.depth_order.push_back(node);
        graph.forward_order.push_back(node);
        graph.street_order.push_back(node);
    }
    for (std::uint32_t node = total_nodes; node > 0; --node) {
        graph.reverse_order.push_back(node - 1U);
    }

    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, spec.root_actions},
        core::HUNLFlatSlice{opponent_begin, spec.root_actions * spec.chance_outcomes},
        core::HUNLFlatSlice{reply_begin, spec.root_actions * spec.chance_outcomes * spec.opponent_actions},
        core::HUNLFlatSlice{
            terminal_begin,
            spec.root_actions * spec.chance_outcomes * spec.opponent_actions * spec.reply_actions,
        },
    };
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, total_nodes};
    return graph;
}

BenchmarkResult run_benchmark_for_workers(const BenchmarkConfig& config, std::size_t workers) {
    const auto preset = parse_preset(config.preset);
    if (!preset.has_value()) {
        throw std::invalid_argument("Unknown benchmark preset");
    }
    auto graph = make_benchmark_graph(*preset);

    core::HUNLFlatMCCFRConfig solver_config;
    solver_config.mode = config.mode;
    solver_config.seed = config.seed;
    solver_config.traversals_per_iteration = config.traversals_per_iteration;
    solver_config.batch_size = config.batch_size;
    solver_config.update_both_players = config.update_both_players;
    solver_config.use_sparse_storage = config.use_sparse_storage;
    solver_config.use_discounting = config.use_discounting;

    core::HUNLFlatMCCFR solver(
        std::move(graph),
        {1, 1},
        solver_config,
        config.layout,
        workers,
        config.precision);

    const auto start = std::chrono::steady_clock::now();
    solver.run_iterations(config.iterations);
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    const auto strategy_table = solver.export_average_strategy_table();
    const auto terminal_values = core::build_flat_terminal_value_table(solver.graph());
    const auto expected_value =
        core::compute_flat_expected_value(solver.graph(), strategy_table.view(), &terminal_values);

    BenchmarkResult result;
    result.graph_name = preset->name;
    result.node_count = static_cast<std::uint32_t>(solver.graph().node_meta.size());
    result.workers = workers;
    result.total_seconds = elapsed;
    result.seconds_per_iteration = elapsed / static_cast<double>(config.iterations);
    result.iterations_per_second = static_cast<double>(config.iterations) / elapsed;
    result.expected_value_p0 = expected_value[0];
    result.traverse_seconds = solver.profile().traverse_seconds;
    result.merge_seconds = solver.profile().merge_seconds;
    result.total_nodes_visited = solver.total_counters().nodes_visited;
    result.sampled_opponent_actions = solver.total_counters().sampled_opponent_actions;
    result.traversing_action_expansions = solver.total_counters().traversing_player_action_expansions;
    return result;
}

void print_results(const BenchmarkConfig& config, const std::vector<BenchmarkResult>& results) {
    const auto graph_name = results.empty() ? std::string("unknown") : results.front().graph_name;
    const auto node_count = results.empty() ? 0U : results.front().node_count;
    std::cout << "MCCFR scaling benchmark\n";
    std::cout << "  preset=" << config.preset
              << " graph=" << graph_name
              << " nodes=" << node_count
              << " mode=" << sampling_mode_name(config.mode)
              << " iterations=" << config.iterations
              << " traversals=" << config.traversals_per_iteration
              << " batch-size=" << config.batch_size
              << " seed=" << config.seed
              << " update-both-players=" << (config.update_both_players ? "true" : "false")
              << " sparse=" << (config.use_sparse_storage ? "true" : "false")
              << " discounting=" << (config.use_discounting ? "true" : "false")
              << "\n\n";
    std::cout << "  note="
              << (config.preset == "tiny"
                      ? "tiny preset = sanity and determinism"
                      : "scaling preset = throughput study")
              << "\n\n";

    std::cout << std::left
              << std::setw(8) << "workers"
              << std::setw(14) << "total_ms"
              << std::setw(14) << "iter_ms"
              << std::setw(14) << "iters/s"
              << std::setw(12) << "speedup"
              << std::setw(12) << "eff"
              << std::setw(16) << "merge_ms"
              << std::setw(16) << "traverse_ms"
              << std::setw(16) << "ev_p0"
              << std::setw(16) << "nodes"
              << '\n';

    const auto baseline_seconds = results.empty() ? 0.0 : results.front().total_seconds;
    for (const auto& result : results) {
        const auto speedup =
            baseline_seconds > 0.0 ? baseline_seconds / result.total_seconds : 0.0;
        const auto efficiency =
            result.workers > 0 ? speedup / static_cast<double>(result.workers) : 0.0;
        std::cout << std::left
                  << std::setw(8) << result.workers
                  << std::setw(14) << std::fixed << std::setprecision(3) << (result.total_seconds * 1000.0)
                  << std::setw(14) << std::fixed << std::setprecision(3) << (result.seconds_per_iteration * 1000.0)
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.iterations_per_second
                  << std::setw(12) << std::fixed << std::setprecision(2) << speedup
                  << std::setw(12) << std::fixed << std::setprecision(2) << efficiency
                  << std::setw(16) << std::fixed << std::setprecision(3) << (result.merge_seconds * 1000.0)
                  << std::setw(16) << std::fixed << std::setprecision(3) << (result.traverse_seconds * 1000.0)
                  << std::setw(16) << std::fixed << std::setprecision(4) << result.expected_value_p0
                  << std::setw(16) << result.total_nodes_visited
                  << '\n';
    }

    std::cout << "\nCSV\n";
    std::cout << "workers,total_ms,iter_ms,iters_per_second,speedup,efficiency,merge_ms,traverse_ms,ev_p0,nodes_visited,sampled_opponent_actions,traversing_action_expansions\n";
    for (const auto& result : results) {
        const auto speedup =
            baseline_seconds > 0.0 ? baseline_seconds / result.total_seconds : 0.0;
        const auto efficiency =
            result.workers > 0 ? speedup / static_cast<double>(result.workers) : 0.0;
        std::cout << result.workers << ','
                  << std::fixed << std::setprecision(3) << (result.total_seconds * 1000.0) << ','
                  << std::fixed << std::setprecision(3) << (result.seconds_per_iteration * 1000.0) << ','
                  << std::fixed << std::setprecision(6) << result.iterations_per_second << ','
                  << std::fixed << std::setprecision(6) << speedup << ','
                  << std::fixed << std::setprecision(6) << efficiency << ','
                  << std::fixed << std::setprecision(3) << (result.merge_seconds * 1000.0) << ','
                  << std::fixed << std::setprecision(3) << (result.traverse_seconds * 1000.0) << ','
                  << std::fixed << std::setprecision(6) << result.expected_value_p0 << ','
                  << result.total_nodes_visited << ','
                  << result.sampled_opponent_actions << ','
                  << result.traversing_action_expansions
                  << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto config = parse_args(argc, argv);
    if (!config.has_value()) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    std::vector<BenchmarkResult> results;
    results.reserve(config->workers.size());
    for (const auto workers : config->workers) {
        results.push_back(run_benchmark_for_workers(*config, workers));
    }

    print_results(*config, results);
    return EXIT_SUCCESS;
}
