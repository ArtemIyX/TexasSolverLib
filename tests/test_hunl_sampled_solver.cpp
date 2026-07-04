#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_builder.hpp"
#include "solver/hunl_sampled_solver.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_simd.hpp"
#include "solver/hunl_sampled_storage.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_terminal.hpp"
#include "solver/hunl_sampled_traversal.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace {

constexpr double TOL = 1e-6;

core::HUNLState make_lazy_root_state() {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::Flop;
    config->initial_board = {
        core::card_to_int(14, 0),
        core::card_to_int(13, 1),
        core::card_to_int(2, 2),
    };
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(12, 0), core::card_to_int(11, 1)},
        {core::card_to_int(10, 2), core::card_to_int(9, 3)},
    }};
    auto state = core::HUNLState::initial(config);
    state.street = core::Street::Turn;
    state.cur_player = -1;
    state.pending_board_deals = 1;
    state.current_street_tokens.clear();
    state.current_street_history_codes.clear();
    return state;
}

TEST_CASE(hunl_sampled_config_defaults_validate) {
    const core::HUNLSampledSolverConfig config;
    const auto validation = core::validate_sampled_config(config);

    EXPECT_TRUE(validation.ok);
    EXPECT_EQ(config.mode, core::HUNLFlatSamplingMode::External);
    EXPECT_EQ(config.precision, core::HUNLFlatStoragePrecision::Float32);
    EXPECT_EQ(config.layout, core::HUNLFlatValueLayout::InfosetActionHand);
    EXPECT_TRUE(config.lazy_public_expansion);
    EXPECT_TRUE(config.sparse_infosets);
    EXPECT_TRUE(config.enable_memory_guardrails);
    EXPECT_TRUE(config.memory_warning_bytes < config.memory_fail_bytes);
}

TEST_CASE(hunl_flat_mccfr_config_defaults_match_external_sampling_baseline) {
    const core::HUNLFlatMCCFRConfig config;

    EXPECT_EQ(config.mode, core::HUNLFlatSamplingMode::External);
    EXPECT_EQ(config.seed, 1U);
    EXPECT_EQ(config.traversals_per_iteration, 1024U);
    EXPECT_EQ(config.batch_size, 64U);
    EXPECT_TRUE(config.update_both_players);
    EXPECT_TRUE(!config.use_discounting);
    EXPECT_NEAR(config.dcfr_alpha, 1.5, TOL);
    EXPECT_NEAR(config.dcfr_beta, 0.0, TOL);
    EXPECT_NEAR(config.dcfr_gamma, 2.0, TOL);
    EXPECT_TRUE(!config.use_sparse_storage);
    EXPECT_TRUE(!config.keep_dense_validation_backend);
    EXPECT_EQ(config.baseline_mode, core::HUNLFlatBaselineMode::None);
}

TEST_CASE(hunl_sampled_storage_allocates_one_sparse_row) {
    core::HUNLSampledStorage storage;
    const auto row = storage.ensure_row({
        core::InfosetId{7},
        1,
        core::Street::Turn,
        3,
        2,
    });

    EXPECT_EQ(storage.row_count(), 1U);
    EXPECT_EQ(storage.total_value_count(), 6U);
    EXPECT_TRUE(!row.empty());
    EXPECT_EQ(row.bucket_count, 3U);
    EXPECT_EQ(row.action_count, 2U);
    EXPECT_EQ(row.regret[0], 0.0f);
    EXPECT_EQ(row.strategy_sum[5], 0.0f);
}

TEST_CASE(hunl_sampled_storage_computes_current_strategy_on_demand_and_estimates_memory) {
    core::HUNLSampledStorage storage(core::HUNLFlatValueLayout::InfosetHandAction);
    const auto row = storage.ensure_row({
        core::InfosetId{3},
        0,
        core::Street::Flop,
        2,
        2,
    });

    row.regret[0] = 3.0f;
    row.regret[1] = 1.0f;
    row.regret[2] = -2.0f;
    row.regret[3] = -4.0f;

    std::array<float, 2> strategy = {0.0f, 0.0f};
    core::HUNLSampledStorage::compute_current_strategy(storage.view(core::InfosetId{3}), 0, strategy.data());
    EXPECT_NEAR(strategy[0], 0.75, TOL);
    EXPECT_NEAR(strategy[1], 0.25, TOL);

    core::HUNLSampledStorage::compute_current_strategy(storage.view(core::InfosetId{3}), 1, strategy.data());
    EXPECT_NEAR(strategy[0], 0.5, TOL);
    EXPECT_NEAR(strategy[1], 0.5, TOL);

    const auto estimate = storage.memory_estimate();
    EXPECT_EQ(estimate.sparse_rows, 1U);
    EXPECT_EQ(estimate.sparse_values, 4U);
    EXPECT_TRUE(estimate.total_bytes() >= storage.storage_bytes());
}

