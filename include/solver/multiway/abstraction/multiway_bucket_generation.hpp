#pragma once

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_BUCKET_DEFAULT_THREADS = 16U;
inline constexpr std::uint32_t MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE = 256U;

struct MultiwayBucketGenerationStats {
    std::uint64_t chunks_built = 0U;
    std::uint64_t chunks_published = 0U;
    std::uint64_t ready_queue_high_watermark = 0U;
    std::uint64_t worker_wait_nanoseconds = 0U;
    std::uint64_t ordered_publish_wait_nanoseconds = 0U;
    std::uint64_t worker_active_nanoseconds = 0U;
};

[[nodiscard]] std::uint64_t estimate_multiway_bucket_generation_process_memory_bytes(
    std::uint32_t requested_threads,
    std::uint32_t detected_threads,
    std::uint32_t chunk_size,
    std::uint32_t queue_capacity = 0U);

using MultiwayBucketDirectSerializedBuilder = std::function<void(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<std::uint8_t>& payload)>;

struct MultiwayBucketGenerationOptions {
    std::uint32_t requested_threads = MULTIWAY_BUCKET_DEFAULT_THREADS;
    std::uint32_t chunk_size = MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE;
    std::uint32_t queue_capacity = 0U;
    MultiwayBucketGenerationStats* stats = nullptr;
    MultiwayBucketDirectSerializedBuilder direct_serialized_builder;
};

struct MultiwayBucketGenerationProgress {
    std::uint64_t completed_tables = 0U;
    std::uint64_t total_tables = 0U;
};

using MultiwayBucketChunkBuilder = std::function<void(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<MultiwayBucketTable>& tables)>;
using MultiwayBucketChunkPublisher = std::function<void(
    std::uint64_t begin_index,
    std::vector<MultiwayBucketTable>&& tables)>;
using MultiwayBucketSerializedChunkPublisher = std::function<void(
    std::uint64_t begin_index,
    std::uint64_t table_count,
    std::vector<std::uint8_t>& payload)>;
using MultiwayBucketProgressCallback = std::function<void(
    const MultiwayBucketGenerationProgress& progress)>;

[[nodiscard]] std::uint32_t multiway_bucket_hardware_thread_count() noexcept;
[[nodiscard]] std::uint32_t multiway_bucket_physical_core_count() noexcept;
[[nodiscard]] std::uint32_t resolve_multiway_bucket_thread_count(
    std::uint32_t requested_threads,
    std::uint32_t detected_threads) noexcept;

void build_multiway_baseline_bucket_chunk(
    const MultiwayModelIdentity& identity,
    const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<MultiwayBucketTable>& tables);
void build_multiway_baseline_direct_serialized_chunk(
    const MultiwayModelIdentity& identity,
    const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<std::uint8_t>& payload);

// Builds [begin_index, end_index) concurrently and publishes chunks in
// global-index order. The builder and publisher are cold-path callbacks.
void generate_multiway_bucket_chunks(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    MultiwayBucketGenerationOptions options,
    MultiwayBucketChunkBuilder builder,
    MultiwayBucketChunkPublisher publisher,
    MultiwayBucketProgressCallback progress_callback = {});

// Builds table chunks and serializes them on worker threads before publication.
// The payload contains only table records, in the same artifact order as the
// table-object API.
void generate_multiway_bucket_serialized_chunks(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    MultiwayBucketGenerationOptions options,
    MultiwayBucketChunkBuilder builder,
    MultiwayBucketSerializedChunkPublisher publisher,
    MultiwayBucketProgressCallback progress_callback = {});

}  // namespace texas::solver::multiway
