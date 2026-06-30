#include "ranges/cache.hpp"
#include "ranges/range.hpp"
#include "ranges/source.hpp"
#include "test_harness.hpp"

#include <filesystem>

namespace {

TEST_CASE(ranges_exported_range_files_round_trip_correctly) {
    core::RangeVector range;
    range.weights = {0.2, 0.3, 0.5};
    const auto path = std::filesystem::temp_directory_path() / "texas_range_roundtrip.tsrng";

    EXPECT_TRUE(core::save_range_file(path, range));

    core::RangeVector loaded;
    EXPECT_TRUE(core::load_range_file(path, loaded));
    EXPECT_EQ(range.weights.size(), loaded.weights.size());
    for (std::size_t i = 0; i < range.weights.size(); ++i) {
        EXPECT_NEAR(range.weights[i], loaded.weights[i], 1e-12);
    }
    std::filesystem::remove(path);
}

TEST_CASE(ranges_cached_entries_reload_exactly) {
    const auto config = core::default_tiny_subgame();
    core::RangeCacheEntry entry;
    entry.key = core::make_range_cache_key(config, 0, core::RangeVector::Kind::Combo);
    entry.range = core::make_uniform_canonical_range(4, core::RangeVector::Kind::Combo);
    entry.iterations = 42;
    entry.exploitability = 0.125;
    entry.label = "cache-reload";

    const auto path = std::filesystem::temp_directory_path() / "texas_range_cache_reload.tsrcache";
    EXPECT_TRUE(core::save_range_cache_entry(path, entry));

    core::RangeCacheEntry loaded;
    EXPECT_TRUE(core::load_range_cache_entry(path, loaded));
    EXPECT_EQ(entry.range.range.weights.size(), loaded.range.range.weights.size());
    EXPECT_EQ(entry.iterations, loaded.iterations);
    EXPECT_NEAR(entry.exploitability, loaded.exploitability, 1e-12);
    for (std::size_t i = 0; i < entry.range.range.weights.size(); ++i) {
        EXPECT_NEAR(entry.range.range.weights[i], loaded.range.range.weights[i], 1e-12);
    }
    std::filesystem::remove(path);
}

TEST_CASE(ranges_stale_cache_entries_are_rejected_when_config_changes) {
    const auto config = core::default_tiny_subgame();
    core::RangeCacheEntry entry;
    entry.key = core::make_range_cache_key(config, 0, core::RangeVector::Kind::Combo);
    entry.range = core::make_uniform_canonical_range(4, core::RangeVector::Kind::Combo);

    const auto path = std::filesystem::temp_directory_path() / "texas_range_cache_stale.tsrcache";
    EXPECT_TRUE(core::save_range_cache_entry(path, entry));

    auto changed = config;
    changed.initial_board[0] = core::card_to_int(2, 0);
    const auto loaded = core::load_range_cache_if_compatible(
        path,
        changed,
        0,
        core::RangeVector::Kind::Combo);
    EXPECT_TRUE(!loaded.has_value());
    std::filesystem::remove(path);
}

TEST_CASE(ranges_precompute_cache_matches_same_node_signature_on_reload) {
    const auto config = core::benchmark_turn_subgame();
    core::RangeCacheEntry entry;
    entry.key = core::make_range_cache_key(config, 1, core::RangeVector::Kind::Combo);
    entry.range = core::make_uniform_canonical_range(8, core::RangeVector::Kind::Combo);
    entry.iterations = 7;
    entry.label = "same-node-signature";

    const auto path = std::filesystem::temp_directory_path() / "texas_range_cache_signature.tsrcache";
    EXPECT_TRUE(core::save_range_cache_entry(path, entry));

    const auto loaded = core::load_range_cache_if_compatible(
        path,
        config,
        1,
        core::RangeVector::Kind::Combo);
    EXPECT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->key.player, entry.key.player);
    EXPECT_EQ(loaded->key.street, entry.key.street);
    EXPECT_EQ(loaded->key.board, entry.key.board);
    EXPECT_EQ(loaded->label, entry.label);
    std::filesystem::remove(path);
}

}  // namespace
