#pragma once

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayBucketArtifactProgress {
    std::uint64_t next_board_index = 0U;
    std::uint64_t table_count = 0U;
    std::uint64_t payload_hash = 0U;
    std::uint64_t byte_length = 0U;
};

class MultiwayBucketArtifactWriter {
public:
    MultiwayBucketArtifactWriter(
        std::filesystem::path temporary_path,
        MultiwayModelIdentity identity,
        std::uint64_t expected_table_count);
    static MultiwayBucketArtifactWriter resume(
        std::filesystem::path temporary_path,
        MultiwayModelIdentity identity,
        std::uint64_t expected_table_count,
        MultiwayBucketArtifactProgress progress);
    ~MultiwayBucketArtifactWriter();

    MultiwayBucketArtifactWriter(const MultiwayBucketArtifactWriter&) = delete;
    MultiwayBucketArtifactWriter& operator=(const MultiwayBucketArtifactWriter&) = delete;
    MultiwayBucketArtifactWriter(MultiwayBucketArtifactWriter&&) = default;
    MultiwayBucketArtifactWriter& operator=(MultiwayBucketArtifactWriter&&) = default;

    void append(const MultiwayBucketTable& table);
    void append_chunk(const std::vector<MultiwayBucketTable>& tables);
    void append_serialized_chunk(std::uint64_t table_count, std::vector<std::uint8_t>&& payload);
    void flush_checkpoint();
    void finish(const std::filesystem::path& destination);
    [[nodiscard]] const MultiwayBucketArtifactProgress& progress() const noexcept { return progress_; }

private:
    MultiwayBucketArtifactWriter() = default;
    std::filesystem::path temporary_path_;
    std::ofstream output_;
    MultiwayBucketArtifactProgress progress_{};
    std::uint64_t expected_table_count_ = 0U;
    bool finished_ = false;
};

void save_multiway_bucket_progress_atomic(
    const std::filesystem::path& path,
    const MultiwayModelIdentity& identity,
    std::uint64_t expected_table_count,
    const MultiwayBucketArtifactProgress& progress);
[[nodiscard]] MultiwayBucketArtifactProgress load_multiway_bucket_progress(
    const std::filesystem::path& path,
    const MultiwayModelIdentity& expected_identity,
    std::uint64_t expected_table_count);

}  // namespace texas::solver::multiway
