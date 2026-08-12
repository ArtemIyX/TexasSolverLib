#include "solver/multiway_search_profile.hpp"
#include "test_harness.hpp"

#include <cstddef>

namespace {

using Mode = texas::MultiwaySearchProfileMode;
using Stage = texas::MultiwaySearchProfileStage;

void expect_single_checkpoint(Stage stage, std::uint64_t elapsed, std::uint64_t calls) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(stage, elapsed, calls);
    const auto snapshot = profile.snapshot();
    EXPECT_EQ(snapshot.checkpoint(stage).elapsed_nanoseconds, elapsed);
    EXPECT_EQ(snapshot.checkpoint(stage).calls, calls);
}

}  // namespace

TEST_CASE(multiway_p7_profile_defaults_to_disabled) {
    const texas::MultiwaySearchProfile profile;
    EXPECT_TRUE(!profile.enabled());
    EXPECT_TRUE(!profile.snapshot().profiled());
}

TEST_CASE(multiway_p7_profile_checkpoints_mode_is_enabled) {
    const texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    EXPECT_TRUE(profile.enabled());
    EXPECT_TRUE(profile.snapshot().profiled());
}

TEST_CASE(multiway_p7_profile_disabled_add_is_ignored) {
    texas::MultiwaySearchProfile profile;
    profile.add(Stage::RowLookup, 41U, 3U);
    const auto checkpoint = profile.snapshot().checkpoint(Stage::RowLookup);
    EXPECT_EQ(checkpoint.elapsed_nanoseconds, 0U);
    EXPECT_EQ(checkpoint.calls, 0U);
}

TEST_CASE(multiway_p7_profile_reset_enables_and_clears) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::DeltaMerge, 99U, 4U);
    profile.reset(Mode::Checkpoints);
    EXPECT_TRUE(profile.enabled());
    EXPECT_EQ(profile.snapshot().checkpoint(Stage::DeltaMerge).calls, 0U);
}

TEST_CASE(multiway_p7_profile_reset_disables_and_clears) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::RootExport, 99U, 4U);
    profile.reset(Mode::Disabled);
    EXPECT_TRUE(!profile.enabled());
    EXPECT_EQ(profile.snapshot().checkpoint(Stage::RootExport).elapsed_nanoseconds, 0U);
}

TEST_CASE(multiway_p7_profile_merge_ignores_disabled_snapshot) {
    texas::MultiwaySearchProfile source;
    texas::MultiwaySearchProfile target(Mode::Checkpoints);
    target.add(Stage::RegretMatching, 7U, 1U);
    target.merge(source.snapshot());
    EXPECT_EQ(target.snapshot().checkpoint(Stage::RegretMatching).elapsed_nanoseconds, 7U);
}

TEST_CASE(multiway_p7_profile_merge_enables_disabled_target) {
    texas::MultiwaySearchProfile source(Mode::Checkpoints);
    source.add(Stage::TerminalSettlement, 13U, 2U);
    texas::MultiwaySearchProfile target;
    target.merge(source.snapshot());
    EXPECT_TRUE(target.enabled());
    EXPECT_EQ(target.snapshot().checkpoint(Stage::TerminalSettlement).calls, 2U);
}

TEST_CASE(multiway_p7_profile_merge_accumulates_elapsed_time) {
    texas::MultiwaySearchProfile source(Mode::Checkpoints);
    source.add(Stage::ContinuationLeaf, 23U, 1U);
    texas::MultiwaySearchProfile target(Mode::Checkpoints);
    target.add(Stage::ContinuationLeaf, 19U, 1U);
    target.merge(source.snapshot());
    EXPECT_EQ(target.snapshot().checkpoint(Stage::ContinuationLeaf).elapsed_nanoseconds, 42U);
}

TEST_CASE(multiway_p7_profile_merge_accumulates_calls) {
    texas::MultiwaySearchProfile source(Mode::Checkpoints);
    source.add(Stage::ActionMenuGeneration, 1U, 5U);
    texas::MultiwaySearchProfile target(Mode::Checkpoints);
    target.add(Stage::ActionMenuGeneration, 1U, 7U);
    target.merge(source.snapshot());
    EXPECT_EQ(target.snapshot().checkpoint(Stage::ActionMenuGeneration).calls, 12U);
}

TEST_CASE(multiway_p7_profile_repeated_add_accumulates) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::PublicGraphAdmission, 8U, 2U);
    profile.add(Stage::PublicGraphAdmission, 9U, 3U);
    const auto checkpoint = profile.snapshot().checkpoint(Stage::PublicGraphAdmission);
    EXPECT_EQ(checkpoint.elapsed_nanoseconds, 17U);
    EXPECT_EQ(checkpoint.calls, 5U);
}

TEST_CASE(multiway_p7_profile_zero_call_add_preserves_call_count) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::PrivateDealSampling, 15U, 0U);
    const auto checkpoint = profile.snapshot().checkpoint(Stage::PrivateDealSampling);
    EXPECT_EQ(checkpoint.elapsed_nanoseconds, 15U);
    EXPECT_EQ(checkpoint.calls, 0U);
}

