#include "solver/multiway/session/multiway_range_belief.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

texas::CanonicalComboId combo_id(std::uint8_t first, std::uint8_t second) {
    return texas::canonical_combos().id({first, second});
}

double total_weight(const texas::MultiwayRangeBeliefView& view) {
    double total = 0.0;
    for (std::size_t id = 0U; id < view.size(); ++id) {
        total += view.weight(static_cast<texas::CanonicalComboId>(id));
    }
    return total;
}

struct BeliefRowSnapshot {
    std::array<double, texas::CANONICAL_HOLE_COMBINATION_COUNT> weights = {};
    texas::CanonicalComboLegalMask legal_mask = {};
    texas::MultiwayRangeBeliefMetadata metadata = {};
};

BeliefRowSnapshot snapshot(const texas::MultiwayRangeBeliefView& view) {
    BeliefRowSnapshot result;
    result.legal_mask = view.legal_mask();
    result.metadata = view.metadata();
    for (std::size_t id = 0U; id < view.size(); ++id) {
        result.weights[id] = view.weight(static_cast<texas::CanonicalComboId>(id));
    }
    return result;
}

void expect_same_row(
    const texas::MultiwayRangeBeliefView& actual,
    const BeliefRowSnapshot& expected) {
    EXPECT_EQ(actual.legal_mask(), expected.legal_mask);
    EXPECT_EQ(actual.metadata().source, expected.metadata.source);
    EXPECT_EQ(actual.metadata().last_update_revision, expected.metadata.last_update_revision);
    EXPECT_EQ(actual.metadata().observation.source, expected.metadata.observation.source);
    EXPECT_EQ(actual.metadata().observation.public_state_id, expected.metadata.observation.public_state_id);
    EXPECT_EQ(actual.metadata().observation.action_menu_id, expected.metadata.observation.action_menu_id);
    EXPECT_EQ(
        actual.metadata().observation.bucket_table_identity,
        expected.metadata.observation.bucket_table_identity);
    EXPECT_EQ(actual.metadata().observation.source_revision, expected.metadata.observation.source_revision);
    EXPECT_EQ(actual.metadata().observation.observed_action, expected.metadata.observation.observed_action);
    EXPECT_EQ(actual.metadata().observation.applied, expected.metadata.observation.applied);
    EXPECT_EQ(actual.metadata().input_mass, expected.metadata.input_mass);
    EXPECT_EQ(actual.metadata().normalized_mass, expected.metadata.normalized_mass);
    for (std::size_t id = 0U; id < actual.size(); ++id) {
        EXPECT_EQ(actual.weight(static_cast<texas::CanonicalComboId>(id)), expected.weights[id]);
    }
}

std::vector<std::uint32_t> two_bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(
        texas::MULTIWAY_HOLE_COMBINATION_COUNT, texas::MULTIWAY_INVALID_BUCKET);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            if (std::find(compact_board.begin(), compact_board.end(), first) != compact_board.end() ||
                std::find(compact_board.begin(), compact_board.end(), second) != compact_board.end()) {
                continue;
            }
            const std::array<std::uint8_t, 2> hole = {first, second};
            assignments[texas::MultiwayBucketTable::hole_index(hole)] =
                static_cast<std::uint32_t>(texas::MultiwayBucketTable::hole_index(hole) % 2U);
        }
    }
    return assignments;
}

texas::MultiwayBucketActionPolicy make_policy(
    const texas::MultiwayModelIdentity& identity,
    std::uint64_t table_identity) {
    texas::MultiwayBucketActionPolicy policy;
    policy.identity = identity;
    policy.public_state = {91U};
    policy.action_menu_id = 53U;
    policy.bucket_table_identity = table_identity;
    policy.bucket_count = 2U;
    policy.action_count = 2U;
    policy.probabilities = {49151U, 16384U, 16384U, 49151U};
    return policy;
}

struct ObservationFixture {
    ObservationFixture()
        : identity(texas::make_multiway_model_identity(config)),
          table(identity, texas::Street::Flop, compact_board, 2U, two_bucket_assignments(compact_board)),
          policy(make_policy(identity, table.table_identity())) {}

    texas::MultiwayBlueprintConfig config;
    texas::MultiwayModelIdentity identity;
    const std::vector<std::uint8_t> compact_board = {0U, 1U, 2U};
    texas::MultiwayBucketTable table;
    texas::MultiwayBucketActionPolicy policy;
};

}  // namespace

