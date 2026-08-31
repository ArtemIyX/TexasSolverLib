#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace texas::solver::multiway;

MultiwayModelIdentity identity() {
    return make_multiway_model_identity(MultiwayBlueprintConfig{});
}

MultiwayBucketTable make_table(std::uint64_t index) {
    return build_multiway_baseline_bucket_table(
        identity(), texas::core::Street::Flop,
        {0U, 1U, static_cast<std::uint8_t>(2U + index)},
        MultiwayBucketBaselineProfile::standard());
}

void build_synthetic_chunk(std::uint64_t begin, std::uint64_t end,
                           std::vector<MultiwayBucketTable>& tables) {
    for (auto index = begin; index < end; ++index) tables.push_back(make_table(index));
}

std::filesystem::path unique_path(const char* name) {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_bucket_generation_") + name + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<char> generate_artifact(std::uint32_t threads,
                                    const std::filesystem::path& output_path) {
    constexpr std::uint64_t table_count = 12U;
    const auto temporary_path = output_path.string() + ".tmp";
    MultiwayBucketArtifactWriter writer(temporary_path, identity(), table_count);
    generate_multiway_bucket_chunks(
        0U, table_count, {threads, 3U, 2U}, build_synthetic_chunk,
        [&writer](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
            EXPECT_EQ(begin, writer.progress().table_count);
            for (auto& table : tables) writer.append(table);
        });
    writer.finish(output_path);
    return read_file(output_path);
}

}  // namespace

TEST_CASE(multiway_bucket_generation_resolves_thread_limits) {
    EXPECT_EQ(resolve_multiway_bucket_thread_count(1U, 32U), 1U);
    EXPECT_EQ(resolve_multiway_bucket_thread_count(16U, 8U), 8U);
    EXPECT_EQ(resolve_multiway_bucket_thread_count(32U, 0U), 1U);
    EXPECT_EQ(resolve_multiway_bucket_thread_count(0U, 32U), 0U);
}

TEST_CASE(multiway_bucket_generation_reports_nonzero_physical_core_count) {
    EXPECT_TRUE(multiway_bucket_physical_core_count() > 0U);
    EXPECT_TRUE(multiway_bucket_physical_core_count() <= multiway_bucket_hardware_thread_count());
}

TEST_CASE(multiway_bucket_generation_chunk_maps_across_street_boundaries) {
    const auto flop_count = multiway_bucket_board_count(texas::core::Street::Flop);
    std::vector<MultiwayBucketTable> tables;
    build_multiway_baseline_bucket_chunk(
        identity(), MultiwayBucketBaselineProfile::standard(),
        flop_count - 2U, flop_count + 2U, tables);
    EXPECT_EQ(tables.size(), 4U);
    EXPECT_EQ(tables[0].street(), texas::core::Street::Flop);
    EXPECT_EQ(tables[1].street(), texas::core::Street::Flop);
    EXPECT_EQ(tables[2].street(), texas::core::Street::Turn);
    EXPECT_EQ(tables[3].street(), texas::core::Street::Turn);
}

TEST_CASE(multiway_bucket_generation_fixed_board_kernel_matches_checked_builder) {
    const auto model = identity();
    const std::array<std::uint8_t, 5U> board{0U, 5U, 10U, 0U, 0U};
    const auto checked = build_multiway_baseline_bucket_table(
        model, texas::core::Street::Flop, {0U, 5U, 10U});
    const auto trusted = build_multiway_baseline_bucket_table_fixed_board(
        model, texas::core::Street::Flop, board);
    EXPECT_EQ(trusted.canonical_board(), checked.canonical_board());
    EXPECT_EQ(trusted.bucket_count(), checked.bucket_count());
    EXPECT_EQ(trusted.assignments(), checked.assignments());
    EXPECT_EQ(trusted.table_identity(), checked.table_identity());
}

TEST_CASE(multiway_bucket_generation_publishes_out_of_order_workers_in_order) {
    std::vector<std::uint64_t> published;
    std::vector<std::uint64_t> progress;
    MultiwayBucketGenerationOptions options;
    options.requested_threads = 4U;
    options.chunk_size = 3U;
    options.queue_capacity = 2U;
    generate_multiway_bucket_chunks(
        0U, 10U, options,
        [](std::uint64_t begin, std::uint64_t end, std::vector<MultiwayBucketTable>& tables) {
            if (begin == 0U) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            build_synthetic_chunk(begin, end, tables);
        },
        [&published](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
            for (std::size_t index = 0U; index < tables.size(); ++index) {
                EXPECT_EQ(begin + index,
                    tables[index].canonical_board()[2U] - 2U);
                published.push_back(begin + index);
            }
        },
        [&progress](const MultiwayBucketGenerationProgress& value) {
            progress.push_back(value.completed_tables);
            EXPECT_EQ(value.total_tables, 10U);
        });

    EXPECT_EQ(published.size(), 10U);
    for (std::uint64_t index = 0U; index < published.size(); ++index) {
        EXPECT_EQ(published[static_cast<std::size_t>(index)], index);
    }
    EXPECT_EQ(progress.size(), 4U);
    EXPECT_EQ(progress.back(), 10U);
}

