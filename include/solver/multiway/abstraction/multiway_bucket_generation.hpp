#pragma once

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_BUCKET_DEFAULT_THREADS = 16U;
inline constexpr std::uint32_t MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE = 256U;

struct MultiwayBucketGenerationOptions {
    std::uint32_t requested_threads = MULTIWAY_BUCKET_DEFAULT_THREADS;
    std::uint32_t chunk_size = MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE;
    std::uint32_t queue_capacity = 0U;
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
using MultiwayBucketProgressCallback = std::function<void(
    const MultiwayBucketGenerationProgress& progress)>;

[[nodiscard]] std::uint32_t multiway_bucket_hardware_thread_count() noexcept;
[[nodiscard]] std::uint32_t resolve_multiway_bucket_thread_count(
    std::uint32_t requested_threads,
    std::uint32_t detected_threads) noexcept;

void build_multiway_baseline_bucket_chunk(
    const MultiwayModelIdentity& identity,
    const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<MultiwayBucketTable>& tables);

// Builds [begin_index, end_index) concurrently and publishes chunks in
// global-index order. The builder and publisher are cold-path callbacks.
void generate_multiway_bucket_chunks(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    MultiwayBucketGenerationOptions options,
    MultiwayBucketChunkBuilder builder,
    MultiwayBucketChunkPublisher publisher,
    MultiwayBucketProgressCallback progress_callback = {});

}  // namespace texas::solver::multiway
