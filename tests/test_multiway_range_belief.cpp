#include "solver/multiway_range_belief.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

core::CanonicalComboId combo_id(std::uint8_t first, std::uint8_t second) {
    return core::canonical_combos().id({first, second});
}

double total_weight(const core::MultiwayRangeBeliefView& view) {
    double total = 0.0;
    for (std::size_t id = 0U; id < view.size(); ++id) {
        total += view.weight(static_cast<core::CanonicalComboId>(id));
    }
    return total;
}

struct BeliefRowSnapshot {
    std::array<double, core::CANONICAL_HOLE_COMBINATION_COUNT> weights = {};
    core::CanonicalComboLegalMask legal_mask = {};
    core::MultiwayRangeBeliefMetadata metadata = {};
};

BeliefRowSnapshot snapshot(const core::MultiwayRangeBeliefView& view) {
    BeliefRowSnapshot result;
    result.legal_mask = view.legal_mask();
    result.metadata = view.metadata();
    for (std::size_t id = 0U; id < view.size(); ++id) {
        result.weights[id] = view.weight(static_cast<core::CanonicalComboId>(id));
    }
    return result;
}

void expect_same_row(
    const core::MultiwayRangeBeliefView& actual,
    const BeliefRowSnapshot& expected) {
    EXPECT_EQ(actual.legal_mask(), expected.legal_mask);
    EXPECT_EQ(actual.metadata().source, expected.metadata.source);
    EXPECT_EQ(actual.metadata().last_update_revision, expected.metadata.last_update_revision);
    EXPECT_EQ(actual.metadata().input_mass, expected.metadata.input_mass);
    EXPECT_EQ(actual.metadata().normalized_mass, expected.metadata.normalized_mass);
    for (std::size_t id = 0U; id < actual.size(); ++id) {
        EXPECT_EQ(actual.weight(static_cast<core::CanonicalComboId>(id)), expected.weights[id]);
    }
}

}  // namespace

TEST_CASE(multiway_range_beliefs_uniform_initialization_tracks_masks_and_metadata) {
    const std::array<std::uint8_t, 3> flop = {8U, 33U, 58U};
    const std::array<core::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {},
        {nullptr, 0U, flop.data(), flop.size()},
    }};
    core::MultiwayRangeBeliefs beliefs;
    beliefs.reset_uniform(seats.size(), seats.data());

    EXPECT_EQ(beliefs.seat_count(), std::size_t{2U});
    EXPECT_EQ(beliefs.revision(), 1U);
    const auto first = beliefs.view(0U);
    const auto second = beliefs.view(1U);
    EXPECT_TRUE(first.valid());
    EXPECT_TRUE(first.data() != nullptr);
    EXPECT_TRUE(first.data() != second.data());
    EXPECT_EQ(first.legal_mask().count(), core::CANONICAL_HOLE_COMBINATION_COUNT);
    EXPECT_EQ(second.legal_mask().count(), std::size_t{1176U});
    EXPECT_NEAR(first.weight(combo_id(8U, 9U)), 1.0 / 1326.0, 1e-15);
    EXPECT_NEAR(second.weight(combo_id(10U, 11U)), 1.0 / 1176.0, 1e-15);
    EXPECT_TRUE(!second.legal(combo_id(8U, 9U)));
    EXPECT_NEAR(second.weight(combo_id(8U, 9U)), 0.0, 0.0);
    EXPECT_NEAR(total_weight(first), 1.0, 1e-12);
    EXPECT_NEAR(total_weight(second), 1.0, 1e-12);
    EXPECT_EQ(first.metadata().source, core::MultiwayRangeBeliefSource::Uniform);
    EXPECT_EQ(second.metadata().last_update_revision, 1U);
    EXPECT_TRUE(!second.metadata().has_last_action);
    EXPECT_NEAR(second.metadata().input_mass, 1176.0, 0.0);
    EXPECT_NEAR(second.metadata().normalized_mass, 1.0, 0.0);

    std::array<core::MultiwayRangeBeliefSeatInput, 6> six_seats = {};
    beliefs.reset_uniform(six_seats.size(), six_seats.data());
    EXPECT_EQ(beliefs.seat_count(), std::size_t{6U});
    EXPECT_EQ(beliefs.revision(), 2U);
    for (std::size_t seat = 0U; seat < six_seats.size(); ++seat) {
        const auto view = beliefs.view(seat);
        EXPECT_EQ(view.legal_mask().count(), core::CANONICAL_HOLE_COMBINATION_COUNT);
        EXPECT_NEAR(total_weight(view), 1.0, 1e-12);
        EXPECT_EQ(view.metadata().last_update_revision, 2U);
    }
}

