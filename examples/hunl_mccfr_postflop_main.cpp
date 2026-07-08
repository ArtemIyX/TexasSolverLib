#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
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
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct AppConfig {
    core::Street street = core::Street::Flop;
    core::HUNLFlatSamplingMode mode = core::HUNLFlatSamplingMode::External;
    core::HUNLFlatStoragePrecision precision = core::HUNLFlatStoragePrecision::Float64;
    std::uint64_t seed = 1;
    std::uint32_t iterations = 200;
    std::uint32_t traversals = 4096;
    std::uint32_t batch_size = 256;
    std::uint32_t time_budget_ms = 0;
    std::size_t workers = 8;
    std::size_t flop_buckets = 64;
    std::size_t turn_buckets = 48;
    std::size_t river_buckets = 32;
    std::size_t range_hands_per_player = 128;
    int starting_stack = 1000;
    bool use_sparse_storage = false;
    bool keep_dense_validation_backend = false;
    bool use_discounting = false;
    bool use_iterative_external_dense_traversal = false;
    bool use_variance_reduction = false;
    bool update_both_players = true;
    double as_epsilon = 0.05;
    double as_tau = 1000.0;
    double as_beta = 1e6;
};

struct RandomScenario {
    core::HUNLConfig config;
    core::HUNLState state;
};

