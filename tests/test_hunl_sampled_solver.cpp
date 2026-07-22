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
#include <limits>
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

core::HUNLState make_sampled_facing_bet_state() {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_stack = 1000;
    config->big_blind = 100;
    config->starting_street = core::Street::River;
    config->initial_board = {
        core::card_to_int(2, 0),
        core::card_to_int(3, 1),
        core::card_to_int(4, 2),
        core::card_to_int(8, 3),
        core::card_to_int(9, 0),
    };
    config->initial_pot = 200;
    config->initial_contributions = {100, 100};
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(14, 1), core::card_to_int(14, 2)},
        {core::card_to_int(13, 1), core::card_to_int(13, 2)},
    }};
    config->river_bet_fractions = std::vector<double>{1.0};
    config->postflop_raise_cap = 1;
    config->include_all_in = false;

    const auto root = core::HUNLState::initial(config);
    return root.apply(core::ACTION_BET_33);
}

core::HUNLState make_suit_symmetric_chance_state() {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::Flop;
    config->initial_board = {
        core::card_to_int(14, 0),
        core::card_to_int(14, 1),
        core::card_to_int(14, 2),
    };
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(13, 0), core::card_to_int(13, 1)},
        {core::card_to_int(12, 0), core::card_to_int(12, 1)},
    }};
    auto state = core::HUNLState::initial(config);
    state.street = core::Street::Turn;
    state.cur_player = -1;
    state.pending_board_deals = 1;
    return state;
}

core::HUNLState make_sampled_tie_showdown_state() {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_stack = 1000;
    config->big_blind = 100;
    config->starting_street = core::Street::River;
    config->initial_board = {
        core::card_to_int(10, 0),
        core::card_to_int(11, 0),
        core::card_to_int(12, 0),
        core::card_to_int(13, 0),
        core::card_to_int(14, 0),
    };
    config->initial_pot = 200;
    config->initial_contributions = {100, 100};
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(2, 1), core::card_to_int(3, 1)},
        {core::card_to_int(4, 2), core::card_to_int(5, 2)},
    }};
    config->include_all_in = false;
    config->bet_size_fractions.clear();
    config->river_bet_fractions = std::vector<double>{};

    return core::HUNLState::initial(config)
        .apply(core::ACTION_CHECK)
        .apply(core::ACTION_CHECK);
}

TEST_CASE(hunl_sampled_config_defaults_validate) {
    const core::HUNLSampledSolverConfig config;
    const auto validation = core::validate_sampled_config(config);

    EXPECT_TRUE(validation.ok);
    EXPECT_EQ(config.mode, core::HUNLFlatSamplingMode::External);
    EXPECT_EQ(config.precision, core::HUNLFlatStoragePrecision::Float32);
    EXPECT_EQ(config.layout, core::HUNLFlatValueLayout::InfosetActionHand);
    EXPECT_TRUE(!config.use_public_chance_isomorphism);
    EXPECT_TRUE(config.lazy_public_expansion);
    EXPECT_TRUE(config.sparse_infosets);
    EXPECT_TRUE(config.enable_memory_guardrails);
    EXPECT_TRUE(config.memory_warning_bytes < config.memory_fail_bytes);
}

TEST_CASE(hunl_sampled_config_rejects_unimplemented_float64_precision) {
    auto config = core::HUNLSampledSolverConfig{};
    config.precision = core::HUNLFlatStoragePrecision::Float64;

    const auto validation = core::validate_sampled_config(config);
    EXPECT_TRUE(!validation.ok);
    EXPECT_THROW(core::validate_sampled_config_or_throw(config), std::invalid_argument);
    EXPECT_THROW(core::HUNLSampledSolver(config), std::invalid_argument);
}

TEST_CASE(hunl_sampled_config_rejects_unimplemented_compressed16_precision) {
    auto config = core::HUNLSampledSolverConfig{};
    config.precision = core::HUNLFlatStoragePrecision::Compressed16;

    const auto validation = core::validate_sampled_config(config);
    EXPECT_TRUE(!validation.ok);
    EXPECT_THROW(core::validate_sampled_config_or_throw(config), std::invalid_argument);
    EXPECT_THROW(core::HUNLSampledSolver(config), std::invalid_argument);
}

