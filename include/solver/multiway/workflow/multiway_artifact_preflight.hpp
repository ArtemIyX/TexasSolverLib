#pragma once

/** @file
 * Cold-path disk, process-memory, and identity checks before artifact writes.
 */

#include "solver/multiway/abstraction/multiway_model_identity.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{

	/** Destination, identity, size, and resource values checked before writing. */
#pragma pack(push, 8)
	struct MultiwayArtifactPreflightRequest
	{
		std::filesystem::path destination;
		std::filesystem::path temporary;
		// Empty means that no resumable temporary state is requested.
		std::filesystem::path progress_sidecar;
		MultiwayModelIdentity identity{};
		std::uint64_t expected_table_count = 0U;
		std::uint64_t estimated_final_bytes = 0U;
		std::uint64_t required_free_bytes = 0U;
		std::uint64_t estimated_process_memory_bytes = 0U;
		std::uint64_t process_memory_limit_bytes = 0U;
	};
#pragma pack(pop)

	// Cold-path checks performed before bucket generation or publication. Throws
	// on an unsafe or internally inconsistent request.
	/** Validates artifact output paths, capacity, and configured resource bounds. */
	void preflight_multiway_artifact(const MultiwayArtifactPreflightRequest& request);

} // namespace texas::solver::multiway
