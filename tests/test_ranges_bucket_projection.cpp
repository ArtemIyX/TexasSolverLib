#include "ranges/bucket_projection.hpp"
#include "ranges/source.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace {

TEST_CASE(ranges_bucket_projection_preserves_total_probability_mass) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_mass.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](texas::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, texas::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto combos = texas::enumerate_combos(config->initial_board);
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto canonical = texas::make_canonical_range_from_values(texas::RangeSourceKind::UniformPrior, range, mask);

    const auto projection = texas::project_combo_range_to_buckets(
        canonical,
        combos,
        texas::load_abstraction(path),
        texas::Street::River);

    EXPECT_NEAR(projection.input_mass, 1.0, 1e-12);
    EXPECT_NEAR(projection.projected_mass, 1.0, 1e-12);
    EXPECT_NEAR(projection.dropped_mass, 0.0, 1e-12);
    EXPECT_NEAR(projection.bucket_range.range.sum(), 1.0, 1e-12);
    std::filesystem::remove(path);
}

TEST_CASE(ranges_bucket_weights_match_sum_of_member_combos) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_sums.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](texas::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, texas::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto tables = texas::load_abstraction(path);
    const auto combos = texas::enumerate_combos(config->initial_board);
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 0.0);
    double bucket_totals[2] = {0.0, 0.0};
    for (std::size_t i = 0; i < combos.size(); ++i) {
        range.weights[i] = static_cast<double>((i % 3U) + 1U);
        const auto bucket = texas::lookup_bucket(tables, config->initial_board, combos.hands[i], texas::Street::River);
        bucket_totals[bucket] += range.weights[i];
    }
    range.normalize();
    const double total = bucket_totals[0] + bucket_totals[1];

    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto projection = texas::project_combo_range_to_buckets(
        texas::make_canonical_range_from_values(texas::RangeSourceKind::UniformPrior, range, mask),
        combos,
        tables,
        texas::Street::River);

    EXPECT_NEAR(projection.bucket_range.range.weights[0], bucket_totals[0] / total, 1e-12);
    EXPECT_NEAR(projection.bucket_range.range.weights[1], bucket_totals[1] / total, 1e-12);
    std::filesystem::remove(path);
}

TEST_CASE(ranges_exact_and_bucketed_totals_agree_on_toy_projection) {
    const auto config = std::make_shared<const texas::HUNLConfig>(texas::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_totals.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](texas::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, texas::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto combos = texas::enumerate_combos(config->initial_board);
    texas::RangeVector range;
    range.kind = texas::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    texas::RangeMask mask;
    mask.kind = texas::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);

    const auto bucket_weights = texas::combo_weights_to_bucket_weights(
        range.weights,
        combos,
        texas::load_abstraction(path),
        texas::Street::River);

    EXPECT_NEAR(range.sum(), 1.0, 1e-12);
    EXPECT_NEAR(bucket_weights[0] + bucket_weights[1], 1.0, 1e-12);
    std::filesystem::remove(path);
}

}  // namespace