TEST_CASE(hunl_sampled_config_rejects_inverted_memory_thresholds) {
    core::HUNLSampledSolverConfig config;
    config.memory_warning_bytes = 1024U;
    config.memory_fail_bytes = 512U;

    const auto validation = core::validate_sampled_config(config);
    EXPECT_TRUE(!validation.ok);
}

TEST_CASE(hunl_sampled_config_rejects_average_strategy_flag_without_average_strategy_mode) {
    core::HUNLSampledSolverConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.use_average_strategy_sampling = true;

    const auto validation = core::validate_sampled_config(config);
    EXPECT_TRUE(!validation.ok);
}

TEST_CASE(hunl_sampled_storage_missing_rows_export_uniform_and_clear_resets_counts) {
    core::HUNLSampledStorage storage;

    EXPECT_TRUE(!storage.has_row(core::InfosetId{99}));
    EXPECT_TRUE(storage.view(core::InfosetId{99}).empty());
    EXPECT_TRUE(storage.view_mut(core::InfosetId{99}).empty());
    EXPECT_TRUE(storage.meta_for(core::InfosetId{99}) == nullptr);
    EXPECT_TRUE(storage.meta_for_mut(core::InfosetId{99}) == nullptr);

    std::array<float, 3> strategy = {0.0f, 0.0f, 0.0f};
    core::HUNLSampledStorage::compute_current_strategy(storage.view(core::InfosetId{99}), 0, strategy.data());
    EXPECT_EQ(strategy[0], 0.0f);
    EXPECT_EQ(strategy[1], 0.0f);
    EXPECT_EQ(strategy[2], 0.0f);

    auto row = storage.ensure_row({
        core::InfosetId{5},
        0,
        core::Street::Flop,
        2,
        3,
    });
    core::HUNLSampledStorage::compute_current_strategy(storage.view(core::InfosetId{5}), 9, strategy.data());
    EXPECT_NEAR(strategy[0], 1.0 / 3.0, TOL);
    EXPECT_NEAR(strategy[1], 1.0 / 3.0, TOL);
    EXPECT_NEAR(strategy[2], 1.0 / 3.0, TOL);

    EXPECT_TRUE(!row.empty());
    EXPECT_EQ(storage.row_count(), 1U);
    EXPECT_EQ(storage.total_value_count(), 6U);

    storage.clear_keep_capacity();
    EXPECT_EQ(storage.row_count(), 0U);
    EXPECT_EQ(storage.total_value_count(), 0U);
    EXPECT_TRUE(storage.view(core::InfosetId{5}).empty());
}

TEST_CASE(hunl_sampled_builder_starts_with_root_only_and_grows_lazily) {
    core::HUNLSampledBuilder builder;
    const auto root_state = make_lazy_root_state();
    const auto root_id = builder.initialize(root_state);

    EXPECT_EQ(root_id, 0U);
    EXPECT_EQ(builder.node_count(), 1U);
    EXPECT_EQ(builder.edge_count(), 0U);

    const auto before = builder.memory_estimate();
    builder.ensure_expanded(root_id);
    const auto after = builder.memory_estimate();

    EXPECT_TRUE(builder.node_count() > 1U);
    EXPECT_TRUE(builder.edge_count() > 0U);
    EXPECT_TRUE(after.total_bytes() >= before.total_bytes());
    EXPECT_TRUE(builder.node(root_id).expanded);
}

TEST_CASE(hunl_sampled_builder_caches_nodes_by_public_state_key) {
    core::HUNLSampledBuilder builder;
    const auto root_state = make_lazy_root_state();
    const auto root_id = builder.initialize(root_state);

    builder.ensure_expanded(root_id);
    const auto first_nodes = builder.node_count();
    const auto first_edges = builder.edge_count();
    builder.ensure_expanded(root_id);

    EXPECT_EQ(builder.node_count(), first_nodes);
    EXPECT_EQ(builder.edge_count(), first_edges);
}