bool parse_u64(std::string_view text, std::uint64_t& out) {
    try {
        out = static_cast<std::uint64_t>(std::stoull(std::string(text)));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_u32(std::string_view text, std::uint32_t& out) {
    try {
        const auto value = std::stoul(std::string(text));
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        out = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_size(std::string_view text, std::size_t& out) {
    try {
        const auto value = std::stoull(std::string(text));
        out = static_cast<std::size_t>(value);
        return out > 0U;
    } catch (...) {
        return false;
    }
}

bool parse_int(std::string_view text, int& out) {
    try {
        out = std::stoi(std::string(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(std::string_view text, double& out) {
    try {
        out = std::stod(std::string(text));
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

std::optional<core::HUNLFlatSamplingMode> mode_from_text(std::string_view text) {
    if (text == "exact") return core::HUNLFlatSamplingMode::Exact;
    if (text == "public-chance") return core::HUNLFlatSamplingMode::PublicChance;
    if (text == "external") return core::HUNLFlatSamplingMode::External;
    if (text == "average-strategy") return core::HUNLFlatSamplingMode::AverageStrategy;
    return std::nullopt;
}

std::optional<core::HUNLFlatStoragePrecision> precision_from_text(std::string_view text) {
    if (text == "double" || text == "float64") return core::HUNLFlatStoragePrecision::Float64;
    if (text == "float" || text == "float32") return core::HUNLFlatStoragePrecision::Float32;
    if (text == "compressed16" || text == "fp16") return core::HUNLFlatStoragePrecision::Compressed16;
    return std::nullopt;
}

const char* mode_name(core::HUNLFlatSamplingMode mode) {
    switch (mode) {
        case core::HUNLFlatSamplingMode::Exact: return "exact";
        case core::HUNLFlatSamplingMode::PublicChance: return "public-chance";
        case core::HUNLFlatSamplingMode::External: return "external";
        case core::HUNLFlatSamplingMode::AverageStrategy: return "average-strategy";
    }
    return "unknown";
}

const char* precision_name(core::HUNLFlatStoragePrecision precision) {
    switch (precision) {
        case core::HUNLFlatStoragePrecision::Float64: return "float64";
        case core::HUNLFlatStoragePrecision::Float32: return "float32";
        case core::HUNLFlatStoragePrecision::Compressed16: return "compressed16";
    }
    return "unknown";
}

std::string cards_to_string(const std::array<std::uint8_t, 2>& cards) {
    return core::card_to_string(cards[0]) + core::card_to_string(cards[1]);
}

std::string board_to_string(const std::vector<std::uint8_t>& board) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < board.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << core::card_to_string(board[i]);
    }
    return oss.str();
}

std::string action_description(core::ActionId action, const core::ActionContext& ctx) {
    switch (action) {
        case core::ACTION_FOLD:
            return "fold";
        case core::ACTION_CHECK:
            return "check";
        case core::ACTION_CALL:
            return "call";
        case core::ACTION_ALL_IN:
            return "all-in";
        default:
            break;
    }

    try {
        if (core::is_opening_bet(action)) {
            return "bet-to-" + std::to_string(core::compute_bet_amount(action, ctx));
        }
        if (core::is_raise(action)) {
            return "raise-to-" + std::to_string(core::compute_raise_to(action, ctx));
        }
    } catch (...) {
    }
    return "action-" + std::to_string(action);
}

void print_usage(const char* exe) {
    std::cerr
        << "Usage:\n"
        << "  " << exe << " [--street flop|turn|river] [--mode exact|public-chance|external|average-strategy]\n"
        << "      [--iterations N] [--traversals N] [--batch-size N] [--time-budget-ms N]\n"
        << "      [--seed N] [--workers N] [--stack N] [--range-hands N]\n"
        << "      [--buckets N] [--flop-buckets N] [--turn-buckets N] [--river-buckets N]\n"
        << "      [--precision float64|float32|compressed16] [--sparse] [--keep-dense-validation]\n"
        << "      [--discounting] [--variance-reduction] [--single-player]\n"
        << "      [--iterative-external-dense] [--as-epsilon X] [--as-tau X] [--as-beta X]\n";
}

std::optional<AppConfig> parse_args(int argc, char* argv[]) {
    AppConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--street" && i + 1 < argc) {
            const auto street = street_from_text(argv[++i]);
            if (!street.has_value()) return std::nullopt;
            cfg.street = *street;
            continue;
        }
        if (arg == "--mode" && i + 1 < argc) {
            const auto mode = mode_from_text(argv[++i]);
            if (!mode.has_value()) return std::nullopt;
            cfg.mode = *mode;
            continue;
        }
        if (arg == "--precision" && i + 1 < argc) {
            const auto precision = precision_from_text(argv[++i]);
            if (!precision.has_value()) return std::nullopt;
            cfg.precision = *precision;
            continue;
        }
        if (arg == "--seed" && i + 1 < argc) {
            if (!parse_u64(argv[++i], cfg.seed)) return std::nullopt;
            continue;
        }
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_u32(argv[++i], cfg.iterations)) return std::nullopt;
            continue;
        }
        if (arg == "--traversals" && i + 1 < argc) {
            if (!parse_u32(argv[++i], cfg.traversals)) return std::nullopt;
            continue;
        }
        if (arg == "--batch-size" && i + 1 < argc) {
            if (!parse_u32(argv[++i], cfg.batch_size)) return std::nullopt;
            continue;
        }
        if (arg == "--time-budget-ms" && i + 1 < argc) {
            if (!parse_u32(argv[++i], cfg.time_budget_ms)) return std::nullopt;
            continue;
        }
        if (arg == "--workers" && i + 1 < argc) {
            if (!parse_size(argv[++i], cfg.workers)) return std::nullopt;
            continue;
        }
        if (arg == "--range-hands" && i + 1 < argc) {
            if (!parse_size(argv[++i], cfg.range_hands_per_player)) return std::nullopt;
            continue;
        }
        if (arg == "--buckets" && i + 1 < argc) {
            std::size_t value = 0;
            if (!parse_size(argv[++i], value)) return std::nullopt;
            cfg.flop_buckets = value;
            cfg.turn_buckets = value;
            cfg.river_buckets = value;
            continue;
        }
        if (arg == "--flop-buckets" && i + 1 < argc) {
            if (!parse_size(argv[++i], cfg.flop_buckets)) return std::nullopt;
            continue;
        }
        if (arg == "--turn-buckets" && i + 1 < argc) {
            if (!parse_size(argv[++i], cfg.turn_buckets)) return std::nullopt;
            continue;
        }
        if (arg == "--river-buckets" && i + 1 < argc) {
            if (!parse_size(argv[++i], cfg.river_buckets)) return std::nullopt;
            continue;
        }
        if (arg == "--stack" && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.starting_stack) || cfg.starting_stack <= 1) return std::nullopt;
            continue;
        }
        if (arg == "--sparse") {
            cfg.use_sparse_storage = true;
            continue;
        }
        if (arg == "--keep-dense-validation") {
            cfg.keep_dense_validation_backend = true;
            continue;
        }
        if (arg == "--discounting") {
            cfg.use_discounting = true;
            continue;
        }
        if (arg == "--variance-reduction") {
            cfg.use_variance_reduction = true;
            continue;
        }
        if (arg == "--single-player") {
            cfg.update_both_players = false;
            continue;
        }
        if (arg == "--iterative-external-dense") {
            cfg.use_iterative_external_dense_traversal = true;
            continue;
        }
        if (arg == "--as-epsilon" && i + 1 < argc) {
            if (!parse_double(argv[++i], cfg.as_epsilon)) return std::nullopt;
            continue;
        }
        if (arg == "--as-tau" && i + 1 < argc) {
            if (!parse_double(argv[++i], cfg.as_tau)) return std::nullopt;
            continue;
        }
        if (arg == "--as-beta" && i + 1 < argc) {
            if (!parse_double(argv[++i], cfg.as_beta)) return std::nullopt;
            continue;
        }
        return std::nullopt;
    }
    return cfg;
}

std::vector<std::array<std::uint8_t, 2>> enumerate_legal_holes(const std::vector<std::uint8_t>& blocked_cards) {
    std::array<bool, 64> blocked = {};
    for (const auto card : blocked_cards) {
        if (card < blocked.size()) {
            blocked[card] = true;
        }
    }

    std::vector<std::array<std::uint8_t, 2>> combos;
    combos.reserve(1326);
    std::vector<std::uint8_t> deck;
    deck.reserve(52);
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            const auto card = core::card_to_int(rank, suit);
            if (!blocked[card]) {
                deck.push_back(card);
            }
        }
    }
    for (std::size_t first = 0; first < deck.size(); ++first) {
        for (std::size_t second = first + 1; second < deck.size(); ++second) {
            combos.push_back({deck[first], deck[second]});
        }
    }
    return combos;
}