TEST_CASE(multiway_range_beliefs_uniform_initialization_tracks_masks_and_metadata) {
    const std::array<std::uint8_t, 3> flop = {8U, 33U, 48U};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {},
        {nullptr, 0U, flop.data(), flop.size()},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_uniform(seats.size(), seats.data());

    EXPECT_EQ(beliefs.seat_count(), std::size_t{2U});
    EXPECT_EQ(beliefs.revision(), 1U);
    const auto first = beliefs.view(0U);
    const auto second = beliefs.view(1U);
    EXPECT_TRUE(first.valid());
    EXPECT_TRUE(first.data() != nullptr);
    EXPECT_TRUE(first.data() != second.data());
    EXPECT_EQ(first.legal_mask().count(), texas::CANONICAL_HOLE_COMBINATION_COUNT);
    EXPECT_EQ(second.legal_mask().count(), std::size_t{1176U});
    EXPECT_NEAR(first.weight(combo_id(8U, 9U)), 1.0 / 1326.0, 1e-15);
    EXPECT_NEAR(second.weight(combo_id(10U, 11U)), 1.0 / 1176.0, 1e-15);
    EXPECT_TRUE(!second.legal(combo_id(8U, 9U)));
    EXPECT_NEAR(second.weight(combo_id(8U, 9U)), 0.0, 0.0);
    EXPECT_NEAR(total_weight(first), 1.0, 1e-12);
    EXPECT_NEAR(total_weight(second), 1.0, 1e-12);
    EXPECT_EQ(first.metadata().source, texas::MultiwayRangeBeliefSource::Uniform);
    EXPECT_EQ(second.metadata().last_update_revision, 1U);
    EXPECT_TRUE(!second.metadata().observation.applied);
    EXPECT_NEAR(second.metadata().input_mass, 1176.0, 0.0);
    EXPECT_NEAR(second.metadata().normalized_mass, 1.0, 0.0);

    std::array<texas::MultiwayRangeBeliefSeatInput, 6> six_seats = {};
    beliefs.reset_uniform(six_seats.size(), six_seats.data());
    EXPECT_EQ(beliefs.seat_count(), std::size_t{6U});
    EXPECT_EQ(beliefs.revision(), 2U);
    for (std::size_t seat = 0U; seat < six_seats.size(); ++seat) {
        const auto view = beliefs.view(seat);
        EXPECT_EQ(view.legal_mask().count(), texas::CANONICAL_HOLE_COMBINATION_COUNT);
        EXPECT_NEAR(total_weight(view), 1.0, 1e-12);
        EXPECT_EQ(view.metadata().last_update_revision, 2U);
    }
}

TEST_CASE(multiway_range_beliefs_apply_exact_observation_posterior_and_metadata) {
    ObservationFixture fixture;
    const std::array<std::uint8_t, 3> hunl_board = {8U, 9U, 10U};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 2> entries = {{
        {{11U, 12U}, 0.25},
        {{11U, 13U}, 0.75},
    }};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), hunl_board.data(), hunl_board.size()},
        {entries.data(), entries.size(), hunl_board.data(), hunl_board.size()},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto untouched_second = snapshot(beliefs.view(1U));
    const texas::MultiwayRangeBeliefObservation observation(
        fixture.table, fixture.policy, texas::MultiwayRangeBeliefSource::Search, 0U, 71U);
    const auto likelihood_zero = fixture.policy.likelihood(0U, 0U);
    const auto likelihood_one = fixture.policy.likelihood(1U, 0U);
    const auto posterior_mass = 0.25 * likelihood_zero + 0.75 * likelihood_one;

    EXPECT_EQ(
        beliefs.apply_observation(0U, observation),
        texas::MultiwayRangeBeliefUpdateResult::Applied);
    const auto updated = beliefs.view(0U);
    EXPECT_NEAR(updated.weight(combo_id(11U, 12U)), 0.25 * likelihood_zero / posterior_mass, 1e-15);
    EXPECT_NEAR(updated.weight(combo_id(11U, 13U)), 0.75 * likelihood_one / posterior_mass, 1e-15);
    EXPECT_NEAR(total_weight(updated), 1.0, 1e-12);
    EXPECT_EQ(beliefs.revision(), 2U);
    EXPECT_EQ(updated.metadata().source, texas::MultiwayRangeBeliefSource::Search);
    EXPECT_EQ(updated.metadata().last_update_revision, 2U);
    EXPECT_EQ(updated.metadata().observation.source, texas::MultiwayRangeBeliefSource::Search);
    EXPECT_EQ(updated.metadata().observation.public_state_id, 91U);
    EXPECT_EQ(updated.metadata().observation.action_menu_id, 53U);
    EXPECT_EQ(updated.metadata().observation.bucket_table_identity, fixture.table.table_identity());
    EXPECT_EQ(updated.metadata().observation.source_revision, 71U);
    EXPECT_EQ(updated.metadata().observation.observed_action, 0U);
    EXPECT_TRUE(updated.metadata().observation.applied);
    EXPECT_NEAR(updated.metadata().input_mass, posterior_mass, 1e-15);
    EXPECT_NEAR(updated.metadata().normalized_mass, 1.0, 0.0);
    expect_same_row(beliefs.view(1U), untouched_second);
}