TEST_CASE(hunl_sampled_builder_public_chance_isomorphism_collapses_turn_outcomes) {
    core::HUNLSampledBuilder collapsed_builder({true});
    core::HUNLSampledBuilder raw_builder({false});
    const auto root_state = make_lazy_root_state();

    const auto collapsed_root = collapsed_builder.initialize(root_state);
    const auto raw_root = raw_builder.initialize(root_state);
    const auto raw_outcomes = root_state.chance_outcomes().size();

    collapsed_builder.ensure_expanded(collapsed_root);
    raw_builder.ensure_expanded(raw_root);

    EXPECT_TRUE(raw_outcomes > 0U);
    EXPECT_EQ(raw_builder.node(raw_root).edge_count, raw_outcomes);
    EXPECT_TRUE(collapsed_builder.node(collapsed_root).edge_count <= raw_outcomes);
    EXPECT_EQ(
        collapsed_builder.node(collapsed_root).chance_isomorphic,
        collapsed_builder.node(collapsed_root).edge_count < raw_outcomes);
}

TEST_CASE(hunl_sampled_solver_memory_estimate_includes_lazy_graph_cache) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_state = make_lazy_root_state();

    const auto empty_memory = solver.memory_estimate();
    solver.run_batches(request, 1);
    const auto initialized_memory = solver.memory_estimate();
    solver.builder().ensure_expanded(solver.builder().root_id());
    const auto expanded_memory = solver.memory_estimate();

    EXPECT_EQ(empty_memory.public_states_cached, 0U);
    EXPECT_EQ(initialized_memory.public_states_cached, 1U);
    EXPECT_TRUE(expanded_memory.public_states_cached > initialized_memory.public_states_cached);
    EXPECT_TRUE(expanded_memory.public_state_cache_bytes >= initialized_memory.public_state_cache_bytes);
}

TEST_CASE(hunl_sampled_solver_preflight_warns_above_warning_threshold) {
    core::HUNLSampledSolverConfig config;
    config.memory_warning_bytes = 1U;
    config.memory_fail_bytes = 1024ULL * 1024ULL * 1024ULL;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 2;

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Warning);
    EXPECT_TRUE(preflight.estimate.total_bytes() > config.memory_warning_bytes);
}

TEST_CASE(hunl_sampled_solver_preflight_rejects_unsafe_dense_nonlazy_production_mode) {
    core::HUNLSampledSolverConfig config;
    config.lazy_public_expansion = false;
    config.sparse_infosets = false;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 2;

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Rejected);
}

TEST_CASE(hunl_sampled_solver_preflight_adapts_before_rejecting_when_allowed) {
    core::HUNLSampledSolverConfig config;
    config.use_average_strategy_sampling = true;
    config.mode = core::HUNLFlatSamplingMode::AverageStrategy;
    config.bucket_count_hint = 512;
    config.workers = 4;
    config.minibatch_size = 1024;
    config.traversals_per_iteration = 4096;
    config.memory_warning_bytes = 8ULL * 1024ULL * 1024ULL;
    config.memory_fail_bytes = 20ULL * 1024ULL * 1024ULL;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 2;

    const auto preflight = solver.preflight(request);
    EXPECT_TRUE(preflight.status == core::HUNLSampledMemoryStatus::Ok ||
                preflight.status == core::HUNLSampledMemoryStatus::Warning);
    EXPECT_TRUE(preflight.adjustments.reduced_minibatch || preflight.adjustments.reduced_traversals);
    EXPECT_TRUE(preflight.effective_config.minibatch_size <= config.minibatch_size);
    EXPECT_TRUE(preflight.estimate.total_bytes() <= preflight.effective_config.memory_fail_bytes);
}

TEST_CASE(hunl_sampled_solver_preflight_without_guardrails_stays_ok_under_tight_thresholds) {
    core::HUNLSampledSolverConfig config;
    config.enable_memory_guardrails = false;
    config.memory_warning_bytes = 1U;
    config.memory_fail_bytes = 2U;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 3;

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Ok);
    EXPECT_TRUE(preflight.estimate.total_bytes() > config.memory_fail_bytes);
}

TEST_CASE(hunl_sampled_solver_preflight_rejects_when_adaptive_fallback_is_disabled) {
    core::HUNLSampledSolverConfig config;
    config.adaptive_memory_fallback = false;
    config.bucket_count_hint = 2048;
    config.workers = 8;
    config.minibatch_size = 1024;
    config.traversals_per_iteration = 8192;
    config.memory_warning_bytes = 8ULL * 1024ULL * 1024ULL;
    config.memory_fail_bytes = 16ULL * 1024ULL * 1024ULL;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 4;

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Rejected);
    EXPECT_TRUE(!preflight.adjustments.reduced_minibatch);
    EXPECT_TRUE(!preflight.adjustments.reduced_traversals);
}