core::HUNLRangeInput make_random_range_input(
    std::mt19937_64& rng,
    const std::vector<std::uint8_t>& blocked_cards,
    std::size_t max_hands) {
    auto combos = enumerate_legal_holes(blocked_cards);
    std::shuffle(combos.begin(), combos.end(), rng);
    const auto count = std::min(max_hands, combos.size());

    core::HUNLRangeInput range;
    range.hand_weights.reserve(count);
    std::uniform_real_distribution<double> weight_dist(0.05, 1.0);
    double weight_sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto weight = weight_dist(rng);
        weight_sum += weight;
        range.hand_weights.push_back(core::HUNLWeightedHand{combos[i], weight});
    }

    if (weight_sum > 0.0) {
        for (auto& hand : range.hand_weights) {
            hand.weight /= weight_sum;
        }
    }
    return range;
}

RandomScenario make_random_scenario(const AppConfig& cfg) {
    std::mt19937_64 rng(cfg.seed == 0U ? std::random_device{}() : cfg.seed);
    const auto board_count = cfg.street == core::Street::Flop
        ? std::size_t{3}
        : (cfg.street == core::Street::Turn ? std::size_t{4} : std::size_t{5});

    std::vector<std::uint8_t> deck;
    deck.reserve(52);
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            deck.push_back(core::card_to_int(rank, suit));
        }
    }
    std::shuffle(deck.begin(), deck.end(), rng);

    core::HUNLConfig config;
    config.starting_stack = cfg.starting_stack;
    config.small_blind = 50;
    config.big_blind = 100;
    config.starting_street = cfg.street;
    config.initial_board.assign(deck.begin(), deck.begin() + static_cast<std::ptrdiff_t>(board_count));
    config.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;
    config.range_policy = core::HUNLRangePolicy::UseInitialRanges;
    config.bucket_counts_by_street = {
        static_cast<std::uint16_t>(cfg.flop_buckets),
        static_cast<std::uint16_t>(cfg.turn_buckets),
        static_cast<std::uint16_t>(cfg.river_buckets),
    };

    std::array<std::array<std::uint8_t, 2>, 2> hole = {{
        {deck[board_count + 0U], deck[board_count + 1U]},
        {deck[board_count + 2U], deck[board_count + 3U]},
    }};
    for (auto& hand : hole) {
        if (hand[1] < hand[0]) {
            std::swap(hand[0], hand[1]);
        }
    }
    config.initial_hole_cards = hole;

    std::uniform_int_distribution<int> sb_dist(50, std::max(51, cfg.starting_stack / 3));
    std::uniform_int_distribution<int> bb_dist(100, std::max(101, cfg.starting_stack / 2));
    config.initial_contributions = {
        std::min(sb_dist(rng), cfg.starting_stack - 1),
        std::min(bb_dist(rng), cfg.starting_stack - 1),
    };
    if (config.initial_contributions[0] >= config.initial_contributions[1]) {
        config.initial_contributions[0] = std::max(1, config.initial_contributions[1] - 1);
    }
    config.initial_pot = config.initial_contributions[0] + config.initial_contributions[1];

    for (std::size_t player = 0; player < 2; ++player) {
        auto range = make_random_range_input(rng, config.initial_board, cfg.range_hands_per_player);
        config.initial_ranges[player] = range;
        config.player_ranges[player] = std::move(range);
    }

    auto state = core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(config));
    return RandomScenario{std::move(config), std::move(state)};
}

