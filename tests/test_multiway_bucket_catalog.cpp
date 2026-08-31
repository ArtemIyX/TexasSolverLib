#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iterator>

TEST_CASE(multiway_bucket_catalog_has_exact_postflop_counts) {
    using texas::core::Street;
    EXPECT_EQ(texas::solver::multiway::MultiwayBucketBoardCatalog(Street::Flop).size(), 22100U);
    EXPECT_EQ(texas::solver::multiway::MultiwayBucketBoardCatalog(Street::Turn).size(), 270725U);
    EXPECT_EQ(texas::solver::multiway::MultiwayBucketBoardCatalog(Street::River).size(), 2598960U);
}

TEST_CASE(multiway_bucket_catalog_adjacent_shards_are_complete) {
    using texas::core::Street;
    texas::solver::multiway::MultiwayBucketBoardCatalog catalog(Street::Flop);
    std::uint64_t count = 0U;
    catalog.for_each(0U, 10U, [&](const auto& request) {
        EXPECT_TRUE(texas::solver::multiway::is_multiway_canonical_board(request.street, request.canonical_board));
        ++count;
    });
    EXPECT_EQ(count, 10U);
    EXPECT_THROW(catalog.for_each(10U, 0U, [&](const auto&) {}), std::out_of_range);
}

TEST_CASE(multiway_bucket_catalog_rejects_non_postflop_streets) {
    EXPECT_THROW(texas::solver::multiway::MultiwayBucketBoardCatalog(texas::core::Street::Preflop), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_writer_matches_reference_serialization) {
    using namespace texas::solver::multiway;
    using texas::core::Street;
    MultiwayBlueprintConfig config;
    const auto identity = make_multiway_model_identity(config);
    const auto table = build_multiway_baseline_bucket_table(identity, Street::Flop, {0U, 5U, 10U});
    const auto expected = serialize_multiway_bucket_registry(
        build_multiway_baseline_bucket_registry(identity, {{Street::Flop, {0U, 5U, 10U}}}));
    const auto root = std::filesystem::temp_directory_path() / "texas_solver_catalog_writer_test";
    const auto temporary = root.string() + ".tmp";
    std::filesystem::remove(root);
    std::filesystem::remove(temporary);
    MultiwayBucketArtifactWriter writer(temporary, identity, 1U);
    writer.append(table);
    writer.finish(root);
    std::ifstream input(root, std::ios::binary);
    const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(input)), {});
    EXPECT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < actual.size() && index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index]);
    }
}

TEST_CASE(multiway_bucket_artifact_writer_chunk_matches_single_table_appends) {
    using namespace texas::solver::multiway;
    using texas::core::Street;
    const auto identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    const auto first = build_multiway_baseline_bucket_table(identity, Street::Flop, {0U, 5U, 10U});
    const auto second = build_multiway_baseline_bucket_table(identity, Street::Flop, {0U, 5U, 11U});
    const auto chunk_path = std::filesystem::temp_directory_path() / "texas_solver_chunk_writer_test";
    const auto single_path = std::filesystem::temp_directory_path() / "texas_solver_single_writer_test";
    const auto chunk_tmp = chunk_path.string() + ".tmp";
    const auto single_tmp = single_path.string() + ".tmp";
    std::filesystem::remove(chunk_path); std::filesystem::remove(single_path);
    {
        MultiwayBucketArtifactWriter writer(chunk_tmp, identity, 2U);
        writer.append_chunk({first, second});
        EXPECT_EQ(writer.progress().table_count, 2U);
        writer.finish(chunk_path);
    }
    {
        MultiwayBucketArtifactWriter writer(single_tmp, identity, 2U);
        writer.append(first); writer.append(second);
        writer.finish(single_path);
    }
    std::ifstream chunk(chunk_path, std::ios::binary);
    std::ifstream single(single_path, std::ios::binary);
    const std::vector<char> chunk_bytes((std::istreambuf_iterator<char>(chunk)), {});
    const std::vector<char> single_bytes((std::istreambuf_iterator<char>(single)), {});
    EXPECT_EQ(chunk_bytes, single_bytes);
    chunk.close();
    single.close();
    std::filesystem::remove(chunk_path); std::filesystem::remove(single_path);
}

TEST_CASE(multiway_bucket_progress_sidecar_round_trips_and_rejects_mismatch) {
    using namespace texas::solver::multiway;
    texas::solver::multiway::MultiwayBlueprintConfig config;
    const auto identity = make_multiway_model_identity(config);
    const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_progress_test";
    const MultiwayBucketArtifactProgress progress{7U, 7U, 123U, 456U};
    save_multiway_bucket_progress_atomic(path, identity, 10U, progress);
    const auto restored = load_multiway_bucket_progress(path, identity, 10U);
    EXPECT_EQ(restored.next_board_index, 7U);
    EXPECT_EQ(restored.payload_hash, 123U);
    EXPECT_THROW(load_multiway_bucket_progress(path, MultiwayModelIdentity{}, 10U), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_writer_resumes_from_a_flushed_verified_sidecar) {
    using namespace texas::solver::multiway;
    using texas::core::Street;
    MultiwayBlueprintConfig config;
    const auto identity = make_multiway_model_identity(config);
    const auto destination = std::filesystem::temp_directory_path() / "texas_solver_bucket_resume_test";
    const auto temporary = destination.string() + ".tmp";
    const auto sidecar = destination.string() + ".progress";
    std::filesystem::remove(destination);
    std::filesystem::remove(temporary);
    std::filesystem::remove(sidecar);
    const auto first = build_multiway_baseline_bucket_table(identity, Street::Flop, {0U, 5U, 10U});
    const auto second = build_multiway_baseline_bucket_table(identity, Street::Flop, {0U, 5U, 11U});
    {
        MultiwayBucketArtifactWriter writer(temporary, identity, 2U);
        writer.append(first);
        writer.flush_checkpoint();
        save_multiway_bucket_progress_atomic(sidecar, identity, 2U, writer.progress());
        writer.append(second);
    }
    auto writer = MultiwayBucketArtifactWriter::resume(
        temporary, identity, 2U, load_multiway_bucket_progress(sidecar, identity, 2U));
    writer.append(second);
    writer.finish(destination);
    std::ifstream input(destination, std::ios::binary);
    const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const auto expected = serialize_multiway_bucket_registry(
        build_multiway_baseline_bucket_registry(identity, {
            {Street::Flop, {0U, 5U, 10U}}, {Street::Flop, {0U, 5U, 11U}}}));
    EXPECT_EQ(actual, expected);
    std::filesystem::remove(destination);
    std::filesystem::remove(sidecar);
}