TEST_CASE(hunl_sampled_solver_run_batches_records_live_memory_budget_categories) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_state = make_lazy_root_state();

    const auto result = solver.run_batches(request, 1);
    EXPECT_TRUE(result.profile.public_states_cached >= 1U);
    EXPECT_TRUE(result.profile.worker_delta_bytes > 0U);
    EXPECT_TRUE(result.profile.export_bytes <= result.profile.total_memory_bytes);
    EXPECT_TRUE(result.profile.total_memory_bytes >= result.profile.worker_delta_bytes);
    EXPECT_TRUE(!result.profile.memory_rejected);
}

TEST_CASE(hunl_sampled_solver_solve_for_zero_budget_returns_uniform_root_without_work) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 3;

    const auto result = solver.solve_for(request, std::chrono::milliseconds{0});
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_TRUE(!result.timed_out);
    EXPECT_EQ(result.root_strategy.actions.size(), 3U);
    EXPECT_NEAR(result.root_strategy.actions[0].probability, 1.0 / 3.0, TOL);
    EXPECT_NEAR(result.root_strategy.actions[1].probability, 1.0 / 3.0, TOL);
    EXPECT_NEAR(result.root_strategy.actions[2].probability, 1.0 / 3.0, TOL);
    EXPECT_EQ(result.profile.traversals, 0U);
}

TEST_CASE(hunl_sampled_solver_run_batches_throws_when_preflight_rejects) {
    core::HUNLSampledSolverConfig config;
    config.adaptive_memory_fallback = false;
    config.bucket_count_hint = 4096;
    config.workers = 8;
    config.minibatch_size = 2048;
    config.traversals_per_iteration = 8192;
    config.memory_warning_bytes = 8ULL * 1024ULL * 1024ULL;
    config.memory_fail_bytes = 16ULL * 1024ULL * 1024ULL;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 4;

    EXPECT_THROW(solver.run_batches(request, 1), std::runtime_error);
}

TEST_CASE(hunl_sampled_traversal_expands_only_the_selected_deeper_path) {
    core::HUNLSampledBuilder builder;
    const auto root_id = builder.initialize(make_lazy_root_state());
    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;

    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.trajectory_id = 0;
    request.traversing_player = 0;
    request.iteration = 1;

    const auto result = traversal.run(request, scratch);

    EXPECT_TRUE(result.nodes_visited >= 2U);
    const auto& root = builder.node(root_id);
    EXPECT_TRUE(root.expanded);
    EXPECT_TRUE(root.edge_count > 1U);

    std::size_t expanded_children = 0;
    for (std::uint32_t edge_index = 0; edge_index < root.edge_count; ++edge_index) {
        const auto& child = builder.node(builder.edge(root.edge_begin + edge_index).child);
        if (child.expanded) {
            ++expanded_children;
        }
    }
    EXPECT_EQ(expanded_children, 1U);
}

TEST_CASE(hunl_sampled_exporter_normalizes_sparse_rows_for_both_layouts) {
    core::HUNLSampledStorage action_major(core::HUNLFlatValueLayout::InfosetActionHand);
    auto action_row = action_major.ensure_row({
        core::InfosetId{1},
        0,
        core::Street::Turn,
        2,
        2,
    });
    action_row.strategy_sum[0] = 3.0f;
    action_row.strategy_sum[1] = 1.0f;
    action_row.strategy_sum[2] = 1.0f;
    action_row.strategy_sum[3] = 3.0f;

    const auto action_exported =
        core::HUNLSampledStrategyExporter::export_average_strategy(action_major.view(core::InfosetId{1}), 1);
    EXPECT_EQ(action_exported.actions.size(), 2U);
    EXPECT_NEAR(action_exported.actions[0].probability, 0.25, TOL);
    EXPECT_NEAR(action_exported.actions[1].probability, 0.75, TOL);

    core::HUNLSampledStorage bucket_major(core::HUNLFlatValueLayout::InfosetHandAction);
    auto bucket_row = bucket_major.ensure_row({
        core::InfosetId{2},
        0,
        core::Street::Turn,
        2,
        2,
    });
    bucket_row.strategy_sum[0] = 2.0f;
    bucket_row.strategy_sum[1] = 6.0f;
    bucket_row.strategy_sum[2] = 6.0f;
    bucket_row.strategy_sum[3] = 2.0f;

    const auto bucket_exported =
        core::HUNLSampledStrategyExporter::export_average_strategy(bucket_major.view(core::InfosetId{2}), 0);
    EXPECT_EQ(bucket_exported.actions.size(), 2U);
    EXPECT_NEAR(bucket_exported.actions[0].probability, 0.25, TOL);
    EXPECT_NEAR(bucket_exported.actions[1].probability, 0.75, TOL);
}

