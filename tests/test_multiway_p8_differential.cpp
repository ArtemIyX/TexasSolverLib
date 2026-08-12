#include "games/multiway_fixed.hpp"
#include "games/multiway_private.hpp"
#include "games/multiway_terminal.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
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

core::MultiwayTerminalInput terminal_input() {
    core::MultiwayTerminalInput input;
    input.contributions = {101, 300, 300};
    input.folded = {false, false, false};
    input.strengths = {3U, 9U, 9U};
    input.odd_chip_first_seat = 2;
    return input;
}

core::MultiwayFixedTerminalInput fixed_input(const core::MultiwayTerminalInput& input) {
    core::MultiwayFixedTerminalInput fixed;
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
    core::MultiwayModelIdentity identity = core::make_multiway_model_identity(core::MultiwayBlueprintConfig{});
    core::MultiwayBucketRegistry buckets = core::build_multiway_baseline_bucket_registry(
        identity, {{core::Street::Flop, {0U, 5U, 9U}}});
    core::MultiwayPublicStateDescriptor root = make_root();

    static core::MultiwayPublicStateDescriptor make_root() {
        core::MultiwayGameConfig config;
        config.starting_stacks = {2'000, 2'000, 2'000};
        config.initial_contributions = {100, 100, 100};
        config.initial_street_contributions = {0, 0, 0};
        config.big_blind = 100;
        config.street = core::Street::Flop;
        const auto state = core::MultiwayState::initial(config);
        const auto menu = core::MultiwayActionAbstraction().make_legal_actions(state.snapshot(), 91U);
        return core::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    }

    core::MultiwayResolverRequest request() const {
        core::MultiwayResolverRequest result;
        result.blueprint_identity = identity;
        result.public_state = root;
        result.hero_seat = 0;
        result.hero_cards = {24U, 31U};
        result.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        result.sampling_seed = 73U;
        return result;
    }
};

}  // namespace

TEST_CASE(multiway_p81_terminal_fixed_and_dynamic_settlement_are_chip_exact) {
    const auto input = terminal_input();
    const auto direct = core::settle_multiway_terminal(input);
    core::MultiwayFixedTerminalScratch scratch;
    core::MultiwayFixedTerminalResult fixed_result;
    core::settle_multiway_terminal_fixed(fixed_input(input), scratch, fixed_result);

    EXPECT_EQ(fixed_result.rake_taken, direct.rake_taken);
    for (std::size_t seat = 0U; seat < input.contributions.size(); ++seat) {
        EXPECT_EQ(fixed_result.refunds[seat], direct.refunds[seat]);
        EXPECT_EQ(fixed_result.payouts[seat], direct.payouts[seat]);
        EXPECT_EQ(fixed_result.utilities[seat], direct.utilities[seat]);
    }
}

TEST_CASE(multiway_p82_range_belief_and_compiled_ranges_canonicalize_duplicates) {
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 3> entries = {{
        {{10U, 11U}, 1.0}, {{11U, 10U}, 2.0}, {{12U, 13U}, 3.0},
    }};
    const std::array<core::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U}, {entries.data(), entries.size(), nullptr, 0U},
    }};
    core::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto view = beliefs.view(0U);
    EXPECT_NEAR(view.weight(core::canonical_combos().id({10U, 11U})), 0.5, 1e-15);
    EXPECT_NEAR(view.weight(core::canonical_combos().id({12U, 13U})), 0.5, 1e-15);

    core::MultiwayPrivateConfig ranges;
    ranges.ranges = {
        {{{10U, 11U}, 1.0}, {{11U, 10U}, 2.0}},
        {{{12U, 13U}, 1.0}},
    };
    core::MultiwayCompiledPrivateRanges compiled(ranges);
    core::MultiwayPrivateWorkerScratch first;
    core::MultiwayPrivateWorkerScratch second;
    EXPECT_TRUE(compiled.try_sample_into(99U, first));
    EXPECT_TRUE(compiled.try_sample_into(99U, second));
    EXPECT_EQ(first.holes, second.holes);
    EXPECT_NEAR(first.proposal_reach, second.proposal_reach, 0.0);
}

TEST_CASE(multiway_p83_artifact_round_trip_preserves_identity_and_snapshot_hash) {
    core::MultiwayBlueprintConfig config;
    core::MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = core::make_multiway_model_identity(config);
    snapshot.public_state = {71U};
    snapshot.infoset = {{71U}, 0};
    snapshot.trajectories = 1U;
    snapshot.training.trajectories = 1U;
    snapshot.actions = {{{core::MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
    const auto path = std::filesystem::temp_directory_path() /
        ("texas_solver_p83_differential_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    core::MultiwayBlueprintArtifacts::save_atomic(path, snapshot);
    const auto loaded = core::MultiwayBlueprintArtifacts::load_verified(path, snapshot.identity);
    EXPECT_EQ(loaded.snapshot.identity, snapshot.identity);
    EXPECT_EQ(loaded.manifest.snapshot_hash, core::MultiwayBlueprintArtifacts::snapshot_hash(snapshot));
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_p84_evaluation_adapter_selects_deterministic_request_local_candidates) {
    ResolverFixture fixture;
    core::MultiwayBlueprintSnapshot blueprint;
    blueprint.identity = fixture.identity;
    blueprint.public_state = fixture.root.id;
    blueprint.infoset = {fixture.root.id, 0};
    blueprint.trajectories = 1U;
    blueprint.training.trajectories = 1U;
    blueprint.actions = {{fixture.root.legal_actions.front(), 65535U}};
    core::MultiwayResolverEvaluationAdapterConfig config;
    config.resolver.buckets = &fixture.buckets;
    config.resolver.blueprint = &blueprint;
    config.candidates = {
        {11U, core::MultiwayResolverEvaluationCandidateKind::StaticLegal},
        {12U, core::MultiwayResolverEvaluationCandidateKind::BlueprintOnly},
        {13U, core::MultiwayResolverEvaluationCandidateKind::SearchDisabled},
        {14U, core::MultiwayResolverEvaluationCandidateKind::SearchEnabled},
    };
    core::MultiwayResolverEvaluationAdapter adapter(config);
    const auto first = adapter.resolve(11U, fixture.request(), 7U);
    const auto second = adapter.resolve(11U, fixture.request(), 7U);
    const auto blueprint_only = adapter.resolve(12U, fixture.request(), 7U);
    const auto disabled = adapter.resolve(13U, fixture.request(), 7U);

    EXPECT_EQ(first.candidate_id, 11U);
    EXPECT_EQ(first.sampling_seed, second.sampling_seed);
    EXPECT_EQ(first.result.diagnostics.policy_provenance, core::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_TRUE(first.result.has_sampled_action);
    EXPECT_EQ(blueprint_only.result.diagnostics.policy_provenance, core::MultiwayPolicyProvenance::BlueprintFallback);
    EXPECT_EQ(disabled.result.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::LegacyDeterministicAdjustment);
    EXPECT_THROW(adapter.resolve(99U, fixture.request(), 7U), std::invalid_argument);
}
