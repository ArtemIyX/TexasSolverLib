#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"
#include "util/abstraction.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
        [](texas::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); });

    const auto tables = texas::load_abstraction(path);

    EXPECT_EQ(tables.metadata.schema_version, texas::ABSTRACTION_SCHEMA_VERSION);
    EXPECT_EQ(tables.metadata.version, std::string("v1"));
    EXPECT_EQ(tables.source_path, path);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_rejects_schema_mismatch) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    test_support::AbstractionFixtureOptions options;
    options.schema_version = static_cast<std::uint8_t>(texas::ABSTRACTION_SCHEMA_VERSION + 1U);
    const auto path = test_support::write_abstraction_fixture(
        "texas_abstraction_loader_schema_mismatch.npz",
        flop,
        std::nullopt,
        std::nullopt,
        [](texas::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        options);

    EXPECT_THROW(texas::load_abstraction(path), std::runtime_error);
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
        [](texas::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        options);

    EXPECT_THROW(texas::load_abstraction(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_rejects_out_of_bounds_zip_directory) {
    const auto path = std::filesystem::temp_directory_path() / "texas_abstraction_loader_bad_directory.npz";
    std::vector<std::uint8_t> bytes(22U, 0U);
    bytes[0] = 0x50U;
    bytes[1] = 0x4bU;
    bytes[2] = 0x05U;
    bytes[3] = 0x06U;
    bytes[10] = 1U;
    bytes[16] = 0xffU;
    bytes[17] = 0xffU;
    bytes[18] = 0xffU;
    bytes[19] = 0xffU;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();

    EXPECT_THROW(texas::load_abstraction(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE(abstraction_loader_canonical_board_lookup_is_stable_under_suit_permutations) {
    const std::vector<std::uint8_t> flop_a = {c(14, 0), c(13, 1), c(7, 2)};
    const std::vector<std::uint8_t> flop_b = {c(14, 3), c(13, 0), c(7, 1)};

    EXPECT_EQ(texas::canonicalize_board(flop_a), texas::canonicalize_board(flop_b));
}

TEST_CASE(abstraction_loader_canonical_hole_lookup_only_changes_when_hand_changes) {
    const std::vector<std::uint8_t> flop = {c(14, 0), c(13, 1), c(7, 2)};
    const std::array<std::uint8_t, 2> hole_a = {c(12, 0), c(11, 3)};
    const std::array<std::uint8_t, 2> hole_same = {c(11, 3), c(12, 0)};
    const std::array<std::uint8_t, 2> hole_b = {c(12, 0), c(10, 3)};

    const auto [board_key_a, hand_key_a] = texas::canonicalize(flop, hole_a);
    const auto [board_key_same, hand_key_same] = texas::canonicalize(flop, hole_same);
    const auto [board_key_b, hand_key_b] = texas::canonicalize(flop, hole_b);

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
        [](texas::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>((index % 3U) + 4U);
        },
        test_support::AbstractionFixtureOptions{{8, 1, 1}, texas::ABSTRACTION_SCHEMA_VERSION, "v1", std::nullopt});

    const auto tables = texas::load_abstraction(path);
    const auto live_hands = test_support::enumerate_live_hands(flop);
    EXPECT_TRUE(!live_hands.empty());
    EXPECT_EQ(texas::lookup_bucket(tables, flop, live_hands[0], texas::Street::Flop), 4);
    EXPECT_EQ(texas::lookup_bucket(tables, flop, live_hands[1], texas::Street::Flop), 5);
    EXPECT_EQ(texas::lookup_bucket(tables, flop, live_hands[2], texas::Street::Flop), 6);

    std::filesystem::remove(path);
}

}  // namespace