TEST_CASE(hunl_sampled_exporter_uniform_and_zero_sum_rows_stay_normalized) {
    const auto uniform = core::HUNLSampledStrategyExporter::export_uniform(4);
    EXPECT_EQ(uniform.actions.size(), 4U);
    for (const auto& action : uniform.actions) {
        EXPECT_NEAR(action.probability, 0.25, TOL);
    }

    core::HUNLSampledStorage storage(core::HUNLFlatValueLayout::InfosetActionHand);
    storage.ensure_row({
        core::InfosetId{7},
        0,
        core::Street::River,
        2,
        3,
    });
    const auto exported =
        core::HUNLSampledStrategyExporter::export_average_strategy(storage.view(core::InfosetId{7}), 0);
    EXPECT_EQ(exported.actions.size(), 3U);
    EXPECT_NEAR(exported.actions[0].probability, 1.0 / 3.0, TOL);
    EXPECT_NEAR(exported.actions[1].probability, 1.0 / 3.0, TOL);
    EXPECT_NEAR(exported.actions[2].probability, 1.0 / 3.0, TOL);
    EXPECT_TRUE(core::HUNLSampledStrategyExporter::export_average_strategy(
                    storage.view(core::InfosetId{7}),
                    9)
                    .actions.empty());
}

TEST_CASE(hunl_sampled_scheduler_partitions_trajectories_deterministically) {
    const auto first = core::HUNLSampledScheduler::partition_deterministic(10, 3);
    const auto second = core::HUNLSampledScheduler::partition_deterministic(10, 3);

    EXPECT_EQ(first.size(), 3U);
    EXPECT_EQ(first[0].trajectories.begin, 0U);
    EXPECT_EQ(first[0].trajectories.end, 4U);
    EXPECT_EQ(first[1].trajectories.begin, 4U);
    EXPECT_EQ(first[1].trajectories.end, 7U);
    EXPECT_EQ(first[2].trajectories.begin, 7U);
    EXPECT_EQ(first[2].trajectories.end, 10U);

    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].worker_index, second[i].worker_index);
        EXPECT_EQ(first[i].trajectories.begin, second[i].trajectories.begin);
        EXPECT_EQ(first[i].trajectories.end, second[i].trajectories.end);
    }
}

TEST_CASE(hunl_sampled_scheduler_handles_zero_trajectories_and_zero_workers) {
    const auto batches = core::HUNLSampledScheduler::partition_deterministic(0, 0);

    EXPECT_EQ(batches.size(), 1U);
    EXPECT_EQ(batches[0].worker_index, 0U);
    EXPECT_EQ(batches[0].trajectories.begin, 0U);
    EXPECT_EQ(batches[0].trajectories.end, 0U);
}