TEST_CASE(multiway_range_beliefs_bind_observations_to_exact_table_content) {
    ObservationFixture fixture;
    auto changed_assignments = two_bucket_assignments(fixture.compact_board);
    const std::array<std::uint8_t, 2> changed_compact_hole = {3U, 4U};
    const auto changed_index = texas::MultiwayBucketTable::hole_index(changed_compact_hole);
    changed_assignments[changed_index] = 1U;
    const texas::MultiwayBucketTable changed_table(
        fixture.identity, texas::Street::Flop, fixture.compact_board, 2U, std::move(changed_assignments));
    EXPECT_TRUE(changed_table.table_identity() != fixture.table.table_identity());

    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> entries = {{{{11U, 12U}, 1.0}}};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U},
        {entries.data(), entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto before = snapshot(beliefs.view(0U));
    const auto before_revision = beliefs.revision();

    const texas::MultiwayRangeBeliefObservation stale_binding(
        changed_table, fixture.policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 1U);
    EXPECT_THROW(beliefs.apply_observation(0U, stale_binding), std::invalid_argument);
    EXPECT_EQ(beliefs.revision(), before_revision);
    expect_same_row(beliefs.view(0U), before);

    const auto changed_policy = make_policy(fixture.identity, changed_table.table_identity());
    const texas::MultiwayRangeBeliefObservation verified_binding(
        changed_table, changed_policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 2U);
    EXPECT_EQ(
        beliefs.apply_observation(0U, verified_binding),
        texas::MultiwayRangeBeliefUpdateResult::Applied);
    EXPECT_EQ(
        beliefs.view(0U).metadata().observation.bucket_table_identity,
        changed_table.table_identity());
}

TEST_CASE(multiway_range_beliefs_observation_zero_mass_and_validation_are_transactional) {
    ObservationFixture fixture;
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> entries = {{{{11U, 12U}, 1.0}}};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U},
        {entries.data(), entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const auto before = snapshot(beliefs.view(0U));
    const auto before_revision = beliefs.revision();

    auto zero_policy = fixture.policy;
    zero_policy.probabilities = {0U, 0U, 65535U, 65535U};
    const texas::MultiwayRangeBeliefObservation zero_observation(
        fixture.table, zero_policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 1U);
    EXPECT_EQ(
        beliefs.apply_observation(0U, zero_observation),
        texas::MultiwayRangeBeliefUpdateResult::NoPosteriorMass);
    EXPECT_EQ(beliefs.revision(), before_revision);
    expect_same_row(beliefs.view(0U), before);

    const auto expect_invalid = [&beliefs, &before, before_revision](
                                    const texas::MultiwayRangeBeliefObservation& observation) {
        EXPECT_THROW(beliefs.apply_observation(0U, observation), std::invalid_argument);
        EXPECT_EQ(beliefs.revision(), before_revision);
        expect_same_row(beliefs.view(0U), before);
    };
    const texas::MultiwayRangeBeliefObservation invalid_source(
        fixture.table, fixture.policy, texas::MultiwayRangeBeliefSource::Uniform, 0U, 1U);
    expect_invalid(invalid_source);

    const texas::MultiwayRangeBeliefObservation invalid_action(
        fixture.table, fixture.policy, texas::MultiwayRangeBeliefSource::Blueprint, 2U, 1U);
    expect_invalid(invalid_action);

    auto malformed_policy = fixture.policy;
    malformed_policy.probabilities[0] = 0U;
    const texas::MultiwayRangeBeliefObservation malformed(
        fixture.table, malformed_policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 1U);
    expect_invalid(malformed);

    auto wrong_table_policy = fixture.policy;
    ++wrong_table_policy.bucket_table_identity;
    const texas::MultiwayRangeBeliefObservation wrong_table(
        fixture.table, wrong_table_policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 1U);
    expect_invalid(wrong_table);

    auto wrong_identity_policy = fixture.policy;
    texas::MultiwayBlueprintConfig other_config;
    other_config.player_count = 2U;
    wrong_identity_policy.identity = texas::make_multiway_model_identity(other_config);
    const texas::MultiwayRangeBeliefObservation wrong_identity(
        fixture.table, wrong_identity_policy, texas::MultiwayRangeBeliefSource::Blueprint, 0U, 1U);
    expect_invalid(wrong_identity);
    EXPECT_THROW(beliefs.apply_observation(2U, zero_observation), std::out_of_range);
}