TEST_CASE(multiway_bucket_generation_supports_resume_ranges_and_backpressure) {
    std::vector<std::uint64_t> published;
    MultiwayBucketGenerationOptions options;
    options.requested_threads = 1U;
    options.chunk_size = 2U;
    options.queue_capacity = 2U;
    generate_multiway_bucket_chunks(
        3U, 9U, options, build_synthetic_chunk,
        [&published](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
            for (std::size_t index = 0U; index < tables.size(); ++index) {
                published.push_back(begin + index);
            }
        });
    EXPECT_EQ(published.size(), 6U);
    EXPECT_EQ(published.front(), 3U);
    EXPECT_EQ(published.back(), 8U);
}

TEST_CASE(multiway_bucket_generation_serial_and_parallel_artifacts_match) {
    const auto serial_path = unique_path("serial.bin");
    const auto parallel_path = unique_path("parallel.bin");
    const auto repeat_path = unique_path("parallel-repeat.bin");
    const auto serial = generate_artifact(1U, serial_path);
    const auto parallel = generate_artifact(4U, parallel_path);
    const auto repeated_parallel = generate_artifact(4U, repeat_path);
    EXPECT_EQ(serial, parallel);
    EXPECT_EQ(parallel, repeated_parallel);
    const auto serial_report = inspect_multiway_bucket_artifact(
        serial_path, identity());
    const auto parallel_report = inspect_multiway_bucket_artifact(
        parallel_path, identity());
    const auto repeated_report = inspect_multiway_bucket_artifact(
        repeat_path, identity());
    EXPECT_EQ(serial_report.payload_hash, parallel_report.payload_hash);
    EXPECT_EQ(parallel_report.payload_hash, repeated_report.payload_hash);
    EXPECT_EQ(serial_report.flop_tables, 12U);
    EXPECT_EQ(parallel_report.flop_tables, 12U);
    std::filesystem::remove(serial_path);
    std::filesystem::remove(parallel_path);
    std::filesystem::remove(repeat_path);
}

TEST_CASE(multiway_bucket_generation_serialized_chunks_match_table_chunks) {
    constexpr std::uint64_t table_count = 12U;
    const auto object_path = unique_path("object.bin");
    const auto serialized_path = unique_path("serialized.bin");
    const auto object_bytes = generate_artifact(4U, object_path);
    const auto temporary = serialized_path.string() + ".tmp";
    MultiwayBucketArtifactWriter writer(temporary, identity(), table_count);
    const auto direct_builder = [](std::uint64_t begin, std::uint64_t end,
                                   std::vector<std::uint8_t>& payload) {
        build_multiway_baseline_direct_serialized_chunk(
            identity(), MultiwayBucketBaselineProfile::standard(), begin, end, payload);
    };
    generate_multiway_bucket_serialized_chunks(
        0U, table_count, {4U, 3U, 2U, nullptr, direct_builder}, build_synthetic_chunk,
        [&writer](std::uint64_t begin, std::uint64_t count, std::vector<std::uint8_t>& payload) {
            EXPECT_EQ(begin, writer.progress().table_count);
            writer.append_serialized_chunk(count, payload);
        });
    writer.finish(serialized_path);
    EXPECT_EQ(object_bytes, read_file(serialized_path));
    std::filesystem::remove(object_path);
    std::filesystem::remove(serialized_path);
}

TEST_CASE(multiway_bucket_generation_one_thread_is_synchronous) {
    const auto caller = std::this_thread::get_id();
    bool builder_on_caller = false;
    bool publisher_on_caller = false;
    generate_multiway_bucket_chunks(
        0U, 2U, {1U, 1U, 2U},
        [&](std::uint64_t begin, std::uint64_t end,
            std::vector<MultiwayBucketTable>& tables) {
            builder_on_caller = std::this_thread::get_id() == caller;
            build_synthetic_chunk(begin, end, tables);
        },
        [&](std::uint64_t, std::vector<MultiwayBucketTable>&&) {
            publisher_on_caller = std::this_thread::get_id() == caller;
        });
    EXPECT_TRUE(builder_on_caller);
    EXPECT_TRUE(publisher_on_caller);
}

