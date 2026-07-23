#include "ranges/cache.hpp"
#include "ranges/range.hpp"
#include "ranges/source.hpp"
#include "test_harness.hpp"

#include <array>
#include <filesystem>
#include <vector>

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

TEST_CASE(ranges_cache_rejects_more_than_twenty_solve_contract_mutations) {
    const auto base = core::benchmark_turn_subgame();
    const auto key = core::make_range_cache_key(
        base, 0, core::RangeVector::Kind::Combo);
    std::vector<core::HUNLConfig> changed;
    auto add = [&](const auto& mutate) {
        auto config = base;
        mutate(config);
        changed.push_back(std::move(config));
    };
    add([](auto& config) { ++config.preflop_raise_cap; });
    add([](auto& config) { ++config.postflop_raise_cap; });
    add([](auto& config) { config.bet_size_fractions[0] += 0.01; });
    add([](auto& config) { config.flop_bet_fractions = std::vector<double>{0.5}; });
    add([](auto& config) { config.turn_bet_fractions = std::vector<double>{0.5}; });
    add([](auto& config) { config.river_bet_fractions = std::vector<double>{0.5}; });
    add([](auto& config) { config.raise_size_xs[0] += 0.1; });
    add([](auto& config) { config.include_all_in = !config.include_all_in; });
    add([](auto& config) { config.auto_all_in_spr_threshold = 0.5; });
    add([](auto& config) { ++config.force_allin_threshold; });
    add([](auto& config) { ++config.min_bet_bb; });
    add([](auto& config) { config.allow_oop_flop_lead = !config.allow_oop_flop_lead; });
    add([](auto& config) { config.rake_rate = 0.01; });
    add([](auto& config) { config.rake_cap = 10; });
    add([](auto& config) { ++config.bucket_counts_by_street[0]; });
    add([](auto& config) { ++config.bucket_counts_by_street[1]; });
    add([](auto& config) { ++config.bucket_counts_by_street[2]; });
    add([](auto& config) { ++config.depth_limit_plies; });
    add([](auto& config) {
        config.flat_solve_mode = core::HUNLFlatSolveMode::ExplicitHand;
    });
    add([](auto& config) {
        config.range_policy = core::HUNLRangePolicy::Uniform;
    });
    add([](auto& config) { config.use_pcs = !config.use_pcs; });
    add([](auto& config) {
        config.initial_hole_cards =
            std::array<std::array<std::uint8_t, 2>, 2>{{
                {core::card_to_int(14, 0), core::card_to_int(14, 1)},
                {core::card_to_int(13, 0), core::card_to_int(13, 1)},
            }};
    });
    add([](auto& config) {
        core::HUNLRangeInput input;
        input.hand_weights.push_back(
            {{core::card_to_int(14, 0), core::card_to_int(14, 1)}, 1.0});
        config.initial_ranges[0] = input;
    });
    add([](auto& config) {
        core::HUNLRangeInput input;
        input.bucket_weights.push_back({core::Street::Turn, 3, 1.0});
        config.player_ranges[1] = input;
    });

    EXPECT_TRUE(changed.size() > 20U);
    for (const auto& config : changed) {
        EXPECT_TRUE(!core::range_cache_key_matches(
            key, config, 0, core::RangeVector::Kind::Combo));
        EXPECT_TRUE(
            core::make_range_cache_key(
                config, 0, core::RangeVector::Kind::Combo)
                .solve_contract_fingerprint !=
            key.solve_contract_fingerprint);
    }
}

}  // namespace
