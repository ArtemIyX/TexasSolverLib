#pragma once

/** @file
 * @brief Streams and checkpoints multiway bucket artifacts during generation.
 */

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	/** @brief Resumable position and content state for an artifact writer. */
	struct MultiwayBucketArtifactProgress
	{
		std::uint64_t next_board_index = 0U;
		std::uint64_t table_count = 0U;
		std::uint64_t payload_hash = 0U;
		std::uint64_t byte_length = 0U;
	};

	/** @brief Incremental writer for ordered bucket-table artifact records. */
	class MultiwayBucketArtifactWriter
	{
	public:
		/** @brief Creates a temporary artifact with expected identity and record count. */
		MultiwayBucketArtifactWriter(
			std::filesystem::path temporary_path,
			MultiwayModelIdentity identity,
			std::uint64_t expected_table_count);
		/** @brief Reopens a temporary artifact at a previously flushed checkpoint. */
		static MultiwayBucketArtifactWriter resume(
			std::filesystem::path temporary_path,
			MultiwayModelIdentity identity,
			std::uint64_t expected_table_count,
			MultiwayBucketArtifactProgress progress);
		/** @brief Closes the temporary stream and preserves an unfinished artifact. */
		~MultiwayBucketArtifactWriter();

		MultiwayBucketArtifactWriter(const MultiwayBucketArtifactWriter&) = delete;
		MultiwayBucketArtifactWriter& operator=(const MultiwayBucketArtifactWriter&) = delete;
		MultiwayBucketArtifactWriter(MultiwayBucketArtifactWriter&&) = default;
		MultiwayBucketArtifactWriter& operator=(MultiwayBucketArtifactWriter&&) = default;

		/** @brief Appends one bucket table. */
		void append(const MultiwayBucketTable& table);
		/** @brief Appends an ordered table chunk. */
		void append_chunk(const std::vector<MultiwayBucketTable>& tables);
		/** @brief Appends already-serialized table records. */
		void append_serialized_chunk(std::uint64_t table_count, const std::vector<std::uint8_t>& payload);
		/** @brief Flushes data and captures resumable progress. */
		void flush_checkpoint();
		/** @brief Completes the artifact and publishes it at destination. */
		void finish(const std::filesystem::path& destination);
		/** @brief Returns current record count, payload hash and byte position. */
		[[nodiscard]] const MultiwayBucketArtifactProgress& progress() const noexcept { return progress_; }

	private:
		MultiwayBucketArtifactWriter() = default;
		std::filesystem::path temporary_path_;
		std::ofstream output_;
		MultiwayModelIdentity identity_{};
		MultiwayBucketArtifactProgress progress_{};
		std::uint64_t expected_table_count_ = 0U;
		bool finished_ = false;
	};

	/** @brief Atomically saves a generation checkpoint. */
	void save_multiway_bucket_progress_atomic(
		const std::filesystem::path& path,
		const MultiwayModelIdentity& identity,
		std::uint64_t expected_table_count,
		const MultiwayBucketArtifactProgress& progress);
	/** @brief Loads and validates a generation checkpoint against expected identity and count. */
	[[nodiscard]] MultiwayBucketArtifactProgress load_multiway_bucket_progress(
		const std::filesystem::path& path,
		const MultiwayModelIdentity& expected_identity,
		std::uint64_t expected_table_count);

} // namespace texas::solver::multiway
