#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{

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

		[[nodiscard]] static MultiwayBlueprintTrainingCheckpoint load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity,
			std::uint64_t expected_schedule_hash,
			std::uint64_t expected_seed);

		[[nodiscard]] static std::uint64_t payload_hash(
			const MultiwayBlueprintTrainingCheckpoint& checkpoint) noexcept;
	};

} // namespace texas::solver::multiway
