#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"
#include "util/abstraction.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

using test_support::c;

TEST_CASE(abstraction_loader_loads_known_fixture_successfully) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    const auto path = test_support::write_abstraction_fixture(
        "texas_abstraction_loader_success.npz",
        flop,
        std::nullopt,
        std::nullopt,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); });

    const auto tables = core::load_abstraction(path);

    EXPECT_EQ(tables.metadata.schema_version, core::ABSTRACTION_SCHEMA_VERSION);
    EXPECT_EQ(tables.metadata.version, std::string("v1"));
    EXPECT_EQ(tables.source_path, path);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_rejects_schema_mismatch) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    test_support::AbstractionFixtureOptions options;
    options.schema_version = static_cast<std::uint8_t>(core::ABSTRACTION_SCHEMA_VERSION + 1U);
    const auto path = test_support::write_abstraction_fixture(
        "texas_abstraction_loader_schema_mismatch.npz",
        flop,
        std::nullopt,
        std::nullopt,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        options);

    EXPECT_THROW(core::load_abstraction(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_rejects_missing_npz_entry) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    test_support::AbstractionFixtureOptions options;
    options.omit_entry = std::string("river_hand_index.npy");
    const auto path = test_support::write_abstraction_fixture(
        "texas_abstraction_loader_missing_entry.npz",
        flop,
        std::nullopt,
        std::nullopt,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        options);

    EXPECT_THROW(core::load_abstraction(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_canonical_board_lookup_is_stable_under_suit_permutations) {
    const std::vector<std::uint8_t> flop_a = {c(14, 0), c(13, 1), c(7, 2)};
    const std::vector<std::uint8_t> flop_b = {c(14, 3), c(13, 0), c(7, 1)};

    EXPECT_EQ(core::canonicalize_board(flop_a), core::canonicalize_board(flop_b));
}

TEST_CASE(abstraction_loader_canonical_hole_lookup_only_changes_when_hand_changes) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    const std::array<std::uint8_t, 2> hole_a = {c(12, 0), c(11, 3)};
    const std::array<std::uint8_t, 2> hole_same = {c(11, 3), c(12, 0)};
    const std::array<std::uint8_t, 2> hole_b = {c(12, 0), c(10, 3)};

    const auto [board_key_a, hand_key_a] = core::canonicalize(flop, hole_a);
    const auto [board_key_same, hand_key_same] = core::canonicalize(flop, hole_same);
    const auto [board_key_b, hand_key_b] = core::canonicalize(flop, hole_b);

    EXPECT_EQ(board_key_a, board_key_same);
    EXPECT_EQ(hand_key_a, hand_key_same);
    EXPECT_EQ(board_key_a, board_key_b);
    EXPECT_TRUE(hand_key_a != hand_key_b);
}

TEST_CASE(abstraction_loader_lookup_bucket_returns_expected_bucket_for_small_fixture) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    const auto path = test_support::write_abstraction_fixture(
        "texas_abstraction_loader_bucket_fixture.npz",
        flop,
        std::nullopt,
        std::nullopt,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>((index % 3U) + 4U);
        },
        test_support::AbstractionFixtureOptions{{8, 1, 1}, core::ABSTRACTION_SCHEMA_VERSION, "v1", std::nullopt});

    const auto tables = core::load_abstraction(path);
    const auto live_hands = test_support::enumerate_live_hands(flop);
    EXPECT_TRUE(!live_hands.empty());
    EXPECT_EQ(core::lookup_bucket(tables, flop, live_hands[0], core::Street::Flop), 4);
    EXPECT_EQ(core::lookup_bucket(tables, flop, live_hands[1], core::Street::Flop), 5);
    EXPECT_EQ(core::lookup_bucket(tables, flop, live_hands[2], core::Street::Flop), 6);

    std::filesystem::remove(path);
}

}  // namespace