TEST_CASE(hunl_sampled_storage_rejects_precision_that_views_cannot_represent) {
    EXPECT_THROW(
        core::HUNLSampledStorage(
            core::HUNLFlatValueLayout::InfosetActionHand,
            core::HUNLFlatStoragePrecision::Float64),
        std::invalid_argument);
    EXPECT_THROW(
        core::HUNLSampledStorage(
            core::HUNLFlatValueLayout::InfosetActionHand,
            core::HUNLFlatStoragePrecision::Compressed16),
        std::invalid_argument);
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

TEST_CASE(hunl_sampled_storage_reusing_id_with_identical_shape_is_allowed) {
    core::HUNLSampledStorage storage;
    const core::HUNLSampledInfosetShape shape{core::InfosetId{17}, 1, core::Street::River, 4, 3};
    const auto first = storage.ensure_row(shape);
    first.regret[0] = 2.0f;
    const auto second = storage.ensure_row(shape);

    EXPECT_EQ(storage.row_count(), 1U);
    EXPECT_EQ(storage.total_value_count(), 12U);
    EXPECT_EQ(second.regret[0], 2.0f);
}

TEST_CASE(hunl_sampled_storage_reusing_id_with_different_shape_fails_for_each_dimension) {
    const core::HUNLSampledInfosetShape base{core::InfosetId{18}, 0, core::Street::Turn, 2, 2};
    const std::array<core::HUNLSampledInfosetShape, 4> mismatches = {{
        {base.id, 1, base.street, base.bucket_count, base.action_count},
        {base.id, base.player, core::Street::River, base.bucket_count, base.action_count},
        {base.id, base.player, base.street, 3, base.action_count},
        {base.id, base.player, base.street, base.bucket_count, 3},
    }};

    for (const auto& mismatch : mismatches) {
        core::HUNLSampledStorage storage;
        storage.ensure_row(base);
        EXPECT_THROW(storage.ensure_row(mismatch), std::invalid_argument);
        EXPECT_EQ(storage.row_count(), 1U);
        EXPECT_EQ(storage.total_value_count(), 4U);
    }
}

TEST_CASE(hunl_sampled_storage_requires_reacquiring_views_after_row_growth) {
    core::HUNLSampledStorage storage;
    const auto first = storage.ensure_row({core::InfosetId{19}, 0, core::Street::Turn, 1, 2});
    first.regret[0] = 4.0f;
    storage.ensure_row({core::InfosetId{20}, 1, core::Street::River, 2, 1});

    const auto reacquired = storage.view_mut(core::InfosetId{19});
    EXPECT_EQ(reacquired.value_count(), 2U);
    EXPECT_EQ(reacquired.regret[0], 4.0f);
}

TEST_CASE(hunl_sampled_storage_value_count_uses_checked_size_t_arithmetic) {
    core::HUNLSampledInfosetMeta meta;
    meta.bucket_count = 3;
    meta.action_count = 7;
    EXPECT_EQ(meta.value_count(), static_cast<std::size_t>(21));

    if constexpr (sizeof(std::size_t) <= sizeof(std::uint32_t)) {
        meta.bucket_count = std::numeric_limits<std::uint32_t>::max();
        meta.action_count = std::numeric_limits<std::uint8_t>::max();
        EXPECT_EQ(meta.value_count(), std::numeric_limits<std::size_t>::max());
    }
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

TEST_CASE(hunl_sampled_builder_public_chance_isomorphism_is_disabled_for_private_state_safety) {
    core::HUNLSampledBuilder requested_builder({true});
    core::HUNLSampledBuilder raw_builder({false});
    const auto root_state = make_lazy_root_state();

    const auto requested_root = requested_builder.initialize(root_state);
    const auto raw_root = raw_builder.initialize(root_state);
    const auto raw_outcomes = root_state.chance_outcomes().size();

    requested_builder.ensure_expanded(requested_root);
    raw_builder.ensure_expanded(raw_root);

    EXPECT_TRUE(raw_outcomes > 0U);
    EXPECT_EQ(raw_builder.node(raw_root).edge_count, raw_outcomes);
    EXPECT_EQ(requested_builder.node(requested_root).edge_count, raw_outcomes);
    EXPECT_TRUE(!requested_builder.node(requested_root).chance_isomorphic);
    EXPECT_EQ(requested_builder.node(requested_root).edge_count, raw_builder.node(raw_root).edge_count);
}

TEST_CASE(hunl_sampled_builder_rejects_history_overflow_instead_of_truncating_keys) {
    auto first = make_lazy_root_state();
    auto second = first;
    first.betting_history_codes.clear();
    second.betting_history_codes.clear();
    first.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES + 1U, 7);
    second.current_street_history_codes = first.current_street_history_codes;
    second.current_street_history_codes.back() = 8;

    EXPECT_THROW(core::HUNLSampledBuilder::make_key(first), std::invalid_argument);
    EXPECT_THROW(core::HUNLSampledBuilder::make_key(second), std::invalid_argument);

    core::HUNLSampledBuilder builder;
    EXPECT_THROW(builder.initialize(first), std::invalid_argument);
    EXPECT_THROW(builder.initialize(second), std::invalid_argument);
}

TEST_CASE(hunl_sampled_builder_accepts_history_at_exact_key_capacity) {
    auto state = make_lazy_root_state();
    state.betting_history_codes.clear();
    state.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES, 7);

    const auto key = core::HUNLSampledBuilder::make_key(state);
    EXPECT_EQ(key.history_count, core::HUNL_MAX_HISTORY_CODES);
    EXPECT_EQ(key.street_lengths[0], core::HUNL_MAX_HISTORY_CODES);
}

