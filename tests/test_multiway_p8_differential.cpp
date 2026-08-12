#include "games/multiway_fixed.hpp"
#include "games/multiway_private.hpp"
#include "games/multiway_terminal.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_resolver_evaluation.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

texas::MultiwayTerminalInput terminal_input() {
    texas::MultiwayTerminalInput input;
    input.contributions = {101, 300, 300};
    input.folded = {false, false, false};
    input.strengths = {{3U}, {9U}, {9U}};
    input.odd_chip_first_seat = 2;
    return input;
}

texas::MultiwayFixedTerminalInput fixed_input(const texas::MultiwayTerminalInput& input) {
    texas::MultiwayFixedTerminalInput fixed;
    fixed.seat_count = static_cast<std::uint8_t>(input.contributions.size());
    fixed.odd_chip_first_seat = input.odd_chip_first_seat;
    for (std::size_t seat = 0U; seat < input.contributions.size(); ++seat) {
        fixed.contributions[seat] = input.contributions[seat];
        fixed.folded[seat] = input.folded[seat];
        fixed.strengths[seat] = input.strengths[seat];
    }
    return fixed;
}

struct ResolverFixture {
    texas::MultiwayModelIdentity identity = texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{});
    texas::MultiwayBucketRegistry buckets = texas::build_multiway_baseline_bucket_registry(
        identity, {{texas::Street::Flop, {8U, 13U, 17U}}});
    texas::MultiwayPublicStateDescriptor root = make_root();

    static texas::MultiwayPublicStateDescriptor make_root() {
        texas::MultiwayGameConfig config;
        config.starting_stacks = {2'000, 2'000, 2'000};
        config.initial_contributions = {100, 100, 100};
        config.initial_street_contributions = {0, 0, 0};
        config.big_blind = 100;
        config.street = texas::Street::Flop;
        const auto state = texas::MultiwayState::initial(config);
        const auto menu = texas::MultiwayActionAbstraction().make_legal_actions(state.snapshot(), 91U);
        return texas::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    }

    texas::MultiwayResolverRequest request() const {
        texas::MultiwayResolverRequest result;
        result.blueprint_identity = identity;
        result.public_state = root;
        result.hero_seat = 0;
        result.hero_cards = {24U, 31U};
        result.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        result.sampling_seed = 73U;
        return result;
    }
};

void run_terminal_differential_case(std::uint8_t index) {
    texas::MultiwayTerminalInput input;
    input.contributions = {
        10 + static_cast<int>(index % 5U),
        30 + static_cast<int>(index % 7U),
        50 + static_cast<int>(index % 11U),
    };
    input.folded = {index % 4U == 0U, index % 5U == 0U, false};
    input.strengths = {
        {index % 3U},
        {(index + 1U) % 3U},
        {(index + 2U) % 3U},
    };
    input.odd_chip_first_seat = static_cast<texas::PlayerId>(index % 3U);
    const auto direct = texas::settle_multiway_terminal(input);
    texas::MultiwayFixedTerminalScratch scratch;
    texas::MultiwayFixedTerminalResult fixed;
    texas::settle_multiway_terminal_fixed(fixed_input(input), scratch, fixed);
    EXPECT_EQ(direct.utilities.size(), input.contributions.size());
    EXPECT_EQ(direct.payouts.size(), input.contributions.size());
    EXPECT_EQ(direct.refunds.size(), input.contributions.size());
    EXPECT_EQ(fixed.seat_count, input.contributions.size());
    EXPECT_EQ(fixed.pot_count, direct.pots.size());
    int committed = 0;
    int returned = direct.rake_taken;
    for (std::size_t seat = 0U; seat < input.contributions.size(); ++seat) {
        EXPECT_EQ(fixed.refunds[seat], direct.refunds[seat]);
        EXPECT_EQ(fixed.payouts[seat], direct.payouts[seat]);
        EXPECT_EQ(fixed.utilities[seat], direct.utilities[seat]);
        EXPECT_EQ(direct.utilities[seat],
            static_cast<texas::Value>(direct.payouts[seat] + direct.refunds[seat] - input.contributions[seat]));
        committed += input.contributions[seat];
        returned += direct.payouts[seat] + direct.refunds[seat];
    }
    EXPECT_EQ(returned, committed);
}

