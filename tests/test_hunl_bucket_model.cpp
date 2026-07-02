#include "solver/hunl_bucket_map.hpp"
#include "solver/hunl_bucket_terminal.hpp"
#include "games/hunl_flat_graph.hpp"
#include "games/hunl.hpp"
#include "test_abstraction_fixture.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using test_support::c;

std::shared_ptr<const core::HUNLConfig> river_config() {
    return std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
}

TEST_CASE(hunl_bucket_model_bucket_counts_match_artifact_metadata) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_counts.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 5U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 5}, core::ABSTRACTION_SCHEMA_VERSION, "custom-river-5", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));

    for (const auto& infoset : graph.infosets) {
        if (infoset.street == core::Street::River) {
            EXPECT_EQ(map.bucket_count(infoset.id), 5U);
        }
    }

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_every_valid_hand_maps_to_a_bucket) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_every_hand.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 4U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 4}, core::ABSTRACTION_SCHEMA_VERSION, "river-4", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));
    const auto live_hands = test_support::enumerate_live_hands(config->initial_board);
    EXPECT_TRUE(!live_hands.empty());

    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        for (const auto& hole : live_hands) {
            const auto bucket = map.lookup_bucket(infoset.id, hole);
            EXPECT_TRUE(bucket >= 0);
            EXPECT_TRUE(static_cast<std::uint32_t>(bucket) < map.bucket_count(infoset.id));
        }
    }

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_invalid_board_or_hand_combinations_fail_cleanly) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_invalid_combo.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); });

    const auto tables = core::load_abstraction(path);
    const auto invalid_board = std::vector<std::uint8_t>{c(2, 0), c(3, 1), c(4, 2), c(5, 3), c(6, 0)};
    const std::array<std::uint8_t, 2> other_hole = {c(8, 0), c(9, 1)};
    const std::array<std::uint8_t, 2> invalid_hole = {config->initial_board[0], c(9, 1)};

    EXPECT_THROW(core::lookup_bucket(tables, invalid_board, other_hole, core::Street::River), std::out_of_range);
    EXPECT_THROW(core::lookup_bucket(tables, config->initial_board, invalid_hole, core::Street::River), std::out_of_range);

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_dense_bucket_ids_are_documented_by_runtime_map) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_dense_ids.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>((index % 2U) == 0U ? 1U : 3U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 4}, core::ABSTRACTION_SCHEMA_VERSION, "sparse-assignment", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));

    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        const auto& ids = map.dense_bucket_ids(infoset.id);
        EXPECT_EQ(ids.size(), map.bucket_count(infoset.id));
        for (std::size_t i = 0; i < ids.size(); ++i) {
            EXPECT_EQ(ids[i], static_cast<std::uint32_t>(i));
        }
    }

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_custom_bucket_schema_round_trips_through_load_and_lookup) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_round_trip.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>& hole) {
            return static_cast<std::uint8_t>((index + hole[0] + hole[1]) % 6U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 6}, core::ABSTRACTION_SCHEMA_VERSION, "river-6-schema", std::nullopt});

    const auto tables = core::load_abstraction(path);
    const auto live_hands = test_support::enumerate_live_hands(config->initial_board);
    EXPECT_TRUE(!live_hands.empty());

    EXPECT_EQ(tables.metadata.version, std::string("river-6-schema"));
    EXPECT_EQ(tables.metadata.bucket_counts[2], 6U);
    EXPECT_EQ(
        core::lookup_bucket(tables, config->initial_board, live_hands[0], core::Street::River),
        static_cast<std::int32_t>((live_hands[0][0] + live_hands[0][1]) % 6U));

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_bucket_weights_default_to_normalized_hand_frequencies) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_default_weights.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));

    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        const auto* weights = map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_EQ(weights->size(), 2U);
        EXPECT_NEAR((*weights)[0] + (*weights)[1], 1.0, 1e-12);
        EXPECT_TRUE((*weights)[0] > 0.0);
        EXPECT_TRUE((*weights)[1] > 0.0);
    }

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_set_bucket_weights_normalizes_custom_weights) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_set_weights.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));

    bool checked_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        map.set_bucket_weights(infoset.id, {2.0, 6.0});
        const auto* weights = map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_NEAR((*weights)[0], 0.25, 1e-12);
        EXPECT_NEAR((*weights)[1], 0.75, 1e-12);
        checked_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_infoset);
    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_model_set_bucket_weights_rejects_wrong_size) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_model_bad_weight_size.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t, const std::array<std::uint8_t, 2>&) { return static_cast<std::uint8_t>(0); },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));
    const std::vector<double> wrong_size_weights = {1.0};

    bool checked_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.street != core::Street::River) {
            continue;
        }
        EXPECT_THROW(map.set_bucket_weights(infoset.id, wrong_size_weights), std::invalid_argument);
        checked_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_infoset);
    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_terminal_cache_reuses_same_board_stats_across_nodes) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_terminal_cache_share.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 3U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 3}, core::ABSTRACTION_SCHEMA_VERSION, "river-3", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));
    const auto table = core::HUNLBucketTerminalTable::build(graph, map);

    EXPECT_TRUE(graph.showdown_terminal_nodes.size() >= 2U);
    const auto first = graph.showdown_terminal_nodes[0];
    const auto second = graph.showdown_terminal_nodes[1];
    EXPECT_TRUE(table.has_showdown_matrix(first));
    EXPECT_TRUE(table.has_showdown_matrix(second));
    EXPECT_TRUE(&table.showdown_matrix(first) == &table.showdown_matrix(second));
    EXPECT_TRUE(table.estimated_bytes() > sizeof(core::HUNLBucketTerminalTable));

    std::filesystem::remove(path);
}

TEST_CASE(hunl_bucket_terminal_expected_showdown_value_is_finite_for_uniform_bucket_ranges) {
    const auto config = river_config();
    const auto path = test_support::write_abstraction_fixture(
        "texas_bucket_terminal_uniform_eval.npz",
        std::nullopt,
        std::nullopt,
        config->initial_board,
        [](core::Street, std::size_t index, const std::array<std::uint8_t, 2>&) {
            return static_cast<std::uint8_t>(index % 2U);
        },
        test_support::AbstractionFixtureOptions{{1, 1, 2}, core::ABSTRACTION_SCHEMA_VERSION, "river-2", std::nullopt});

    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto map = core::HUNLFlatBucketMap::from_abstraction(graph, core::load_abstraction(path));
    const auto table = core::HUNLBucketTerminalTable::build(graph, map);

    bool checked = false;
    for (const auto node_idx : graph.showdown_terminal_nodes) {
        if (!table.has_showdown_matrix(node_idx)) {
            continue;
        }
        const auto value = table.expected_showdown_value(node_idx);
        EXPECT_TRUE(std::isfinite(value));
        checked = true;
        break;
    }
    EXPECT_TRUE(checked);

    std::filesystem::remove(path);
}

}  // namespace