TEST_CASE(hunl_sampled_builder_accepts_exact_capacity_split_across_streets) {
    auto state = make_lazy_root_state();
    state.betting_history_codes = {{1, 2, 3}, {4, 5}, {6}};
    state.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES - 6U, 9);

    const auto key = core::HUNLSampledBuilder::make_key(state);
    EXPECT_EQ(key.history_count, core::HUNL_MAX_HISTORY_CODES);
    EXPECT_EQ(key.street_lengths[0], 3U);
    EXPECT_EQ(key.street_lengths[1], 2U);
    EXPECT_EQ(key.street_lengths[2], 1U);
    EXPECT_EQ(key.street_lengths[3], core::HUNL_MAX_HISTORY_CODES - 6U);
}

TEST_CASE(hunl_sampled_builder_rejects_cumulative_overflow_across_street_segments) {
    auto state = make_lazy_root_state();
    state.betting_history_codes = {
        std::vector<int>(16, 1),
        std::vector<int>(16, 2),
        std::vector<int>(16, 3),
        std::vector<int>(1, 4),
    };
    state.current_street_history_codes.clear();

    EXPECT_THROW(core::HUNLSampledBuilder::make_key(state), std::invalid_argument);
}

TEST_CASE(hunl_sampled_builder_rejects_current_street_overflow_after_prior_history) {
    auto state = make_lazy_root_state();
    state.betting_history_codes = {{1, 2, 3}, {4, 5}, {6}};
    state.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES - 5U, 9);

    EXPECT_THROW(core::HUNLSampledBuilder::make_key(state), std::invalid_argument);
}

TEST_CASE(hunl_sampled_builder_rejects_each_single_segment_overflow) {
    for (std::size_t street = 0; street < 4; ++street) {
        auto state = make_lazy_root_state();
        state.betting_history_codes.clear();
        state.betting_history_codes.resize(street + 1U);
        state.betting_history_codes[street].assign(core::HUNL_MAX_HISTORY_CODES + 1U, 1);
        state.current_street_history_codes.clear();

        EXPECT_THROW(core::HUNLSampledBuilder::make_key(state), std::invalid_argument);
    }
}

TEST_CASE(hunl_sampled_builder_rejects_current_history_when_all_street_slots_are_used) {
    auto state = make_lazy_root_state();
    state.betting_history_codes.resize(4);
    state.current_street_history_codes = {1};

    EXPECT_THROW(core::HUNLSampledBuilder::make_key(state), std::invalid_argument);
}

TEST_CASE(hunl_sampled_builder_distinguishes_different_in_capacity_history_suffixes) {
    auto first = make_lazy_root_state();
    auto second = first;
    first.betting_history_codes.clear();
    second.betting_history_codes.clear();
    first.current_street_history_codes.assign(core::HUNL_MAX_HISTORY_CODES, 7);
    second.current_street_history_codes = first.current_street_history_codes;
    second.current_street_history_codes.back() = 8;

    const auto first_key = core::HUNLSampledBuilder::make_key(first);
    const auto second_key = core::HUNLSampledBuilder::make_key(second);
    EXPECT_TRUE(!(first_key == second_key));

}

TEST_CASE(hunl_sampled_builder_rejects_excess_street_segments_in_keys) {
    auto state = make_lazy_root_state();
    state.betting_history_codes.resize(5);

    EXPECT_THROW(core::HUNLSampledBuilder::make_key(state), std::invalid_argument);
}

