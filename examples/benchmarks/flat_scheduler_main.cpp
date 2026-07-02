#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_dcfr.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t kMemoryWarningBytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMemoryFailBytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;

struct BenchConfig {
    enum class Preset : std::uint8_t {
        None = 0,
        RTAFlopConservative = 1,
        RTAFlopBalanced = 2,
    };

    std::uint32_t iterations = 100;
    std::vector<std::size_t> workers;
    core::HUNLFlatValueLayout layout = core::HUNLFlatValueLayout::InfosetActionHand;
    Preset preset = Preset::None;
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

std::optional<BenchConfig::Preset> preset_from_text(std::string_view text) {
    if (text == "conservative") return BenchConfig::Preset::RTAFlopConservative;
    if (text == "balanced") return BenchConfig::Preset::RTAFlopBalanced;
    return std::nullopt;
}

std::optional<BenchConfig> parse_args(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        return std::nullopt;
    }

    BenchConfig cfg;
    if (!parse_uint32(argv[1], cfg.iterations)) {
        return std::nullopt;
    }
    if (!parse_worker_list(argv[2], cfg.workers)) {
        return std::nullopt;
    }

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "hand-action") {
            cfg.layout = core::HUNLFlatValueLayout::InfosetHandAction;
        } else if (arg == "action-hand") {
            cfg.layout = core::HUNLFlatValueLayout::InfosetActionHand;
        } else if (const auto preset = preset_from_text(arg); preset.has_value()) {
            cfg.preset = *preset;
        } else {
            return std::nullopt;
        }
    }

    return cfg;
}

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " <iterations> <workers> [action-hand|hand-action] [conservative|balanced]\n\n"
              << "Examples:\n"
              << "  " << exe << " 100 1,2,4\n"
              << "  " << exe << " 200 4 action-hand conservative\n";
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
    std::cout << "  memory estimate\n";
    std::cout << "    " << std::setw(18) << std::left << "graph"
              << format_bytes(estimate.graph_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "infoset_table"
              << format_bytes(estimate.infoset_table_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "solver_buffers"
              << format_bytes(estimate.solver_buffers_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "worker_scratch"
              << format_bytes(estimate.worker_scratch_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "parallel_plan"
              << format_bytes(estimate.parallel_plan_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "auxiliary"
              << format_bytes(estimate.auxiliary_bytes) << "\n";
    std::cout << "    " << std::setw(18) << std::left << "total"
              << format_bytes(estimate.total_bytes()) << "\n";
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

std::string layout_name(core::HUNLFlatValueLayout layout) {
    switch (layout) {
        case core::HUNLFlatValueLayout::InfosetHandAction:
            return "hand-action";
        case core::HUNLFlatValueLayout::InfosetActionHand:
            return "action-hand";
    }
    return "unknown";
}

std::string preset_name(BenchConfig::Preset preset) {
    switch (preset) {
        case BenchConfig::Preset::None:
            return "none";
        case BenchConfig::Preset::RTAFlopConservative:
            return "rta-flop-conservative";
        case BenchConfig::Preset::RTAFlopBalanced:
            return "rta-flop-balanced";
    }
    return "unknown";
}

double stage_value(const core::HUNLFlatStageProfile& profile, core::HUNLFlatStageKind stage) {
    switch (stage) {
        case core::HUNLFlatStageKind::Discount:
            return profile.discount_seconds;
        case core::HUNLFlatStageKind::Strategy:
            return profile.strategy_seconds;
        case core::HUNLFlatStageKind::Reach:
            return profile.reach_seconds;
        case core::HUNLFlatStageKind::Terminal:
            return profile.terminal_seconds;
        case core::HUNLFlatStageKind::Backward:
            return profile.backward_seconds;
        case core::HUNLFlatStageKind::Regret:
            return profile.regret_seconds;
        case core::HUNLFlatStageKind::AverageStrategy:
            return profile.average_strategy_seconds;
    }
    return 0.0;
}

const char* stage_name(core::HUNLFlatStageKind stage) {
    switch (stage) {
        case core::HUNLFlatStageKind::Discount:
            return "discount";
        case core::HUNLFlatStageKind::Strategy:
            return "strategy";
        case core::HUNLFlatStageKind::Reach:
            return "reach";
        case core::HUNLFlatStageKind::Terminal:
            return "terminal";
        case core::HUNLFlatStageKind::Backward:
            return "backward";
        case core::HUNLFlatStageKind::Regret:
            return "regret";
        case core::HUNLFlatStageKind::AverageStrategy:
            return "avg-strategy";
    }
    return "unknown";
}

void print_stage_summary(const core::HUNLFlatStageProfile& profile) {
    const std::array<core::HUNLFlatStageKind, 7> stages = {{
        core::HUNLFlatStageKind::Discount,
        core::HUNLFlatStageKind::Strategy,
        core::HUNLFlatStageKind::Reach,
        core::HUNLFlatStageKind::Terminal,
        core::HUNLFlatStageKind::Backward,
        core::HUNLFlatStageKind::Regret,
        core::HUNLFlatStageKind::AverageStrategy,
    }};

    std::cout << "  stage breakdown\n";
    for (const auto stage : stages) {
        std::cout << "    " << std::setw(12) << std::left << stage_name(stage)
                  << format_seconds(stage_value(profile, stage)) << "\n";
    }
}

void print_worker_diagnostics(const core::HUNLFlatSchedulerDiagnostics& diagnostics, std::uint32_t iterations) {
    const std::array<core::HUNLFlatStageKind, 7> stages = {{
        core::HUNLFlatStageKind::Discount,
        core::HUNLFlatStageKind::Strategy,
        core::HUNLFlatStageKind::Reach,
        core::HUNLFlatStageKind::Terminal,
        core::HUNLFlatStageKind::Backward,
        core::HUNLFlatStageKind::Regret,
        core::HUNLFlatStageKind::AverageStrategy,
    }};

    std::cout << "  per-worker stage times\n";
    for (std::size_t worker = 0; worker < diagnostics.worker_profiles.size(); ++worker) {
        const auto& profile = diagnostics.worker_profiles[worker];
        std::cout << "    worker[" << worker << "]";
        for (const auto stage : stages) {
            const double avg_seconds = iterations > 0 ? stage_value(profile, stage) / static_cast<double>(iterations) : 0.0;
            std::cout << " " << stage_name(stage) << "=" << format_seconds(avg_seconds);
        }
        std::cout << "\n";
    }

    std::cout << "  imbalance\n";
    for (const auto stage : stages) {
        double fastest = std::numeric_limits<double>::max();
        double slowest = 0.0;
        for (const auto& worker : diagnostics.worker_profiles) {
            const double avg_seconds =
                iterations > 0 ? stage_value(worker, stage) / static_cast<double>(iterations) : 0.0;
            fastest = std::min(fastest, avg_seconds);
            slowest = std::max(slowest, avg_seconds);
        }
        if (diagnostics.worker_profiles.empty()) {
            fastest = 0.0;
        }
        const double ratio = fastest > 0.0 ? slowest / fastest : 1.0;
        std::cout << "    " << std::setw(12) << std::left << stage_name(stage)
                  << "fastest=" << format_seconds(fastest)
                  << " slowest=" << format_seconds(slowest)
                  << " ratio=" << std::fixed << std::setprecision(3) << ratio << "x\n";
    }
}

void print_expected_backward_costs(const core::HUNLFlatParallelPlan& plan) {
    std::cout << "  expected backward cost by worker/depth\n";
    for (std::size_t worker = 0; worker < plan.workers.size(); ++worker) {
        std::cout << "    worker[" << worker << "]";
        for (std::size_t depth = 0; depth < plan.workers[worker].depth_backward_costs.size(); ++depth) {
            std::cout << " d" << depth << "=" << plan.workers[worker].depth_backward_costs[depth];
        }
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed) {
        print_usage(argv[0]);
        return 1;
    }

    const auto cfg = *parsed;
    const auto base_config =
        cfg.preset == BenchConfig::Preset::RTAFlopConservative ? core::rta_flop_conservative() :
        cfg.preset == BenchConfig::Preset::RTAFlopBalanced ? core::rta_flop_balanced() :
        core::benchmark_turn_subgame();
    const auto config = std::make_shared<const core::HUNLConfig>(base_config);
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto bucket_count = cfg.preset == BenchConfig::Preset::None
        ? 1326U
        : static_cast<std::uint32_t>(core::configured_bucket_count(*config, config->starting_street));
    const std::array<std::size_t, 2> hand_count_per_player = {bucket_count, bucket_count};

    std::cout << "Flat HUNL scheduler benchmark"
              << " layout=" << layout_name(cfg.layout)
              << " preset=" << preset_name(cfg.preset)
              << " iterations=" << cfg.iterations
              << " infosets=" << graph.infosets.size()
              << " nodes=" << graph.node_meta.size() << "\n";

    for (const auto workers : cfg.workers) {
        const auto table = core::HUNLFlatInfosetTable::build(graph, hand_count_per_player, cfg.layout);
        const auto plan = core::HUNLFlatParallelPlan::build(graph, table, workers);
        core::HUNLFlatMemoryEstimateOptions memory_options;
        memory_options.max_child_count = max_backward_row_width(graph);
        memory_options.max_bucket_count = max_bucket_width(table);
        const auto memory = core::estimate_memory(graph, table, workers, memory_options);

        std::cout << "\nworkers=" << workers << "\n";
        print_memory_estimate(memory);
        if (!enforce_memory_guardrails(memory)) {
            return 2;
        }

        core::HUNLFlatDCFR solver(graph, hand_count_per_player, cfg.layout, workers);
        const auto start = std::chrono::steady_clock::now();
        solver.run_iterations(cfg.iterations);
        const auto finish = std::chrono::steady_clock::now();

        std::cout << "  total=" << format_seconds(std::chrono::duration<double>(finish - start).count()) << "\n";
        print_expected_backward_costs(plan);
        print_stage_summary(solver.profile());
        print_worker_diagnostics(solver.scheduler_diagnostics(), cfg.iterations);
    }

    return 0;
}