TEST_CASE(multiway_range_beliefs_supplied_rows_merge_duplicates_and_expose_legal_input_mass) {
    const std::array<std::uint8_t, 1> dead = {8U};
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 4> first_entries = {{
        {{8U, 9U}, 9.0},
        {{10U, 11U}, 2.0},
        {{11U, 10U}, 3.0},
        {{12U, 13U}, 5.0},
    }};
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> second_entries = {{
        {{14U, 15U}, 7.0},
    }};
    const std::array<core::MultiwayRangeBeliefSeatInput, 2> seats = {{
        {first_entries.data(), first_entries.size(), dead.data(), dead.size()},
        {second_entries.data(), second_entries.size(), nullptr, 0U},
    }};
    core::MultiwayRangeBeliefs beliefs;
    beliefs.reset_supplied(seats.size(), seats.data());

    const auto first = beliefs.view(0U);
    const auto second = beliefs.view(1U);
    EXPECT_EQ(first.metadata().source, core::MultiwayRangeBeliefSource::Supplied);
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
    const std::array<core::MultiwayRangeBeliefSeatInput, 2> uniform_seats = {};
    core::MultiwayRangeBeliefs beliefs;
    beliefs.reset_uniform(uniform_seats.size(), uniform_seats.data());
    const auto prior_revision = beliefs.revision();
    const auto prior_first = snapshot(beliefs.view(0U));
    const auto prior_second = snapshot(beliefs.view(1U));

    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> valid_entries = {{{{10U, 11U}, 1.0}}};
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> zero_entries = {{{{12U, 13U}, 0.0}}};
    std::array<core::MultiwayRangeBeliefSeatInput, 2> supplied = {{
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
        std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> invalid_entries = {{{{10U, 11U}, weight}}};
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
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> blocked_entries = {{{{8U, 9U}, 1.0}}};
    supplied[1] = {blocked_entries.data(), blocked_entries.size(), dead.data(), dead.size()};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);

    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> invalid_hole_entries = {{{{0U, 9U}, 1.0}}};
    supplied[1] = {invalid_hole_entries.data(), invalid_hole_entries.size(), nullptr, 0U};
    EXPECT_THROW(beliefs.reset_supplied(supplied.size(), supplied.data()), std::invalid_argument);
    EXPECT_EQ(beliefs.revision(), prior_revision);
    expect_same_row(beliefs.view(0U), prior_first);
    expect_same_row(beliefs.view(1U), prior_second);
}

TEST_CASE(multiway_range_beliefs_copy_rows_and_enforce_view_bounds) {
    core::MultiwayRangeBeliefs source;
    EXPECT_THROW(source.view(0U), std::out_of_range);

    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> first_entries = {{{{10U, 11U}, 2.0}}};
    const std::array<core::MultiwayRangeBeliefSuppliedEntry, 1> second_entries = {{{{12U, 13U}, 3.0}}};
    const std::array<core::MultiwayRangeBeliefSeatInput, 2> source_seats = {{
        {first_entries.data(), first_entries.size(), nullptr, 0U},
        {second_entries.data(), second_entries.size(), nullptr, 0U},
    }};
    source.reset_supplied(source_seats.size(), source_seats.data());

    core::MultiwayRangeBeliefs copied;
    copied.copy_from(source);
    EXPECT_EQ(copied.seat_count(), source.seat_count());
    EXPECT_EQ(copied.revision(), source.revision());
    EXPECT_TRUE(copied.view(0U).data() != source.view(0U).data());
    expect_same_row(copied.view(0U), snapshot(source.view(0U)));
    expect_same_row(copied.view(1U), snapshot(source.view(1U)));
    EXPECT_THROW(copied.view(2U), std::out_of_range);
    EXPECT_THROW(copied.view(0U).weight(static_cast<core::CanonicalComboId>(1326U)), std::out_of_range);
    EXPECT_THROW(copied.view(0U).legal(static_cast<core::CanonicalComboId>(1326U)), std::out_of_range);

    const std::array<core::MultiwayRangeBeliefSeatInput, 2> uniform_seats = {};
    source.reset_uniform(uniform_seats.size(), uniform_seats.data());
    EXPECT_NEAR(copied.view(0U).weight(combo_id(10U, 11U)), 1.0, 1e-15);
    copied.copy_from(copied);
    EXPECT_EQ(copied.revision(), 1U);
    EXPECT_NEAR(copied.view(1U).weight(combo_id(12U, 13U)), 1.0, 1e-15);
}
