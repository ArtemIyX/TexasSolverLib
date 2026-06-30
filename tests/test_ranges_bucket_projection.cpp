#include "ranges/bucket_projection.hpp"
#include "ranges/source.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace {

TEST_CASE(ranges_bucket_projection_preserves_total_probability_mass) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_mass.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto combos = core::enumerate_combos(config->initial_board);
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    core::RangeMask mask;
    mask.kind = core::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto canonical = core::make_canonical_range_from_values(core::RangeSourceKind::UniformPrior, range, mask);

    const auto projection = core::project_combo_range_to_buckets(
        canonical,
        combos,
        core::load_abstraction(path),
        core::Street::River);

    EXPECT_NEAR(projection.input_mass, 1.0, 1e-12);
    EXPECT_NEAR(projection.projected_mass, 1.0, 1e-12);
    EXPECT_NEAR(projection.dropped_mass, 0.0, 1e-12);
    EXPECT_NEAR(projection.bucket_range.range.sum(), 1.0, 1e-12);
    std::filesystem::remove(path);
}

TEST_CASE(ranges_bucket_weights_match_sum_of_member_combos) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_sums.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto tables = core::load_abstraction(path);
    const auto combos = core::enumerate_combos(config->initial_board);
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 0.0);
    double bucket_totals[2] = {0.0, 0.0};
    for (std::size_t i = 0; i < combos.size(); ++i) {
        range.weights[i] = static_cast<double>((i % 3U) + 1U);
        const auto bucket = core::lookup_bucket(tables, config->initial_board, combos.hands[i], core::Street::River);
        bucket_totals[bucket] += range.weights[i];
    }
    range.normalize();
    const double total = bucket_totals[0] + bucket_totals[1];

    core::RangeMask mask;
    mask.kind = core::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);
    const auto projection = core::project_combo_range_to_buckets(
        core::make_canonical_range_from_values(core::RangeSourceKind::UniformPrior, range, mask),
        combos,
        tables,
        core::Street::River);

    EXPECT_NEAR(projection.bucket_range.range.weights[0], bucket_totals[0] / total, 1e-12);
    EXPECT_NEAR(projection.bucket_range.range.weights[1], bucket_totals[1] / total, 1e-12);
    std::filesystem::remove(path);
}

TEST_CASE(ranges_exact_and_bucketed_totals_agree_on_toy_projection) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto path = test_support::write_abstraction_fixture(
        "texas_ranges_bucket_projection_totals.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto combos = core::enumerate_combos(config->initial_board);
    core::RangeVector range;
    range.kind = core::RangeVector::Kind::Combo;
    range.weights.assign(combos.size(), 1.0);
    range.normalize();
    core::RangeMask mask;
    mask.kind = core::RangeVector::Kind::Combo;
    mask.enabled.assign(combos.size(), 1U);

    const auto bucket_weights = core::combo_weights_to_bucket_weights(
        range.weights,
        combos,
        core::load_abstraction(path),
        core::Street::River);

    EXPECT_NEAR(range.sum(), 1.0, 1e-12);
    EXPECT_NEAR(bucket_weights[0] + bucket_weights[1], 1.0, 1e-12);
    std::filesystem::remove(path);
}

}  // namespace