TEST_CASE(hunl_sampled_solver_memory_estimate_includes_lazy_graph_cache) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_state = make_lazy_root_state();

    const auto empty_memory = solver.memory_estimate();
    const auto initialization = solver.run_batches(request, 0);
    const auto initialized_memory = solver.memory_estimate();
    solver.builder().ensure_expanded(solver.builder().root_id());
    const auto expanded_memory = solver.memory_estimate();

    EXPECT_EQ(empty_memory.public_states_cached, 0U);
    EXPECT_EQ(initialization.batches_completed, 0U);
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

    const auto result = solver.run_batches(request, 0);
    EXPECT_TRUE(result.profile.public_states_cached >= 1U);
    EXPECT_TRUE(result.profile.worker_delta_bytes > 0U);
    EXPECT_TRUE(result.profile.export_bytes <= result.profile.total_memory_bytes);
    EXPECT_TRUE(result.profile.total_memory_bytes >= result.profile.worker_delta_bytes);
    EXPECT_TRUE(!result.profile.memory_rejected);
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_EQ(result.profile.traversals, 0U);
}

TEST_CASE(hunl_sampled_solver_preflight_rejects_impossible_config_without_unbounded_fallback) {
    core::HUNLSampledSolverConfig config;
    config.max_cached_public_states = std::numeric_limits<std::uint32_t>::max();
    config.memory_warning_bytes = 1U;
    config.memory_fail_bytes = 1024U;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 2;

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Rejected);
    EXPECT_TRUE(preflight.adjustments.reduced_minibatch);
    EXPECT_EQ(preflight.effective_config.minibatch_size, 1U);
    EXPECT_EQ(preflight.effective_config.traversals_per_iteration, config.traversals_per_iteration);
}

TEST_CASE(hunl_sampled_solver_preflight_records_strictly_decreasing_adaptive_estimates) {
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1024;
    config.traversals_per_iteration = 4096;
    config.bucket_count_hint = 4096;
    config.depth_limit_plies_hint = 8;
    config.memory_warning_bytes = 1U;
    config.memory_fail_bytes = 2ULL * 1024ULL * 1024ULL;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 4;

    const auto preflight = solver.preflight(request);
    EXPECT_TRUE(preflight.adjustments.recorded_step_count > 0U);
    EXPECT_TRUE(preflight.adjustments.recorded_step_count <=
                core::HUNLSampledAdaptiveAdjustments::kMaxRecordedSteps);
    for (std::size_t step = 0; step < preflight.adjustments.recorded_step_count; ++step) {
        EXPECT_TRUE(preflight.adjustments.estimate_after[step] <
                    preflight.adjustments.estimate_before[step]);
    }
}

TEST_CASE(hunl_sampled_solver_preflight_reduces_depth_limit_hint_when_it_lowers_memory) {
    core::HUNLSampledSolverConfig config;
    config.minibatch_size = 1;
    config.traversals_per_iteration = 1;
    config.bucket_count_hint = 32;
    config.depth_limit_plies_hint = 8;
    config.memory_warning_bytes = 1U;
    config.memory_fail_bytes = 8U * 1024U;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 4;

    const auto preflight = solver.preflight(request);
    EXPECT_TRUE(preflight.adjustments.reduced_depth_limit_hint);
    EXPECT_TRUE(preflight.effective_config.depth_limit_plies_hint < config.depth_limit_plies_hint);
    EXPECT_TRUE(preflight.estimate.total_bytes() <= config.memory_fail_bytes);
}

TEST_CASE(hunl_sampled_solver_preflight_saturates_overflowing_memory_estimates) {
    core::HUNLSampledSolverConfig config;
    config.max_cached_public_states = std::numeric_limits<std::uint32_t>::max();
    config.workers = std::numeric_limits<std::size_t>::max();
    config.minibatch_size = std::numeric_limits<std::uint32_t>::max();
    config.bucket_count_hint = std::numeric_limits<std::size_t>::max();
    config.depth_limit_plies_hint = std::numeric_limits<std::uint32_t>::max();
    config.memory_fail_bytes = std::numeric_limits<std::uint64_t>::max() - 1U;

    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = std::numeric_limits<std::uint8_t>::max();

    const auto preflight = solver.preflight(request);
    EXPECT_EQ(preflight.status, core::HUNLSampledMemoryStatus::Rejected);
    EXPECT_EQ(preflight.estimate.total_bytes(), std::numeric_limits<std::uint64_t>::max());
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

TEST_CASE(hunl_sampled_solver_positive_batch_request_fails_without_reporting_work) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 3;
    const auto root_id = solver.builder().initialize(make_lazy_root_state());

    const auto initialized = solver.run_batches(request, 0);
    const auto nodes_before = solver.builder().node_count();
    const auto profile_before = solver.profile().snapshot();
    const auto strategy_before = solver.export_root_strategy();

    EXPECT_THROW(solver.run_batches(request, 1), core::HUNLSampledSolverNotReady);

    EXPECT_EQ(root_id, 0U);
    EXPECT_EQ(initialized.batches_completed, 0U);
    EXPECT_EQ(strategy_before.actions.size(), 3U);
    EXPECT_EQ(solver.builder().node_count(), nodes_before);
    EXPECT_EQ(solver.profile().snapshot().traversals, profile_before.traversals);
    EXPECT_EQ(solver.profile().snapshot().nodes_visited, profile_before.nodes_visited);
    EXPECT_EQ(solver.profile().snapshot().infosets_updated, profile_before.infosets_updated);
    EXPECT_EQ(solver.export_root_strategy().actions.size(), strategy_before.actions.size());
    for (std::size_t action = 0; action < strategy_before.actions.size(); ++action) {
        EXPECT_EQ(
            solver.export_root_strategy().actions[action].action_index,
            strategy_before.actions[action].action_index);
        EXPECT_NEAR(
            solver.export_root_strategy().actions[action].probability,
            strategy_before.actions[action].probability,
            TOL);
    }
}