TEST_CASE(multiway_range_beliefs_observation_prunes_table_blockers_and_updates_repeatedly) {
    ObservationFixture fixture;
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 2> entries = {{
        {{0U, 11U}, 0.4},
        {{11U, 12U}, 0.6},
    }};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {entries.data(), entries.size(), nullptr, 0U},
        {entries.data(), entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());
    const texas::MultiwayRangeBeliefObservation first_observation(
        fixture.table, fixture.policy, texas::MultiwayRangeBeliefSource::Translated, 0U, 5U);
    EXPECT_EQ(
        beliefs.apply_observation(0U, first_observation),
        texas::MultiwayRangeBeliefUpdateResult::Applied);
    auto first = beliefs.view(0U);
    EXPECT_TRUE(!first.legal(combo_id(0U, 11U)));
    EXPECT_NEAR(first.weight(combo_id(0U, 11U)), 0.0, 0.0);
    EXPECT_NEAR(first.weight(combo_id(11U, 12U)), 1.0, 1e-15);
    EXPECT_EQ(first.metadata().observation.source, texas::MultiwayRangeBeliefSource::Translated);
    EXPECT_EQ(first.metadata().observation.source_revision, 5U);

    const texas::MultiwayRangeBeliefObservation second_observation(
        fixture.table, fixture.policy, texas::MultiwayRangeBeliefSource::Fallback, 1U, 6U);
    EXPECT_EQ(
        beliefs.apply_observation(0U, second_observation),
        texas::MultiwayRangeBeliefUpdateResult::Applied);
    const auto second = beliefs.view(0U);
    EXPECT_NEAR(second.weight(combo_id(11U, 12U)), 1.0, 1e-15);
    EXPECT_EQ(beliefs.revision(), 3U);
    EXPECT_EQ(second.metadata().last_update_revision, 3U);
    EXPECT_EQ(second.metadata().observation.source, texas::MultiwayRangeBeliefSource::Fallback);
    EXPECT_EQ(second.metadata().observation.source_revision, 6U);
    EXPECT_EQ(second.metadata().observation.observed_action, 1U);
    EXPECT_EQ(beliefs.view(1U).metadata().last_update_revision, 1U);
    EXPECT_EQ(beliefs.view(1U).metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
}

TEST_CASE(multiway_range_beliefs_supplied_rows_merge_duplicates_and_expose_legal_input_mass) {
    const std::array<std::uint8_t, 1> dead = {8U};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 4> first_entries = {{
        {{8U, 9U}, 9.0},
        {{10U, 11U}, 2.0},
        {{11U, 10U}, 3.0},
        {{12U, 13U}, 5.0},
    }};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> second_entries = {{
        {{14U, 15U}, 7.0},
    }};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {first_entries.data(), first_entries.size(), dead.data(), dead.size()},
        {second_entries.data(), second_entries.size(), nullptr, 0U},
    }};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());

    const auto first = beliefs.view(0U);
    const auto second = beliefs.view(1U);
    EXPECT_EQ(first.metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
    EXPECT_NEAR(first.metadata().input_mass, 10.0, 0.0);
    EXPECT_NEAR(first.metadata().normalized_mass, 1.0, 0.0);
    EXPECT_TRUE(!first.legal(combo_id(8U, 9U)));
    EXPECT_NEAR(first.weight(combo_id(8U, 9U)), 0.0, 0.0);
    EXPECT_NEAR(first.weight(combo_id(10U, 11U)), 0.5, 1e-15);
    EXPECT_NEAR(first.weight(combo_id(12U, 13U)), 0.5, 1e-15);
    EXPECT_NEAR(second.weight(combo_id(14U, 15U)), 1.0, 1e-15);
    EXPECT_NEAR(total_weight(first), 1.0, 1e-12);
    EXPECT_NEAR(total_weight(second), 1.0, 1e-12);
}