void run_range_differential_case(std::uint8_t index) {
    const auto first = texas::Card::from_index(index).index();
    const auto second = static_cast<std::uint8_t>(first + 1U);
    const auto alternate = static_cast<std::uint8_t>(first + 2U);
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 3> entries = {{
        {{first, second}, static_cast<double>(index + 1U)},
        {{second, first}, static_cast<double>(index + 2U)},
        {{second, alternate}, static_cast<double>(index + 3U)},
    }};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U},
        {entries.data(), entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto view = beliefs.view(0U);
    const auto total = static_cast<double>(3U * index + 6U);
    EXPECT_NEAR(view.weight(texas::canonical_combos().id({first, second})),
        static_cast<double>(2U * index + 3U) / total, 1e-15);
    EXPECT_NEAR(view.weight(texas::canonical_combos().id({second, alternate})),
        static_cast<double>(index + 3U) / total, 1e-15);
    EXPECT_NEAR(view.metadata().input_mass, total, 1e-15);
    EXPECT_EQ(view.metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_TRUE(view.legal(texas::canonical_combos().id({first, second})));
    EXPECT_NEAR(view.metadata().normalized_mass, 1.0, 0.0);
}

void run_artifact_hash_differential_case(std::uint8_t index) {
    texas::MultiwayBlueprintConfig config;
    texas::MultiwayBlueprintSnapshot baseline;
    baseline.identity = texas::make_multiway_model_identity(config);
    baseline.public_state = {71U};
    baseline.infoset = {{71U}, 0};
    baseline.trajectories = 1U;
    baseline.training.trajectories = 1U;
    baseline.actions = {{{texas::MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
    auto changed = baseline;
    changed.public_state = {static_cast<std::uint64_t>(100U + index)};
    changed.infoset.public_state = changed.public_state;
    EXPECT_EQ(texas::MultiwayBlueprintArtifacts::snapshot_hash(baseline),
        texas::MultiwayBlueprintArtifacts::snapshot_hash(baseline));
    EXPECT_TRUE(texas::MultiwayBlueprintArtifacts::snapshot_hash(baseline) !=
        texas::MultiwayBlueprintArtifacts::snapshot_hash(changed));
    EXPECT_EQ(baseline.identity, changed.identity);
    EXPECT_TRUE(!(baseline.public_state == changed.public_state));
}

void run_adapter_differential_case(std::uint8_t index) {
    ResolverFixture fixture;
    texas::MultiwayResolverEvaluationAdapterConfig config;
    config.candidates = {{1U, texas::MultiwayResolverEvaluationCandidateKind::StaticLegal}};
    texas::MultiwayResolverEvaluationAdapter adapter(config);
    auto request = fixture.request();
    request.sampling_seed = index + 1U;
    const auto first = adapter.resolve(1U, request, index);
    const auto second = adapter.resolve(1U, request, index);
    EXPECT_EQ(first.sampling_seed, second.sampling_seed);
    EXPECT_EQ(first.candidate_id, 1U);
    EXPECT_EQ(first.result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_TRUE(first.result.has_sampled_action);
    EXPECT_TRUE(first.result.diagnostics.policy_normalized);
}

}  // namespace

TEST_CASE(multiway_p81_terminal_fixed_and_dynamic_settlement_are_chip_exact) {
    const auto input = terminal_input();
    const auto direct = texas::settle_multiway_terminal(input);
    texas::MultiwayFixedTerminalScratch scratch;
    texas::MultiwayFixedTerminalResult fixed_result;
    texas::settle_multiway_terminal_fixed(fixed_input(input), scratch, fixed_result);

    EXPECT_EQ(fixed_result.rake_taken, direct.rake_taken);
    for (std::size_t seat = 0U; seat < input.contributions.size(); ++seat) {
        EXPECT_EQ(fixed_result.refunds[seat], direct.refunds[seat]);
        EXPECT_EQ(fixed_result.payouts[seat], direct.payouts[seat]);
        EXPECT_EQ(fixed_result.utilities[seat], direct.utilities[seat]);
    }
}

TEST_CASE(multiway_p82_range_belief_and_compiled_ranges_canonicalize_duplicates) {
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 3> entries = {{
        {{10U, 11U}, 1.0}, {{11U, 10U}, 2.0}, {{12U, 13U}, 3.0},
    }};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U}, {entries.data(), entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto view = beliefs.view(0U);
    EXPECT_NEAR(view.weight(texas::canonical_combos().id({10U, 11U})), 0.5, 1e-15);
    EXPECT_NEAR(view.weight(texas::canonical_combos().id({12U, 13U})), 0.5, 1e-15);

    texas::MultiwayPrivateConfig ranges;
    ranges.ranges = {
        {{{10U, 11U}, 1.0}, {{11U, 10U}, 2.0}},
        {{{12U, 13U}, 1.0}},
    };
    texas::MultiwayCompiledPrivateRanges compiled(ranges);
    texas::MultiwayPrivateWorkerScratch first;
    texas::MultiwayPrivateWorkerScratch second;
    EXPECT_TRUE(compiled.try_sample_into(99U, first));
    EXPECT_TRUE(compiled.try_sample_into(99U, second));
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_NEAR(first.proposal_reach, second.proposal_reach, 0.0);
}

TEST_CASE(multiway_p83_artifact_round_trip_preserves_identity_and_snapshot_hash) {
    texas::MultiwayBlueprintConfig config;
    texas::MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = texas::make_multiway_model_identity(config);
    snapshot.public_state = {71U};
    snapshot.infoset = {{71U}, 0};
    snapshot.trajectories = 1U;
    snapshot.training.trajectories = 1U;
    snapshot.actions = {{{texas::MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
    const auto path = std::filesystem::temp_directory_path() /
        ("texas_solver_p83_differential_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    texas::MultiwayBlueprintArtifacts::save_atomic(path, snapshot);
    const auto loaded = texas::MultiwayBlueprintArtifacts::load_verified(path, snapshot.identity);
    EXPECT_EQ(loaded.snapshot.identity, snapshot.identity);
    EXPECT_EQ(loaded.manifest.snapshot_hash, texas::MultiwayBlueprintArtifacts::snapshot_hash(snapshot));
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_p84_evaluation_adapter_selects_deterministic_request_local_candidates) {
    ResolverFixture fixture;
    texas::MultiwayBlueprintSnapshot blueprint;
    blueprint.identity = fixture.identity;
    blueprint.public_state = fixture.root.id;
    blueprint.infoset = {fixture.root.id, 0};
    blueprint.trajectories = 1U;
    blueprint.training.trajectories = 1U;
    blueprint.actions = {{fixture.root.legal_actions.front(), 65535U}};
    texas::MultiwayResolverEvaluationAdapterConfig config;
    config.resolver.buckets = &fixture.buckets;
    config.resolver.blueprint = &blueprint;
    config.candidates = {
        {11U, texas::MultiwayResolverEvaluationCandidateKind::StaticLegal},
        {12U, texas::MultiwayResolverEvaluationCandidateKind::BlueprintOnly},
        {13U, texas::MultiwayResolverEvaluationCandidateKind::SearchDisabled},
        {14U, texas::MultiwayResolverEvaluationCandidateKind::SearchEnabled},
    };
    texas::MultiwayResolverEvaluationAdapter adapter(config);
    const auto first = adapter.resolve(11U, fixture.request(), 7U);
    const auto second = adapter.resolve(11U, fixture.request(), 7U);
    const auto blueprint_only = adapter.resolve(12U, fixture.request(), 7U);
    const auto disabled = adapter.resolve(13U, fixture.request(), 7U);

    EXPECT_EQ(first.candidate_id, 11U);
    EXPECT_EQ(first.sampling_seed, second.sampling_seed);
    EXPECT_EQ(first.result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_TRUE(first.result.has_sampled_action);
    EXPECT_EQ(blueprint_only.result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::BlueprintFallback);
    EXPECT_EQ(disabled.result.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::LegacyDeterministicAdjustment);
    EXPECT_THROW(adapter.resolve(99U, fixture.request(), 7U), std::invalid_argument);
}

TEST_CASE(multiway_p84_evaluation_adapter_rejects_unsafe_candidate_configuration) {
    texas::MultiwayResolverEvaluationAdapterConfig empty;
    EXPECT_THROW(texas::MultiwayResolverEvaluationAdapter(empty), std::invalid_argument);

    texas::MultiwayResolverEvaluationAdapterConfig blueprint_only;
    blueprint_only.candidates = {{1U, texas::MultiwayResolverEvaluationCandidateKind::BlueprintOnly}};
    EXPECT_THROW(texas::MultiwayResolverEvaluationAdapter(blueprint_only), std::invalid_argument);

    texas::MultiwayResolverEvaluationAdapterConfig duplicate;
    duplicate.candidates = {
        {1U, texas::MultiwayResolverEvaluationCandidateKind::StaticLegal},
        {1U, texas::MultiwayResolverEvaluationCandidateKind::SearchDisabled},
    };
    EXPECT_THROW(texas::MultiwayResolverEvaluationAdapter(duplicate), std::invalid_argument);
}

#define P8_DIFFERENTIAL_CASE(name, helper, index) TEST_CASE(name) { helper(index); }

P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_01, run_terminal_differential_case, 1U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_02, run_terminal_differential_case, 2U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_03, run_terminal_differential_case, 3U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_04, run_terminal_differential_case, 4U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_05, run_terminal_differential_case, 5U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_06, run_terminal_differential_case, 6U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_07, run_terminal_differential_case, 7U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_08, run_terminal_differential_case, 8U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_09, run_terminal_differential_case, 9U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_10, run_terminal_differential_case, 10U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_11, run_terminal_differential_case, 11U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_12, run_terminal_differential_case, 12U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_13, run_terminal_differential_case, 13U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_14, run_terminal_differential_case, 14U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_15, run_terminal_differential_case, 15U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_16, run_terminal_differential_case, 16U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_17, run_terminal_differential_case, 17U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_18, run_terminal_differential_case, 18U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_19, run_terminal_differential_case, 19U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_20, run_terminal_differential_case, 20U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_21, run_terminal_differential_case, 21U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_22, run_terminal_differential_case, 22U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_23, run_terminal_differential_case, 23U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_24, run_terminal_differential_case, 24U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_25, run_terminal_differential_case, 25U)
P8_DIFFERENTIAL_CASE(multiway_p81_terminal_differential_26, run_terminal_differential_case, 26U)

P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_01, run_range_differential_case, 1U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_02, run_range_differential_case, 2U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_03, run_range_differential_case, 3U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_04, run_range_differential_case, 4U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_05, run_range_differential_case, 5U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_06, run_range_differential_case, 6U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_07, run_range_differential_case, 7U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_08, run_range_differential_case, 8U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_09, run_range_differential_case, 9U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_10, run_range_differential_case, 10U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_11, run_range_differential_case, 11U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_12, run_range_differential_case, 12U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_13, run_range_differential_case, 13U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_14, run_range_differential_case, 14U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_15, run_range_differential_case, 15U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_16, run_range_differential_case, 16U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_17, run_range_differential_case, 17U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_18, run_range_differential_case, 18U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_19, run_range_differential_case, 19U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_20, run_range_differential_case, 20U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_21, run_range_differential_case, 21U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_22, run_range_differential_case, 22U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_23, run_range_differential_case, 23U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_24, run_range_differential_case, 24U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_25, run_range_differential_case, 25U)
P8_DIFFERENTIAL_CASE(multiway_p82_range_differential_26, run_range_differential_case, 26U)

P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_01, run_artifact_hash_differential_case, 1U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_02, run_artifact_hash_differential_case, 2U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_03, run_artifact_hash_differential_case, 3U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_04, run_artifact_hash_differential_case, 4U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_05, run_artifact_hash_differential_case, 5U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_06, run_artifact_hash_differential_case, 6U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_07, run_artifact_hash_differential_case, 7U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_08, run_artifact_hash_differential_case, 8U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_09, run_artifact_hash_differential_case, 9U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_10, run_artifact_hash_differential_case, 10U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_11, run_artifact_hash_differential_case, 11U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_12, run_artifact_hash_differential_case, 12U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_13, run_artifact_hash_differential_case, 13U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_14, run_artifact_hash_differential_case, 14U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_15, run_artifact_hash_differential_case, 15U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_16, run_artifact_hash_differential_case, 16U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_17, run_artifact_hash_differential_case, 17U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_18, run_artifact_hash_differential_case, 18U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_19, run_artifact_hash_differential_case, 19U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_20, run_artifact_hash_differential_case, 20U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_21, run_artifact_hash_differential_case, 21U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_22, run_artifact_hash_differential_case, 22U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_23, run_artifact_hash_differential_case, 23U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_24, run_artifact_hash_differential_case, 24U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_25, run_artifact_hash_differential_case, 25U)
P8_DIFFERENTIAL_CASE(multiway_p83_artifact_differential_26, run_artifact_hash_differential_case, 26U)

P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_01, run_adapter_differential_case, 1U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_02, run_adapter_differential_case, 2U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_03, run_adapter_differential_case, 3U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_04, run_adapter_differential_case, 4U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_05, run_adapter_differential_case, 5U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_06, run_adapter_differential_case, 6U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_07, run_adapter_differential_case, 7U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_08, run_adapter_differential_case, 8U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_09, run_adapter_differential_case, 9U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_10, run_adapter_differential_case, 10U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_11, run_adapter_differential_case, 11U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_12, run_adapter_differential_case, 12U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_13, run_adapter_differential_case, 13U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_14, run_adapter_differential_case, 14U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_15, run_adapter_differential_case, 15U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_16, run_adapter_differential_case, 16U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_17, run_adapter_differential_case, 17U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_18, run_adapter_differential_case, 18U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_19, run_adapter_differential_case, 19U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_20, run_adapter_differential_case, 20U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_21, run_adapter_differential_case, 21U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_22, run_adapter_differential_case, 22U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_23, run_adapter_differential_case, 23U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_24, run_adapter_differential_case, 24U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_25, run_adapter_differential_case, 25U)
P8_DIFFERENTIAL_CASE(multiway_p84_adapter_differential_26, run_adapter_differential_case, 26U)

#undef P8_DIFFERENTIAL_CASE