TEST_CASE(multiway_p7_profile_snapshot_is_value_copy) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::RowLookup, 3U, 1U);
    const auto first = profile.snapshot();
    profile.add(Stage::RowLookup, 4U, 1U);
    EXPECT_EQ(first.checkpoint(Stage::RowLookup).elapsed_nanoseconds, 3U);
}

TEST_CASE(multiway_p7_profile_ranking_orders_descending_elapsed_time) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::RowLookup, 20U);
    profile.add(Stage::DeltaMerge, 90U);
    profile.add(Stage::ContinuationLeaf, 40U);
    const auto ranking = texas::rank_multiway_search_profile(profile.snapshot());
    EXPECT_EQ(ranking[0].stage, Stage::DeltaMerge);
    EXPECT_EQ(ranking[1].stage, Stage::ContinuationLeaf);
    EXPECT_EQ(ranking[2].stage, Stage::RowLookup);
}

TEST_CASE(multiway_p7_profile_ranking_retains_time_values) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::RootExport, 73U);
    const auto ranking = texas::rank_multiway_search_profile(profile.snapshot());
    EXPECT_EQ(ranking[0].elapsed_nanoseconds, 73U);
}

TEST_CASE(multiway_p7_profile_ranking_is_stable_for_ties) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    profile.add(Stage::PrivateDealSampling, 5U);
    profile.add(Stage::PublicChanceSampling, 5U);
    const auto ranking = texas::rank_multiway_search_profile(profile.snapshot());
    EXPECT_EQ(ranking[0].stage, Stage::PrivateDealSampling);
    EXPECT_EQ(ranking[1].stage, Stage::PublicChanceSampling);
}

TEST_CASE(multiway_p7_profile_zero_ranking_uses_numeric_stage_order) {
    const auto ranking = texas::rank_multiway_search_profile({});
    for (std::size_t index = 0U; index < ranking.size(); ++index) {
        EXPECT_EQ(ranking[index].stage, static_cast<Stage>(index));
    }
}

TEST_CASE(multiway_p7_profile_null_scope_is_safe) {
    { texas::MultiwaySearchProfileScope scope(nullptr, Stage::RowLookup); }
    EXPECT_TRUE(true);
}

TEST_CASE(multiway_p7_profile_disabled_scope_records_nothing) {
    texas::MultiwaySearchProfile profile;
    { texas::MultiwaySearchProfileScope scope(&profile, Stage::RegretMatching); }
    EXPECT_EQ(profile.snapshot().checkpoint(Stage::RegretMatching).calls, 0U);
}

TEST_CASE(multiway_p7_profile_enabled_scope_records_one_call) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    { texas::MultiwaySearchProfileScope scope(&profile, Stage::TerminalSettlement); }
    EXPECT_EQ(profile.snapshot().checkpoint(Stage::TerminalSettlement).calls, 1U);
}

TEST_CASE(multiway_p7_profile_scope_respects_disable_before_destruction) {
    texas::MultiwaySearchProfile profile(Mode::Checkpoints);
    {
        texas::MultiwaySearchProfileScope scope(&profile, Stage::RootExport);
        profile.reset(Mode::Disabled);
    }
    EXPECT_EQ(profile.snapshot().checkpoint(Stage::RootExport).calls, 0U);
}

TEST_CASE(multiway_p7_profile_stage_count_matches_public_enum) {
    EXPECT_EQ(texas::MULTIWAY_SEARCH_PROFILE_STAGE_COUNT, std::size_t{10U});
    EXPECT_EQ(static_cast<std::size_t>(Stage::Count), std::size_t{10U});
}

TEST_CASE(multiway_p7_profile_records_private_deal_sampling_slot) {
    expect_single_checkpoint(Stage::PrivateDealSampling, 11U, 1U);
}

TEST_CASE(multiway_p7_profile_records_public_chance_sampling_slot) {
    expect_single_checkpoint(Stage::PublicChanceSampling, 12U, 2U);
}

TEST_CASE(multiway_p7_profile_records_action_menu_generation_slot) {
    expect_single_checkpoint(Stage::ActionMenuGeneration, 13U, 3U);
}

TEST_CASE(multiway_p7_profile_records_public_graph_admission_slot) {
    expect_single_checkpoint(Stage::PublicGraphAdmission, 14U, 4U);
}

TEST_CASE(multiway_p7_profile_records_row_lookup_slot) {
    expect_single_checkpoint(Stage::RowLookup, 15U, 5U);
}

TEST_CASE(multiway_p7_profile_records_regret_matching_slot) {
    expect_single_checkpoint(Stage::RegretMatching, 16U, 6U);
}

TEST_CASE(multiway_p7_profile_records_terminal_settlement_slot) {
    expect_single_checkpoint(Stage::TerminalSettlement, 17U, 7U);
}

TEST_CASE(multiway_p7_profile_records_continuation_leaf_slot) {
    expect_single_checkpoint(Stage::ContinuationLeaf, 18U, 8U);
}

TEST_CASE(multiway_p7_profile_records_delta_merge_slot) {
    expect_single_checkpoint(Stage::DeltaMerge, 19U, 9U);
}

TEST_CASE(multiway_p7_profile_records_root_export_slot) {
    expect_single_checkpoint(Stage::RootExport, 20U, 10U);
}