TEST_CASE(hunl_sampled_simd_scalar_reference_kernels_match_hand_computed_rows) {
    const std::array<float, 6> regret = {1.0f, -2.0f, 3.0f, 3.0f, 2.0f, -1.0f};
    std::array<float, 6> strategy = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::regret_matching_action_major_f32(regret.data(), 2, 3, strategy.data());

    EXPECT_NEAR(strategy[0], 0.25, TOL);
    EXPECT_NEAR(strategy[3], 0.75, TOL);
    EXPECT_NEAR(strategy[1], 0.0, TOL);
    EXPECT_NEAR(strategy[4], 1.0, TOL);
    EXPECT_NEAR(strategy[2], 1.0, TOL);
    EXPECT_NEAR(strategy[5], 0.0, TOL);

    const std::array<float, 3> reach = {2.0f, 4.0f, 1.0f};
    std::array<float, 6> strategy_sum = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::accumulate_average_strategy_action_major_f32(
        strategy.data(),
        reach.data(),
        2,
        3,
        0.5f,
        strategy_sum.data());

    EXPECT_NEAR(strategy_sum[0], 0.25, TOL);
    EXPECT_NEAR(strategy_sum[3], 0.75, TOL);
    EXPECT_NEAR(strategy_sum[1], 0.0, TOL);
    EXPECT_NEAR(strategy_sum[4], 2.0, TOL);
    EXPECT_NEAR(strategy_sum[2], 0.5, TOL);
    EXPECT_NEAR(strategy_sum[5], 0.0, TOL);

    const std::array<float, 6> action_values = {2.0f, 5.0f, 4.0f, 6.0f, 1.0f, 3.0f};
    const std::array<float, 3> node_values = {4.0f, 3.0f, 2.0f};
    const std::array<float, 3> cf_reach = {1.0f, 0.5f, 2.0f};
    std::array<float, 6> regret_delta = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    core::add_regret_delta_action_major_f32(
        action_values.data(),
        node_values.data(),
        cf_reach.data(),
        2,
        3,
        regret_delta.data());

    EXPECT_NEAR(regret_delta[0], -2.0, TOL);
    EXPECT_NEAR(regret_delta[1], 1.0, TOL);
    EXPECT_NEAR(regret_delta[2], 4.0, TOL);
    EXPECT_NEAR(regret_delta[3], 2.0, TOL);
    EXPECT_NEAR(regret_delta[4], -1.0, TOL);
    EXPECT_NEAR(regret_delta[5], 2.0, TOL);

    const auto weighted = core::weighted_sum_f32_f64_accum(
        static_cast<std::uint32_t>(action_values.size()),
        action_values.data(),
        action_values.data());
    EXPECT_NEAR(weighted, 91.0, TOL);
}

TEST_CASE(hunl_sampled_simd_double_kernels_and_runtime_disable_match_scalar_reference) {
    const std::array<double, 6> regret = {1.0, -2.0, 3.0, 3.0, 2.0, -1.0};
    std::array<double, 6> strategy_scalar = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> strategy_dispatched = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    core::regret_matching_action_major_f64_scalar(regret.data(), 2, 3, strategy_scalar.data());

    const auto was_enabled = core::hunl_sampled_simd_enabled();
    core::set_hunl_sampled_simd_enabled(false);
    core::regret_matching_action_major_f64(regret.data(), 2, 3, strategy_dispatched.data());
    EXPECT_EQ(core::hunl_sampled_simd_backend(), core::HUNLSampledSimdBackend::Scalar);

    for (std::size_t i = 0; i < strategy_scalar.size(); ++i) {
        EXPECT_NEAR(strategy_scalar[i], strategy_dispatched[i], TOL);
    }

    const std::array<double, 6> action_values = {2.0, 5.0, 4.0, 6.0, 1.0, 3.0};
    const std::array<double, 3> node_values = {4.0, 3.0, 2.0};
    const std::array<double, 3> cf_reach = {1.0, 0.5, 2.0};
    std::array<double, 6> regret_delta_scalar = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> regret_delta_dispatched = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    core::add_regret_delta_action_major_f64_scalar(
        action_values.data(),
        node_values.data(),
        cf_reach.data(),
        2,
        3,
        regret_delta_scalar.data());
    core::add_regret_delta_action_major_f64(
        action_values.data(),
        node_values.data(),
        cf_reach.data(),
        2,
        3,
        regret_delta_dispatched.data());
    for (std::size_t i = 0; i < regret_delta_scalar.size(); ++i) {
        EXPECT_NEAR(regret_delta_scalar[i], regret_delta_dispatched[i], TOL);
    }

    core::set_hunl_sampled_simd_enabled(was_enabled);
}

TEST_CASE(hunl_sampled_profile_formats_summary_into_caller_buffer) {
    core::HUNLSampledProfile profile;
    profile.record_traversal(128, 4096, 64);
    profile.record_sparse_storage(12, 768);
    profile.record_memory_budget(8, 12, 768, 64, 128, 32, 1024, false, false);
    profile.add_traverse_seconds(0.25);
    profile.add_merge_seconds(0.05);

    std::array<char, 256> buffer = {};
    const auto written = profile.format_summary(buffer.data(), buffer.size());

    EXPECT_TRUE(written > 0);
    EXPECT_TRUE(std::strstr(buffer.data(), "traversals=128") != nullptr);
    EXPECT_TRUE(std::strstr(buffer.data(), "sparse_rows=12") != nullptr);
    EXPECT_TRUE(std::strstr(buffer.data(), "mem_total=1024") != nullptr);
    EXPECT_TRUE(std::strstr(buffer.data(), "t_merge=0.050000") != nullptr);
}

}  // namespace