TEST_CASE(hunl_sampled_solver_positive_time_budgets_fail_explicitly) {
    core::HUNLSampledSolver solver;
    core::HUNLSampledSolveRequest request;
    request.root_action_count = 3;

    EXPECT_THROW(
        solver.solve_for(request, std::chrono::milliseconds{1}),
        core::HUNLSampledSolverNotReady);
    EXPECT_THROW(
        solver.solve_for(request, std::chrono::milliseconds{15'000}),
        core::HUNLSampledSolverNotReady);
    EXPECT_EQ(solver.profile().snapshot().traversals, 0U);
    EXPECT_EQ(solver.export_root_strategy().actions.size(), 0U);
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

    EXPECT_THROW(solver.run_batches(request, 0), std::runtime_error);
}

TEST_CASE(hunl_sampled_solver_runs_prepared_positive_work_with_deterministic_worker_batches) {
    core::HUNLSampledSolverConfig config;
    config.workers = 2;
    config.minibatch_size = 2;
    config.max_cached_public_states = 1024;
    config.seed = 77;
    core::HUNLSampledSolver first(config);
    core::HUNLSampledSolver second(config);
    core::HUNLSampledSolveRequest request;
    request.root_state = make_lazy_root_state();
    const auto first_result = first.run_batches(request, 1);
    const auto second_result = second.run_batches(request, 1);
    EXPECT_EQ(first_result.batches_completed, 1U);
    EXPECT_EQ(first_result.profile.traversals, 2U);
    EXPECT_EQ(first_result.root_strategy.actions.size(), second_result.root_strategy.actions.size());
    for (std::size_t action = 0; action < first_result.root_strategy.actions.size(); ++action) {
        EXPECT_NEAR(first_result.root_strategy.actions[action].probability,
                    second_result.root_strategy.actions[action].probability, TOL);
    }
}

TEST_CASE(hunl_sampled_builder_enforces_public_state_admission_limit_during_expansion) {
    core::HUNLSampledBuilder builder({false, 1});
    const auto root = builder.initialize(make_lazy_root_state());
    EXPECT_THROW(builder.ensure_expanded(root), std::runtime_error);
    EXPECT_EQ(builder.node_count(), 1U);
}

void expect_sampled_positive_work_request_is_fail_closed(
    core::HUNLFlatSamplingMode mode,
    std::uint8_t action_count,
    std::uint32_t batch_count,
    std::chrono::milliseconds budget) {
    core::HUNLSampledSolverConfig config;
    config.mode = mode;
    config.seed = 0xC0FFEEU;
    core::HUNLSampledSolver solver(config);
    core::HUNLSampledSolveRequest request;
    request.root_action_count = action_count;
    request.root_state = make_sampled_facing_bet_state();

    const auto initialized = solver.run_batches(request, 0);
    const auto profile_before = solver.profile().snapshot();
    const auto strategy_before = solver.export_root_strategy();
    const auto memory_before = solver.memory_estimate();
    const auto nodes_before = solver.builder().node_count();
    const auto rows_before = solver.storage().row_count();

    EXPECT_EQ(initialized.batches_completed, 0U);
    const auto batch_result = solver.run_batches(request, batch_count);
    const auto timed_result = solver.solve_for(request, budget);

    const auto profile_after = solver.profile().snapshot();
    const auto memory_after = solver.memory_estimate();
    const auto strategy_after = solver.export_root_strategy();
    EXPECT_EQ(batch_result.batches_completed, batch_count);
    EXPECT_EQ(timed_result.batches_completed, 1U);
    EXPECT_TRUE(timed_result.timed_out);
    EXPECT_TRUE(profile_after.traversals >= profile_before.traversals + config.minibatch_size);
    EXPECT_TRUE(solver.builder().node_count() >= nodes_before);
    EXPECT_TRUE(solver.storage().row_count() >= rows_before);
    EXPECT_TRUE(memory_after.total_bytes() >= memory_before.total_bytes());
    EXPECT_TRUE(!strategy_after.actions.empty());
}

#define SAMPLED_FAIL_CLOSED_CASE(name, mode_value, action_count_value, batch_count_value, budget_value) \
    TEST_CASE(name) { expect_sampled_positive_work_request_is_fail_closed(mode_value, action_count_value, batch_count_value, budget_value); }
#define SAMPLED_FAIL_CLOSED_FAMILY(prefix, mode_value) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a1_b1_t1, mode_value, 1U, 1U, std::chrono::milliseconds{1}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a1_b2_t2, mode_value, 1U, 2U, std::chrono::milliseconds{2}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a1_b64_t100, mode_value, 1U, 64U, std::chrono::milliseconds{100}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a1_bmax_t15000, mode_value, 1U, std::numeric_limits<std::uint32_t>::max(), std::chrono::milliseconds{15'000}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a2_b1_t1, mode_value, 2U, 1U, std::chrono::milliseconds{1}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a2_b2_t2, mode_value, 2U, 2U, std::chrono::milliseconds{2}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a2_b64_t100, mode_value, 2U, 64U, std::chrono::milliseconds{100}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a2_bmax_t15000, mode_value, 2U, std::numeric_limits<std::uint32_t>::max(), std::chrono::milliseconds{15'000}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a3_b1_t1, mode_value, 3U, 1U, std::chrono::milliseconds{1}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a3_b2_t2, mode_value, 3U, 2U, std::chrono::milliseconds{2}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a3_b64_t100, mode_value, 3U, 64U, std::chrono::milliseconds{100}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a3_bmax_t15000, mode_value, 3U, std::numeric_limits<std::uint32_t>::max(), std::chrono::milliseconds{15'000}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a8_b1_t1, mode_value, 8U, 1U, std::chrono::milliseconds{1}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a8_b2_t2, mode_value, 8U, 2U, std::chrono::milliseconds{2}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a8_b64_t100, mode_value, 8U, 64U, std::chrono::milliseconds{100}) \
    SAMPLED_FAIL_CLOSED_CASE(prefix##_a8_bmax_t15000, mode_value, 8U, std::numeric_limits<std::uint32_t>::max(), std::chrono::milliseconds{15'000})

SAMPLED_FAIL_CLOSED_FAMILY(hunl_sampled_fail_closed_exact, core::HUNLFlatSamplingMode::Exact)
SAMPLED_FAIL_CLOSED_FAMILY(hunl_sampled_fail_closed_public_chance, core::HUNLFlatSamplingMode::PublicChance)
SAMPLED_FAIL_CLOSED_FAMILY(hunl_sampled_fail_closed_external, core::HUNLFlatSamplingMode::External)
SAMPLED_FAIL_CLOSED_FAMILY(hunl_sampled_fail_closed_average_strategy, core::HUNLFlatSamplingMode::AverageStrategy)

#undef SAMPLED_FAIL_CLOSED_FAMILY
#undef SAMPLED_FAIL_CLOSED_CASE

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

TEST_CASE(hunl_sampled_external_traversal_matches_hand_computed_river_update) {
    const auto root_state = make_sampled_facing_bet_state();
    core::HUNLSampledBuilder builder({false});
    const auto root_id = builder.initialize(root_state);
    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;

    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.traversing_player = 0;
    request.seed = 17;
    request.trajectory_id = 3;
    request.iteration = 1;

    const auto result = traversal.run(request, scratch);
    const auto root_infoset = builder.node(root_id).infoset_id;
    const auto row = storage.view(root_infoset);

    EXPECT_NEAR(root_state.apply(core::ACTION_FOLD).utility()[0], 0.0, TOL);
    EXPECT_NEAR(root_state.apply(core::ACTION_CALL).utility()[0], 4.0, TOL);
    EXPECT_NEAR(result.value, 2.0, TOL);
    EXPECT_EQ(result.infosets_updated, 1U);
    EXPECT_EQ(result.opponent_nodes_sampled, 0U);
    EXPECT_EQ(row.action_count, 2U);
    EXPECT_NEAR(row.regret[0], -2.0, TOL);
    EXPECT_NEAR(row.regret[1], 2.0, TOL);
    EXPECT_NEAR(row.strategy_sum[0], 0.5, TOL);
    EXPECT_NEAR(row.strategy_sum[1], 0.5, TOL);
}

TEST_CASE(hunl_sampled_unmerged_traversal_keeps_central_rows_unchanged_until_coordinator_merge) {
    const auto root_state = make_sampled_facing_bet_state();
    core::HUNLSampledBuilder builder({false});
    const auto root_id = builder.initialize(root_state);
    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.traversing_player = 0;
    core::prepare_hunl_sampled_trajectory(builder, storage, terminal_evaluator, request);
    (void)traversal.run_unmerged(request, scratch);
    const auto row = storage.view(builder.node(root_id).infoset_id);
    EXPECT_NEAR(row.regret[0], 0.0, TOL);
    EXPECT_TRUE(!scratch.deltas.empty());
    core::merge_hunl_sampled_worker_deltas(storage, scratch);
    EXPECT_TRUE(std::abs(storage.view(builder.node(root_id).infoset_id).regret[0]) > 0.0f);
}

TEST_CASE(hunl_sampled_unmerged_traversal_requires_coordinator_preparation) {
    core::HUNLSampledBuilder builder({false});
    const auto root_id = builder.initialize(make_sampled_facing_bet_state());
    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    EXPECT_THROW(traversal.run_unmerged(request, scratch), core::HUNLSampledTraversalPreparationRequired);
    core::prepare_hunl_sampled_trajectory(builder, storage, terminal_evaluator, request);
    EXPECT_TRUE(traversal.run_unmerged(request, scratch).nodes_visited > 0U);
}

TEST_CASE(hunl_sampled_coordinator_merge_orders_worker_deltas_deterministically) {
    core::HUNLSampledStorage storage;
    storage.ensure_row({core::InfosetId{0}, 0, core::Street::River, 1, 2});
    core::HUNLSampledWorkerScratch scratch;
    scratch.deltas = {
        {core::InfosetId{0}, 0, 1, 2.0, 3.0},
        {core::InfosetId{0}, 0, 0, 1.0, 4.0},
    };
    core::merge_hunl_sampled_worker_deltas(storage, scratch);
    EXPECT_EQ(scratch.deltas[0].action, 0U);
    const auto row = storage.view(core::InfosetId{0});
    EXPECT_NEAR(row.regret[0], 1.0, TOL);
    EXPECT_NEAR(row.regret[1], 2.0, TOL);
}

TEST_CASE(hunl_sampled_external_traversal_samples_opponent_strategy_probabilities) {
    const auto root_state = make_sampled_facing_bet_state();
    core::HUNLSampledBuilder builder({false});
    const auto root_id = builder.initialize(root_state);
    builder.ensure_expanded(root_id);

    core::HUNLSampledStorage storage;
    const auto root_node = builder.node(root_id);
    auto row = storage.ensure_row({
        root_node.infoset_id,
        root_node.player,
        root_node.street,
        1,
        static_cast<std::uint8_t>(root_node.edge_count),
    });
    row.regret[0] = 3.0f;
    row.regret[1] = 1.0f;

    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.traversing_player = 1;
    request.seed = 991;
    request.iteration = 5;

    constexpr std::uint64_t trajectories = 4000;
    std::array<std::uint64_t, 2> selected = {0, 0};
    double value_sum = 0.0;
    for (std::uint64_t trajectory = 0; trajectory < trajectories; ++trajectory) {
        request.trajectory_id = trajectory;
        const auto result = traversal.run(request, scratch);
        EXPECT_EQ(result.opponent_nodes_sampled, 1U);
        EXPECT_EQ(result.sampled_edge_slot_count, 1U);
        const auto action = result.sampled_edge_slots[0];
        EXPECT_TRUE(action < selected.size());
        ++selected[action];
        value_sum += result.value;
    }

    const auto fold_frequency = static_cast<double>(selected[0]) / static_cast<double>(trajectories);
    const auto call_frequency = static_cast<double>(selected[1]) / static_cast<double>(trajectories);
    EXPECT_NEAR(fold_frequency, 0.75, 0.04);
    EXPECT_NEAR(call_frequency, 0.25, 0.04);
    EXPECT_NEAR(value_sum / static_cast<double>(trajectories), 1.0, 0.16);
}

TEST_CASE(hunl_sampled_external_traversal_samples_chance_edges_by_probability) {
    core::HUNLSampledBuilder builder({true});
    const auto root_id = builder.initialize(make_suit_symmetric_chance_state());
    builder.ensure_expanded(root_id);
    const auto root = builder.node(root_id);
    EXPECT_TRUE(root.edge_count > 1U);
    // Public-board symmetry is intentionally disabled until private-state
    // suit remapping is implemented; all chance outcomes remain explicit.
    EXPECT_TRUE(!root.chance_isomorphic);

    bool probabilities_are_uniform = true;
    const auto first_probability = builder.edge(root.edge_begin).probability;
    for (std::size_t edge_slot = 1; edge_slot < root.edge_count; ++edge_slot) {
        if (std::abs(builder.edge(root.edge_begin + edge_slot).probability - first_probability) > 1e-12) {
            probabilities_are_uniform = false;
            break;
        }
    }
    EXPECT_TRUE(probabilities_are_uniform);

    for (std::size_t edge_slot = 0; edge_slot < root.edge_count; ++edge_slot) {
        auto& child = builder.node_mut(builder.edge(root.edge_begin + edge_slot).child);
        child.type = core::HUNLFlatNodeType::TerminalShowdown;
        child.terminal_utility = {1.0, -1.0};
    }

    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.traversing_player = 0;
    request.seed = 1234567;
    request.iteration = 9;

    constexpr std::uint64_t trajectories = 6000;
    std::vector<std::uint64_t> selected(root.edge_count, 0U);
    for (std::uint64_t trajectory = 0; trajectory < trajectories; ++trajectory) {
        request.trajectory_id = trajectory;
        const auto result = traversal.run(request, scratch);
        EXPECT_EQ(result.chance_nodes_sampled, 1U);
        EXPECT_EQ(result.sampled_edge_slot_count, 1U);
        EXPECT_TRUE(result.sampled_edge_slots[0] < selected.size());
        ++selected[result.sampled_edge_slots[0]];
        EXPECT_NEAR(result.value, 1.0, TOL);
    }

    for (std::size_t edge_slot = 0; edge_slot < root.edge_count; ++edge_slot) {
        const auto observed = static_cast<double>(selected[edge_slot]) / static_cast<double>(trajectories);
        const auto expected = builder.edge(root.edge_begin + edge_slot).probability;
        EXPECT_NEAR(observed, expected, 0.015);
    }
}

TEST_CASE(hunl_sampled_external_traversal_uses_independent_draws_down_the_path) {
    core::HUNLSampledBuilder builder({false});
    const auto root_id = builder.initialize(make_lazy_root_state());
    core::HUNLSampledStorage storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = root_id;
    request.traversing_player = 0;
    request.seed = 44;
    request.iteration = 2;

    bool found_different_slots = false;
    for (std::uint64_t trajectory = 0; trajectory < 7; ++trajectory) {
        request.trajectory_id = trajectory;
        const auto result = traversal.run(request, scratch);
        EXPECT_TRUE(result.sampled_edge_slot_count >= 2U);
        if (result.sampled_edge_slots[0] != result.sampled_edge_slots[1]) {
            found_different_slots = true;
        }
    }
    EXPECT_TRUE(found_different_slots);
}

TEST_CASE(hunl_sampled_terminal_values_preserve_win_loss_and_tie_perspectives) {
    const auto win_state = make_sampled_facing_bet_state().apply(core::ACTION_CALL);
    core::HUNLSampledBuilder win_builder({false});
    const auto win_root = win_builder.initialize(win_state);
    core::HUNLSampledStorage win_storage;
    core::HUNLSampledTerminalEvaluator terminal_evaluator;
    core::HUNLSampledTraversal win_traversal(win_builder, win_storage, terminal_evaluator);
    core::HUNLSampledWorkerScratch scratch;
    core::HUNLSampledTraversalRequest request;
    request.root_node_id = win_root;

    request.traversing_player = 0;
    const auto winner = win_traversal.run(request, scratch);
    request.traversing_player = 1;
    const auto loser = win_traversal.run(request, scratch);
    EXPECT_NEAR(winner.value, win_state.utility()[0], TOL);
    EXPECT_NEAR(loser.value, win_state.utility()[1], TOL);
    EXPECT_TRUE(winner.value > 0.0);
    EXPECT_TRUE(loser.value < 0.0);

    const auto tie_state = make_sampled_tie_showdown_state();
    core::HUNLSampledBuilder tie_builder({false});
    const auto tie_root = tie_builder.initialize(tie_state);
    core::HUNLSampledStorage tie_storage;
    core::HUNLSampledTraversal tie_traversal(tie_builder, tie_storage, terminal_evaluator);
    request.root_node_id = tie_root;
    request.traversing_player = 0;
    const auto tie0 = tie_traversal.run(request, scratch);
    request.traversing_player = 1;
    const auto tie1 = tie_traversal.run(request, scratch);
    EXPECT_NEAR(tie0.value, tie_state.utility()[0], TOL);
    EXPECT_NEAR(tie1.value, tie_state.utility()[1], TOL);
    EXPECT_NEAR(tie0.value, tie1.value, TOL);
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