TEST_CASE(multiway_range_beliefs_reject_invalid_supplied_rows_transactionally) {
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> uniform_seats = {};
    texas::MultiwayRangeBeliefs beliefs;
    beliefs.reset_uniform(uniform_seats.size(), uniform_seats.data());
    const auto prior_revision = beliefs.revision();
    const auto prior_first = snapshot(beliefs.view(0U));
    const auto prior_second = snapshot(beliefs.view(1U));

    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> valid_entries = {{{{10U, 11U}, 1.0}}};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> zero_entries = {{{{12U, 13U}, 0.0}}};
    std::array<texas::MultiwayRangeBeliefSeatInput, 2> supplied = {{
        {valid_entries.data(), valid_entries.size(), nullptr, 0U},
        {zero_entries.data(), zero_entries.size(), nullptr, 0U},
    }};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
    EXPECT_EQ(beliefs.revision(), prior_revision);
    expect_same_row(beliefs.view(0U), prior_first);
    expect_same_row(beliefs.view(1U), prior_second);

    const std::array<double, 3> invalid_weights = {
        -1.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
    };
    for (const auto weight : invalid_weights) {
        std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> invalid_entries = {{{{10U, 11U}, weight}}};
        supplied[1] = {invalid_entries.data(), invalid_entries.size(), nullptr, 0U};
        EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
        EXPECT_EQ(beliefs.revision(), prior_revision);
    }

    supplied[1] = {nullptr, 1U, nullptr, 0U};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
    supplied[1] = {valid_entries.data(), 0U, nullptr, 0U};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
    EXPECT_THROW(beliefs.reset_uniform(1U, uniform_seats.data()), std::invalid_argument);
    EXPECT_THROW(beliefs.reset_uniform(7U, uniform_seats.data()), std::invalid_argument);
    EXPECT_THROW(beliefs.reset_uniform(2U, nullptr), std::invalid_argument);
    supplied[1] = {valid_entries.data(), valid_entries.size(), nullptr, 1U};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);

    const std::array<std::uint8_t, 1> dead = {8U};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> blocked_entries = {{{{8U, 9U}, 1.0}}};
    supplied[1] = {blocked_entries.data(), blocked_entries.size(), dead.data(), dead.size()};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);

    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> invalid_hole_entries = {{{{52U, 9U}, 1.0}}};
    supplied[1] = {invalid_hole_entries.data(), invalid_hole_entries.size(), nullptr, 0U};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
    EXPECT_EQ(beliefs.revision(), prior_revision);
    expect_same_row(beliefs.view(0U), prior_first);
    expect_same_row(beliefs.view(1U), prior_second);
}

TEST_CASE(multiway_range_beliefs_copy_rows_and_enforce_view_bounds) {
    texas::MultiwayRangeBeliefs source;
    EXPECT_THROW(source.view(0U), std::out_of_range);

    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> first_entries = {{{{10U, 11U}, 2.0}}};
    const std::array<texas::MultiwayRangeBeliefSuppliedEntry, 1> second_entries = {{{{12U, 13U}, 3.0}}};
    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> source_seats = {{
        {first_entries.data(), first_entries.size(), nullptr, 0U},
        {second_entries.data(), second_entries.size(), nullptr, 0U},
    }};
    source.reset_supplied(source_seats.size(), source_seats.data());

    texas::MultiwayRangeBeliefs copied;
    copied.copy_from(source);
    EXPECT_EQ(copied.seat_count(), source.seat_count());
    EXPECT_EQ(copied.revision(), source.revision());
    EXPECT_TRUE(copied.view(0U).data() != source.view(0U).data());
    expect_same_row(copied.view(0U), snapshot(source.view(0U)));
    expect_same_row(copied.view(1U), snapshot(source.view(1U)));
    EXPECT_THROW(copied.view(2U), std::out_of_range);
    EXPECT_THROW(copied.view(0U).weight(static_cast<texas::CanonicalComboId>(1326U)), std::out_of_range);
    EXPECT_THROW(copied.view(0U).legal(static_cast<texas::CanonicalComboId>(1326U)), std::out_of_range);

    const std::array<texas::MultiwayRangeBeliefSeatInput, 2> uniform_seats = {};
    source.reset_uniform(uniform_seats.size(), uniform_seats.data());
    EXPECT_NEAR(copied.view(0U).weight(combo_id(10U, 11U)), 1.0, 1e-15);
    copied.copy_from(copied);
    EXPECT_EQ(copied.revision(), 1U);
    EXPECT_NEAR(copied.view(1U).weight(combo_id(12U, 13U)), 1.0, 1e-15);
}
