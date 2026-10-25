#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{
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

	struct MultiwayTrainingCheckpointWriteResult
	{
		MultiwayTrainingCheckpointStreamPlan plan{};
		std::uint64_t payload_hash = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t peak_scratch_bytes = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
		MultiwayCheckpointTelemetry telemetry{};
	};

	struct MultiwayTrainingCheckpointStreamSource
	{
		const MultiwayModelIdentity* identity = nullptr;
		const MultiwayBlueprintTrainingMetadata* training = nullptr;
		const MultiwayBlueprintCoverageManifest* coverage = nullptr;
		const MultiwayPublicCatalog* catalog = nullptr;
		const MultiwaySparseRowStorage* storage = nullptr;
		const MultiwayCompactStorage* compact_storage = nullptr;
		const std::vector<double>* late_window_baseline = nullptr;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		std::uint64_t last_merged_stream_fingerprint = 0U;
		std::array<std::uint64_t, 4U> street_visits{};
	};

	// Versioned, lossless process-resume artifact. It is separate from the
	// root-policy deployment artifact because it retains all sparse accumulators.
	class MultiwayTrainingCheckpointArtifacts
	{
	public:
		static void save_atomic(
			const std::filesystem::path& path,
			const MultiwayBlueprintTrainingCheckpoint& checkpoint,
			MultiwayCheckpointTelemetry* telemetry = nullptr);

		// The caller must validate the fully constructed checkpoint before calling
		// this trusted training-workflow path.
		static void save_atomic_sealed(
			const std::filesystem::path& path,
			const MultiwayBlueprintTrainingCheckpoint& checkpoint,
			MultiwayCheckpointTelemetry* telemetry = nullptr);

		[[nodiscard]] static MultiwayTrainingCheckpointWriteResult save_atomic_streamed(
			const std::filesystem::path& path,
			const MultiwayTrainingCheckpointStreamSource& source,
			MultiwayCheckpointTelemetry* telemetry = nullptr);

		[[nodiscard]] static MultiwayBlueprintTrainingCheckpoint load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity,
			std::uint64_t expected_schedule_hash,
			std::uint64_t expected_seed);

		[[nodiscard]] static std::uint64_t payload_hash(
			const MultiwayBlueprintTrainingCheckpoint& checkpoint) noexcept;
	};

} // namespace texas::solver::multiway
