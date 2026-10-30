#pragma once

/** @file
 * Serialization API for compact multiway root-policy snapshots.
 */

#include "solver/multiway/blueprint/multiway_export.hpp"

#include <filesystem>

namespace texas::solver::multiway
{

	/** Reads and atomically writes deployment snapshots used for policy resume. */
	class MultiwayRootPolicyArtifact
	{
	public:
		/** Atomically writes a validated compact snapshot to path. */
		static void save_atomic(
			const std::filesystem::path& path,
			const MultiwayBlueprintSnapshot& snapshot);
		/** Loads and validates a compact root-policy snapshot. */
		[[nodiscard]] static MultiwayBlueprintSnapshot load(const std::filesystem::path& path);
		/** Loads a snapshot and checks compatibility with the expected model identity. */
		[[nodiscard]] static MultiwayBlueprintSnapshot load_for_resume(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity);
		/** Rejects a snapshot whose model identity differs from the expected identity. */
		static void validate_resume_identity(
			const MultiwayBlueprintSnapshot& snapshot,
			const MultiwayModelIdentity& expected_identity);
	};

} // namespace texas::solver::multiway
