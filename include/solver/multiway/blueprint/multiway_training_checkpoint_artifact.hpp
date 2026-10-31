#pragma once

/** @file
 * Streaming and verified serialization for full multiway training checkpoints.
 */

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/workflow/multiway_binary_provenance.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{
	/** Predicted sizes and retained scratch for a streamed checkpoint write. */
#pragma pack(push, 8)
	struct MultiwayTrainingCheckpointStreamPlan
	{
		std::uint64_t public_state_count = 0U;
		std::uint64_t row_shape_count = 0U;
		std::uint64_t active_page_count = 0U;
		std::uint64_t compact_value_count = 0U;
		std::uint64_t dense_value_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t retained_scratch_bytes = 0U;
	};
#pragma pack(pop)

	/** Measurements and publication gates returned by a checkpoint write. */
#pragma pack(push, 8)
	struct MultiwayTrainingCheckpointWriteResult
	{
		MultiwayTrainingCheckpointStreamPlan plan{};
		std::uint64_t payload_hash = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t peak_scratch_bytes = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
		MultiwayCheckpointTelemetry telemetry{};
		MultiwayExportProcessGateResult process_gate{};
		MultiwayExportSystemGateResult system_gate{};
	};
#pragma pack(pop)

	/** Non-owning trainer data consumed by the streaming checkpoint writer. */
#pragma pack(push, 8)
	struct MultiwayTrainingCheckpointStreamSource
	{
		const MultiwayModelIdentity* identity = nullptr;
		const MultiwayBlueprintTrainingMetadata* training = nullptr;
		const MultiwayBlueprintCoverageManifest* coverage = nullptr;
		const MultiwayPublicCatalog* catalog = nullptr;
		const MultiwaySparseRowStorage* storage = nullptr;
		const MultiwayCompactStorage* compact_storage = nullptr;
		const std::vector<double>* late_window_baseline = nullptr;
		const MultiwayExportMemoryGateConfig* gate = nullptr;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		std::uint64_t last_merged_stream_fingerprint = 0U;
		std::array<std::uint64_t, 4U> street_visits{};
	};
#pragma pack(pop)

	/** Optional fault-injection controls for checkpoint publication. */
#pragma pack(push, 8)
	struct MultiwayTrainingCheckpointWriteOptions
	{
		bool fail_before_close = false;
		bool fail_before_publication = false;
	};
#pragma pack(pop)

	// Versioned, lossless process-resume artifact. It is separate from the
	// root-policy deployment artifact because it retains all sparse accumulators.
	/** Persistence boundary for resumable training state and sparse accumulators. */
	class MultiwayTrainingCheckpointArtifacts
	{
	public:
		/** Validates and atomically saves a checkpoint. */
		static void save_atomic(
			const std::filesystem::path& path,
			const MultiwayBlueprintTrainingCheckpoint& checkpoint,
			MultiwayCheckpointTelemetry* telemetry = nullptr);

		// The caller must validate the fully constructed checkpoint before calling
		// this trusted training-workflow path.
		/** Atomically saves a checkpoint already validated by its workflow caller. */
		static void save_atomic_sealed(
			const std::filesystem::path& path,
			const MultiwayBlueprintTrainingCheckpoint& checkpoint,
			MultiwayCheckpointTelemetry* telemetry = nullptr);

		/** Streams a checkpoint from trainer-owned views and reports write telemetry. */
		[[nodiscard]] static MultiwayTrainingCheckpointWriteResult save_atomic_streamed(
			const std::filesystem::path& path,
			const MultiwayTrainingCheckpointStreamSource& source,
			MultiwayCheckpointTelemetry* telemetry = nullptr,
			MultiwayTrainingCheckpointWriteOptions options = {});

		/** Loads a verified checkpoint matching identity, schedule, and seed. */
		[[nodiscard]] static MultiwayBlueprintTrainingCheckpoint load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity,
			std::uint64_t expected_schedule_hash,
			std::uint64_t expected_seed);

		/** Computes the stable payload hash for a checkpoint. */
		[[nodiscard]] static std::uint64_t payload_hash(
			const MultiwayBlueprintTrainingCheckpoint& checkpoint) noexcept;
	};

} // namespace texas::solver::multiway
