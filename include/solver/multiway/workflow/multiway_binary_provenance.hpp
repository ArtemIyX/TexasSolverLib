#pragma once

/** @file
 * Compile-time binary source, compiler, dirty-state, and schema provenance.
 */

#include <cstdint>
#include <string_view>

#ifndef TEXASSOLVER_BINARY_SOURCE_COMMIT_TOKEN
	#define TEXASSOLVER_BINARY_SOURCE_COMMIT_TOKEN unknown
#endif

#ifndef TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE_TOKEN
	#define TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE_TOKEN unknown
#endif

#ifndef TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN
	#define TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN unknown
#endif

#ifndef TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN
	#define TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN unknown
#endif

#define TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY_INNER(value) #value
#define TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY(value) TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY_INNER(value)

namespace texas::solver::multiway
{

	inline constexpr std::uint32_t MULTIWAY_EVIDENCE_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_SIZING_REPORT_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION = 7U;
	inline constexpr std::uint32_t MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION = 2U;
	inline constexpr std::uint32_t MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION = 2U;
	inline constexpr std::uint32_t MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_TRAINING_CHECKPOINT_ARTIFACT_SCHEMA_VERSION = 5U;

	/** Build identity and evidence schema versions compiled into the binary. */
#pragma pack(push, 8)
	struct MultiwayBinaryProvenance
	{
		std::string_view source_commit = TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY(TEXASSOLVER_BINARY_SOURCE_COMMIT_TOKEN);
		std::string_view dirty_state = TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY(TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE_TOKEN);
		std::string_view compiler_identity = TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY(TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN);
		std::string_view build_configuration = TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY(TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN);
		std::uint32_t evidence_schema_version = MULTIWAY_EVIDENCE_SCHEMA_VERSION;
		std::uint32_t training_report_schema_version = MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION;
		std::uint32_t checkpoint_artifact_schema_version = MULTIWAY_TRAINING_CHECKPOINT_ARTIFACT_SCHEMA_VERSION;
	};
#pragma pack(pop)

	/** Returns the compiler-provided binary provenance constants. */
	inline constexpr MultiwayBinaryProvenance multiway_binary_provenance() noexcept
	{
		return {};
	}

} // namespace texas::solver::multiway

#undef TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY
#undef TEXASSOLVER_BINARY_PROVENANCE_STRINGIFY_INNER