TEST_CASE(multiway_bucket_generation_parallel_resume_matches_continuous_output) {
    constexpr std::uint64_t table_count = 12U;
    const auto base = unique_path("resume");
    std::filesystem::create_directories(base);
    const auto continuous_path = base / "continuous.bin";
    const auto resumed_path = base / "resumed.bin";
    const auto resumed_temporary = resumed_path.string() + ".tmp";
    const auto sidecar = base.string() + ".progress";

    const auto continuous = generate_artifact(4U, continuous_path);
    {
        MultiwayBucketArtifactWriter writer(resumed_temporary, identity(), table_count);
        generate_multiway_bucket_chunks(
            0U, 6U, {4U, 3U, 2U}, build_synthetic_chunk,
            [&writer](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
                EXPECT_EQ(begin, writer.progress().table_count);
                for (auto& table : tables) writer.append(table);
            });
        writer.flush_checkpoint();
        save_multiway_bucket_progress_atomic(
            sidecar, identity(), table_count, writer.progress());
    }
    const auto progress = load_multiway_bucket_progress(sidecar, identity(), table_count);
    {
        MultiwayBucketArtifactWriter writer = MultiwayBucketArtifactWriter::resume(
            resumed_temporary, identity(), table_count, progress);
        generate_multiway_bucket_chunks(
            progress.table_count, table_count, {4U, 3U, 2U}, build_synthetic_chunk,
            [&writer](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
                EXPECT_EQ(begin, writer.progress().table_count);
                for (auto& table : tables) writer.append(table);
            });
        writer.finish(resumed_path);
    }
    EXPECT_EQ(continuous, read_file(resumed_path));
    std::filesystem::remove(continuous_path);
    std::filesystem::remove(resumed_path);
    std::filesystem::remove(sidecar);
    std::filesystem::remove(resumed_temporary);
    std::filesystem::remove(base);
}

TEST_CASE(multiway_bucket_generation_propagates_worker_failure) {
    std::atomic<std::uint32_t> built{0U};
    EXPECT_THROW(
        generate_multiway_bucket_chunks(
            0U, 12U, {4U, 2U, 2U},
            [&built](std::uint64_t begin, std::uint64_t end,
                     std::vector<MultiwayBucketTable>& tables) {
                if (begin == 4U) throw std::runtime_error("synthetic worker failure");
                build_synthetic_chunk(begin, end, tables);
                ++built;
            },
            [](std::uint64_t, std::vector<MultiwayBucketTable>&&) {}),
        std::runtime_error);
    EXPECT_TRUE(built.load() > 0U);
}

TEST_CASE(multiway_bucket_generation_failure_does_not_publish_artifact) {
    const auto output_path = unique_path("failure.bin");
    const auto temporary_path = output_path.string() + ".tmp";
    EXPECT_THROW(
        [&] {
            MultiwayBucketArtifactWriter writer(temporary_path, identity(), 8U);
            generate_multiway_bucket_chunks(
                0U, 8U, {4U, 2U, 2U},
                [](std::uint64_t begin, std::uint64_t end,
                   std::vector<MultiwayBucketTable>& tables) {
                    if (begin == 0U) throw std::runtime_error("synthetic publication failure");
                    build_synthetic_chunk(begin, end, tables);
                },
                [&writer](std::uint64_t, std::vector<MultiwayBucketTable>&& tables) {
                    for (auto& table : tables) writer.append(table);
                });
        }(),
        std::runtime_error);
    EXPECT_TRUE(!std::filesystem::exists(output_path));
    EXPECT_TRUE(std::filesystem::exists(temporary_path));
    std::filesystem::remove(temporary_path);
}

TEST_CASE(multiway_bucket_generation_rejects_invalid_requests) {
    EXPECT_THROW(
        generate_multiway_bucket_chunks(
            2U, 1U, {}, build_synthetic_chunk,
            [](std::uint64_t, std::vector<MultiwayBucketTable>&&) {}),
        std::invalid_argument);
    EXPECT_THROW(
        generate_multiway_bucket_chunks(
            0U, 1U, {1U, 0U, 0U}, build_synthetic_chunk,
            [](std::uint64_t, std::vector<MultiwayBucketTable>&&) {}),
        std::invalid_argument);
    EXPECT_THROW(
        generate_multiway_bucket_chunks(
            0U, 1U, {0U, 1U, 0U}, build_synthetic_chunk,
            [](std::uint64_t, std::vector<MultiwayBucketTable>&&) {}),
        std::invalid_argument);
}

TEST_CASE(multiway_bucket_generation_repeated_parallel_runs_are_deterministic) {
    for (std::uint32_t run = 0U; run < 5U; ++run) {
        std::vector<std::uint64_t> published;
        generate_multiway_bucket_chunks(
            0U, 16U, {4U, 4U, 8U}, build_synthetic_chunk,
            [&published](std::uint64_t begin, std::vector<MultiwayBucketTable>&& tables) {
                for (std::size_t index = 0U; index < tables.size(); ++index) {
                    published.push_back(begin + index);
                }
            });
        EXPECT_EQ(published.size(), 16U);
        for (std::uint64_t index = 0U; index < published.size(); ++index) {
            EXPECT_EQ(published[static_cast<std::size_t>(index)], index);
        }
    }
}

TEST_CASE(multiway_bucket_generation_reports_aggregate_scheduler_stats) {
    MultiwayBucketGenerationStats stats;
    MultiwayBucketGenerationOptions options{2U, 2U, 2U, &stats};
    std::uint64_t published = 0U;
    generate_multiway_bucket_chunks(
        0U, 6U, options, build_synthetic_chunk,
        [&published](std::uint64_t, std::vector<MultiwayBucketTable>&& tables) { published += tables.size(); });
    EXPECT_EQ(published, 6U);
    EXPECT_EQ(stats.chunks_built, 3U);
    EXPECT_EQ(stats.chunks_published, 3U);
    EXPECT_TRUE(stats.ready_queue_high_watermark <= 2U);
}