void print_range_summary(const core::HUNLRangeInput& range, std::size_t player) {
    std::cout << "player" << player << "_range:\n";
    std::cout << "  hands=" << range.hand_weights.size() << "\n";
    const auto preview = std::min<std::size_t>(range.hand_weights.size(), 8U);
    for (std::size_t i = 0; i < preview; ++i) {
        const auto& hand = range.hand_weights[i];
        std::cout << "  " << cards_to_string(hand.hole)
                  << " weight=" << std::fixed << std::setprecision(6) << hand.weight << "\n";
    }
}

void print_config(const AppConfig& cfg, const RandomScenario& scenario) {
    std::cout << "solve_config:\n";
    std::cout << "  mode=" << mode_name(cfg.mode) << "\n";
    std::cout << "  precision=" << precision_name(cfg.precision) << "\n";
    std::cout << "  street=" << core::street_token(cfg.street) << "\n";
    std::cout << "  seed=" << cfg.seed << "\n";
    std::cout << "  iterations=" << cfg.iterations << "\n";
    std::cout << "  traversals_per_iteration=" << cfg.traversals << "\n";
    std::cout << "  batch_size=" << cfg.batch_size << "\n";
    std::cout << "  time_budget_ms=" << cfg.time_budget_ms << "\n";
    std::cout << "  workers=" << cfg.workers << "\n";
    std::cout << "  sparse=" << (cfg.use_sparse_storage ? "true" : "false") << "\n";
    std::cout << "  keep_dense_validation_backend=" << (cfg.keep_dense_validation_backend ? "true" : "false") << "\n";
    std::cout << "  discounting=" << (cfg.use_discounting ? "true" : "false") << "\n";
    std::cout << "  variance_reduction=" << (cfg.use_variance_reduction ? "true" : "false") << "\n";
    std::cout << "  update_both_players=" << (cfg.update_both_players ? "true" : "false") << "\n";
    std::cout << "  iterative_external_dense=" << (cfg.use_iterative_external_dense_traversal ? "true" : "false") << "\n";
    std::cout << "  buckets_by_street=" << cfg.flop_buckets << "," << cfg.turn_buckets << "," << cfg.river_buckets << "\n";
    std::cout << "  range_hands_per_player=" << cfg.range_hands_per_player << "\n";
    std::cout << "game_state:\n";
    std::cout << "  board=" << board_to_string(scenario.state.board) << "\n";
    std::cout << "  hole0=" << cards_to_string((*scenario.config.initial_hole_cards)[0]) << "\n";
    std::cout << "  hole1=" << cards_to_string((*scenario.config.initial_hole_cards)[1]) << "\n";
    std::cout << "  cur_player=" << scenario.state.cur_player << "\n";
    std::cout << "  stacks=" << scenario.state.stacks[0] << "," << scenario.state.stacks[1] << "\n";
    std::cout << "  contributions=" << scenario.state.contributions[0] << "," << scenario.state.contributions[1] << "\n";
    std::cout << "  pot=" << scenario.config.initial_pot << "\n";
    std::cout << "  history=" << scenario.state.format_history() << "\n";
    print_range_summary(*scenario.config.initial_ranges[0], 0);
    print_range_summary(*scenario.config.initial_ranges[1], 1);
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto cfg = parse_args(argc, argv);
    if (!cfg.has_value()) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    try {
        const auto scenario = make_random_scenario(*cfg);
        print_config(*cfg, scenario);

        auto shared = std::make_shared<const core::HUNLConfig>(scenario.config);
        const auto graph = core::HUNLFlatSolveGraph::build(shared);
        const auto bucket_count = core::configured_bucket_count(scenario.config, cfg->street);
        const std::array<std::size_t, 2> buckets = {bucket_count, bucket_count};

        core::HUNLFlatMCCFRConfig solver_config;
        solver_config.mode = cfg->mode;
        solver_config.seed = cfg->seed;
        solver_config.traversals_per_iteration = cfg->traversals;
        solver_config.batch_size = cfg->batch_size;
        solver_config.as_epsilon = cfg->as_epsilon;
        solver_config.as_tau = cfg->as_tau;
        solver_config.as_beta = cfg->as_beta;
        solver_config.update_both_players = cfg->update_both_players;
        solver_config.use_discounting = cfg->use_discounting;
        solver_config.use_sparse_storage = cfg->use_sparse_storage;
        solver_config.keep_dense_validation_backend = cfg->keep_dense_validation_backend;
        solver_config.use_iterative_external_dense_traversal = cfg->use_iterative_external_dense_traversal;
        if (cfg->use_variance_reduction) {
            solver_config.baseline_mode = core::HUNLFlatBaselineMode::MovingAverage;
        }

        core::HUNLFlatMCCFR solver(
            graph,
            buckets,
            solver_config,
            core::HUNLFlatValueLayout::InfosetActionHand,
            cfg->workers,
            cfg->precision);

        const auto start = std::chrono::steady_clock::now();
        if (cfg->time_budget_ms > 0U) {
            const auto timed = solver.solve_for(std::chrono::milliseconds{cfg->time_budget_ms});
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

            std::cout << "solve_result:\n";
            std::cout << "  wall_seconds=" << std::fixed << std::setprecision(6) << elapsed << "\n";
            std::cout << "  iterations_completed=" << timed.iterations_completed << "\n";
            std::cout << "  batches_completed=" << timed.batches_completed << "\n";
            std::cout << "  timed_out=" << (timed.timed_out ? "true" : "false") << "\n";
            std::cout << "root_strategy:\n";
            const auto legal_actions = scenario.state.legal_actions();
            const auto ctx = scenario.state.action_context();
            for (const auto& action : timed.latest_snapshot.strategy.actions) {
                const auto action_index = static_cast<std::size_t>(action.action_index);
                if (action_index >= legal_actions.size()) {
                    continue;
                }
                std::cout << "  action_index=" << action_index
                          << " action_id=" << legal_actions[action_index]
                          << " desc=" << action_description(legal_actions[action_index], ctx)
                          << " prob=" << std::fixed << std::setprecision(6) << action.probability
                          << "\n";
            }
            std::cout << "diagnostics:\n";
            std::cout << "  entropy=" << timed.latest_snapshot.action_entropy << "\n";
            std::cout << "  delta=" << timed.latest_snapshot.action_probability_delta << "\n";
            std::cout << "  sampled_nodes_visited=" << timed.latest_snapshot.sampled_nodes_visited << "\n";
            std::cout << "  unique_infosets_touched=" << timed.latest_snapshot.unique_infosets_touched << "\n";
            std::cout << "  memory_used_bytes=" << timed.latest_snapshot.memory_used_bytes << "\n";
            return EXIT_SUCCESS;
        }

        solver.run_iterations(cfg->iterations);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto root = solver.export_root_average_strategy();
        const auto legal_actions = scenario.state.legal_actions();
        const auto ctx = scenario.state.action_context();

        std::cout << "solve_result:\n";
        std::cout << "  wall_seconds=" << std::fixed << std::setprecision(6) << elapsed << "\n";
        std::cout << "  iterations_completed=" << solver.iterations() << "\n";
        std::cout << "  traversals=" << solver.profile().traversals << "\n";
        std::cout << "  chance_nodes_visited=" << solver.total_counters().chance_nodes_visited << "\n";
        std::cout << "  decision_nodes_visited=" << solver.total_counters().decision_nodes_visited << "\n";
        std::cout << "  merge_seconds=" << solver.profile().merge_seconds << "\n";
        std::cout << "  traverse_seconds=" << solver.profile().traverse_seconds << "\n";
        std::cout << "root_strategy:\n";
        for (const auto& action : root.actions) {
            const auto action_index = static_cast<std::size_t>(action.action_index);
            if (action_index >= legal_actions.size()) {
                continue;
            }
            std::cout << "  action_index=" << action_index
                      << " action_id=" << legal_actions[action_index]
                      << " desc=" << action_description(legal_actions[action_index], ctx)
                      << " prob=" << std::fixed << std::setprecision(6) << action.probability
                      << "\n";
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
