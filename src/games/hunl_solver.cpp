#include "games/hunl_solver.hpp"

#include "solver/exploit.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/solver.hpp"
#include "util/profiling.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace texas::solver::hunl {

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

bool should_use_flat_hunl_backend(
    const HUNLConfig& config,
    std::size_t workers,
    bool force_parallel) {
    if (config.starting_street == Street::River) {
        return false;
    }
    return force_parallel || workers > 1 || config.starting_street == Street::Turn;
}

bool has_range_inputs(const HUNLConfig& config) {
    return config.initial_ranges[0].has_value() ||
           config.initial_ranges[1].has_value();
}

HUNLConfig range_config_for_root(const HUNLStructuredRootRequest& request) {
    auto range_config = request.config;
    if (request.live_root.has_value()) {
        range_config.starting_street = request.live_root->public_state.street;
        range_config.initial_board = request.live_root->public_state.board;
    }
    return range_config;
}

bool same_hole_cards(
    const std::array<std::uint8_t, 2>& left,
    const std::array<std::uint8_t, 2>& right) noexcept {
    return left == right || (left[0] == right[1] && left[1] == right[0]);
}

HUNLFlatSolveMode resolve_flat_solve_mode(const HUNLConfig& config) {
    if (config.flat_solve_mode != HUNLFlatSolveMode::Auto) {
        return config.flat_solve_mode;
    }
    return config.abstraction_path.has_value()
        ? HUNLFlatSolveMode::Bucketed
        : HUNLFlatSolveMode::ExplicitHand;
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

void validate_config(const HUNLConfig& config) {
    if (config.starting_street == Street::Preflop) {
        throw std::invalid_argument("solve_hunl_postflop requires starting_street >= Flop");
    }
    config.validate();
    const auto range_policy = resolve_range_policy(config);
    if (has_range_inputs(config) ||
        range_policy == HUNLRangePolicy::UseInitialRanges ||
        range_policy == HUNLRangePolicy::RequireExplicit) {
        throw std::invalid_argument(
            "solve_hunl_postflop does not implement range/bucket solving; "
            "use an explicit-hand configuration without range fields");
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
    if (config.flat_solve_mode == HUNLFlatSolveMode::Bucketed &&
        !config.abstraction_path.has_value()) {
        throw std::invalid_argument("bucketed flat solve mode requires abstraction_path");
    }
    if (config.depth_limit_plies != 0U) {
        throw std::invalid_argument(
            "solve_hunl_postflop rejects depth_limit_plies until a shared HUNL leaf evaluator is configured");
    }
}

void HUNLLiveRootSnapshot::validate(const HUNLConfig& config) const {
    if (state_version.empty()) {
        throw std::invalid_argument("HUNLLiveRootSnapshot requires a state version");
    }
    if (public_state.hole_cards.has_value()) {
        throw std::invalid_argument("HUNLLiveRootSnapshot must not contain private hole cards");
    }
    if (public_state.street < Street::Flop || public_state.street > Street::River ||
        public_state.board.size() != expected_board_len(public_state.street) ||
        !are_valid_and_distinct_cards(public_state.board.data(), public_state.board.size())) {
        throw std::invalid_argument("HUNLLiveRootSnapshot has an invalid public board state");
    }
    if (public_state.cur_player < 0 || public_state.cur_player > 1 ||
        public_state.pending_board_deals != 0U ||
        public_state.folded[0] || public_state.folded[1] ||
        public_state.all_in[0] || public_state.all_in[1]) {
        throw std::invalid_argument("HUNLLiveRootSnapshot must be an actionable non-terminal state");
    }
    for (std::size_t player = 0; player < 2U; ++player) {
        if (public_state.contributions[player] < 0 || public_state.stacks[player] < 0 ||
            static_cast<std::int64_t>(public_state.contributions[player]) +
                    public_state.stacks[player] != config.starting_stack) {
            throw std::invalid_argument("HUNLLiveRootSnapshot has inconsistent stacks or contributions");
        }
    }
    const auto contribution_delta =
        static_cast<std::int64_t>(public_state.contributions[0]) - public_state.contributions[1];
    const auto expected_to_call = std::abs(contribution_delta);
    if (public_state.to_call < 0 || static_cast<std::int64_t>(public_state.to_call) != expected_to_call) {
        throw std::invalid_argument("HUNLLiveRootSnapshot has an inconsistent pending call");
    }
    if (public_state.to_call > 0) {
        const auto aggressor = public_state.contributions[0] > public_state.contributions[1] ? 0 : 1;
        const auto responder = 1 - aggressor;
        if (public_state.street_aggressor != aggressor ||
            public_state.cur_player != responder ||
            public_state.street_num_raises == 0U) {
            throw std::invalid_argument("HUNLLiveRootSnapshot has inconsistent raise rights");
        }
    } else if (public_state.street_aggressor != -1 || public_state.street_num_raises != 0U) {
        throw std::invalid_argument("HUNLLiveRootSnapshot has stale raise rights");
    }
    if (canonical_public_history.empty() || legal_actions.empty()) {
        throw std::invalid_argument("HUNLLiveRootSnapshot requires history and typed legal actions");
    }

    auto bound = bind_config(std::make_shared<const HUNLConfig>(config));
    if (bound.format_history() != canonical_public_history || bound.legal_actions() != legal_actions) {
        throw std::invalid_argument("HUNLLiveRootSnapshot history or legal action menu does not match state");
    }
}

HUNLState HUNLLiveRootSnapshot::bind_config(std::shared_ptr<const HUNLConfig> config) const {
    if (!config) throw std::invalid_argument("HUNLLiveRootSnapshot requires a non-null configuration");
    auto bound = public_state;
    bound.hole_cards.reset();
    bound.config = std::move(config);
    return bound;
}

void HUNLStructuredRootRequest::validate() const {
    config.validate();
    if (value_units != HUNLLeafValueUnits::Chips &&
        value_units != HUNLLeafValueUnits::BigBlinds) {
        throw std::invalid_argument(
            "HUNLStructuredRootRequest supports chip or big-blind values only");
    }
    const auto policy = resolve_range_policy(config);
    if (policy != HUNLRangePolicy::UseInitialRanges && policy != HUNLRangePolicy::RequireExplicit) {
        throw std::invalid_argument("HUNLStructuredRootRequest requires explicit initial ranges");
    }
    if (blueprint_version.empty()) {
        throw std::invalid_argument("HUNLStructuredRootRequest requires a blueprint version");
    }
    if (config.depth_limit_plies != 0U && model_version.empty()) {
        throw std::invalid_argument(
            "HUNLStructuredRootRequest requires a leaf model version with a depth limit");
    }
    if (!live_root.has_value() && config.initial_contributions[0] != config.initial_contributions[1]) {
        throw std::invalid_argument(
            "HUNLStructuredRootRequest rejects unequal contributions until a full live betting snapshot is available");
    }
    if (live_root.has_value()) live_root->validate(config);
    validate_hunl_joint_range_feasibility(range_config_for_root(*this));
    const auto selection = effective_hero_selection();
    if (selection.player < 0 || selection.player > 1 || selection.bucket != 0U ||
        !is_valid_card(selection.hole_cards[0]) || !is_valid_card(selection.hole_cards[1]) ||
        selection.hole_cards[0] == selection.hole_cards[1]) {
        throw std::invalid_argument("HUNLStructuredRootRequest has an invalid selected hero hand or bucket");
    }
    // Legacy balanced range roots export player zero's selected hand. Their
    // public state intentionally omits private cards and therefore has no
    // actor until the sampled session attaches a compatible deal.
    const auto root_player = live_root.has_value()
        ? live_root->bind_config(std::make_shared<const HUNLConfig>(config)).current_player()
        : 0;
    if (selection.player != root_player) {
        throw std::invalid_argument("HUNLStructuredRootRequest selected hero is not the root actor");
    }
    const auto deals = normalize_hunl_joint_range(range_config_for_root(*this));
    const auto selected = static_cast<std::size_t>(selection.player);
    if (std::none_of(deals.begin(), deals.end(), [&selection, selected](const HUNLJointRangeDeal& deal) {
            return same_hole_cards(deal.hole[selected], selection.hole_cards);
        })) {
        throw std::invalid_argument("HUNLStructuredRootRequest selected hero hand is not in the compatible joint range");
    }
}

HUNLStructuredRootRequest::HeroSelection HUNLStructuredRootRequest::effective_hero_selection() const {
    if (hero_selection.has_value()) return *hero_selection;
    // Compatibility is limited to an unambiguous one-combo player-zero range.
    // Any range with multiple hero hands must name the acting hand explicitly.
    const auto& range = config.initial_ranges[0];
    if (!range.has_value() || range->hand_weights.size() != 1U) {
        throw std::invalid_argument(
            "HUNLStructuredRootRequest requires an explicit selected hero hand and bucket for a multi-hand range");
    }
    return {0, range->hand_weights.front().hole, 0U};
}

std::vector<HUNLJointRangeDeal> HUNLStructuredRootRequest::normalized_joint_range() const {
    validate();
    return normalize_hunl_joint_range(range_config_for_root(*this));
}

HUNLState HUNLStructuredRootRequest::public_root_state(
    std::shared_ptr<const HUNLConfig> config) const {
    validate();
    if (live_root.has_value()) return live_root->bind_config(std::move(config));
    return HUNLState::initial(std::move(config));
}

void validate_structured_root_request(const HUNLStructuredRootRequest& request) {
    request.validate();
}

HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers,
    std::size_t frontier_multiplier,
    bool force_parallel,
    HUNLBackendSelection backend) {
    validate_config(config);

    const auto start = std::chrono::steady_clock::now();
    SolveOutput solve_output;
    const auto selection = backend;
    const bool use_flat_backend =
        selection == HUNLBackendSelection::Flat ||
        (selection == HUNLBackendSelection::Auto &&
         should_use_flat_hunl_backend(config, workers, force_parallel));

    if (use_flat_backend) {
        auto shared = std::make_shared<const HUNLConfig>(config);
        const auto graph = HUNLFlatSolveGraph::build(shared);
        const auto flat_solve_mode = resolve_flat_solve_mode(config);
        const auto configured_buckets = configured_bucket_count(config, config.starting_street);
        std::array<std::size_t, 2> bucket_count_per_player = {configured_buckets, configured_buckets};
        if (config.initial_hole_cards.has_value()) {
            bucket_count_per_player = {1, 1};
        }

        HUNLFlatDCFR solver(
            std::move(graph),
            bucket_count_per_player,
            flat_solve_mode,
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
        const auto value = texas::solver::detail::expected_value(state, strategy);
        solve_output.game_value = value[0];
        solve_output.exploitability = texas::solver::detail::exploitability<HUNLState>(strategy);

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
             texas::solver::detail::should_use_parallel_solver(workers, frontier_multiplier, texas::solver::detail::estimated_root_branch_count(root)));

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
    std::unordered_map<InfosetKey, std::vector<Probability>> normalized_strategy;
    normalized_strategy.reserve(out.average_strategy.size());
    for (const auto& [key, probabilities] : out.average_strategy) {
        normalized_strategy.emplace(key, probabilities);
    }
    // Compute the named metric once at the wrapper boundary so recursive and
    // flat backends cannot report different scaling or constant-sum offsets.
    out.exploitability = texas::solver::detail::exploitability<HUNLState>(normalized_strategy);
    out.total_nash_conv = out.exploitability * 2.0;
    out.quality_metric = HUNLQualityMetric::PerPlayerExploitability;
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

}  // namespace texas::solver::hunl
