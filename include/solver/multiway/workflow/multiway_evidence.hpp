#pragma once

/** @file
 * Producer identity, schema headers, and stable evidence serialization.
 */

#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "solver/multiway/workflow/multiway_binary_provenance.hpp"

#include <cstdint>
#include <string>

namespace texas::solver::multiway
{

	/** Evidence profile represented by a workflow report. */
	enum class MultiwayWorkflowProfileKind : std::uint8_t
	{
		Sizing = 1,
		Acceptance = 2,
	};

	/** Schema versions for bucket, blueprint, and checkpoint artifacts. */
	struct MultiwayArtifactSchemaVersions
	{
		std::uint32_t bucket_artifact = 0U;
		std::uint32_t blueprint_artifact = 0U;
		std::uint32_t checkpoint_artifact = 0U;

		/** Checks that configured artifact schema versions are supported. */
		void validate() const;
		constexpr bool operator==(const MultiwayArtifactSchemaVersions& other) const noexcept
		{
			return bucket_artifact == other.bucket_artifact && blueprint_artifact == other.blueprint_artifact && checkpoint_artifact == other.checkpoint_artifact;
		}
	};

	/** Binary, model, workflow, and artifact schema identity for evidence. */
	struct MultiwayProducerIdentity
	{
		std::uint32_t schema_version = MULTIWAY_EVIDENCE_SCHEMA_VERSION;
		// git_commit remains the legacy serialized name for the binary source commit.
		std::string git_commit;
		std::string binary_source_commit;
		std::string binary_dirty_state = "unknown";
		std::string build_configuration;
		std::string compiler_identity;
		std::string run_checkout_commit;
		MultiwayModelIdentity model_identity{};
		std::uint64_t workflow_config_fingerprint = 0U;
		MultiwayArtifactSchemaVersions artifact_schemas{};

		/** Checks required producer fields and schema versions. */
		void validate() const;
		bool operator==(const MultiwayProducerIdentity& other) const noexcept
		{
			const auto source_commit = binary_source_commit.empty() ? git_commit : binary_source_commit;
			const auto other_source_commit = other.binary_source_commit.empty() ? other.git_commit : other.binary_source_commit;
			return schema_version == other.schema_version && source_commit == other_source_commit && binary_dirty_state == other.binary_dirty_state && build_configuration == other.build_configuration && compiler_identity == other.compiler_identity && model_identity == other.model_identity && workflow_config_fingerprint == other.workflow_config_fingerprint && artifact_schemas == other.artifact_schemas;
		}
	};

	/** Schema, profile kind, and producer metadata shared by evidence reports. */
	struct MultiwayEvidenceHeader
	{
		std::uint32_t schema_version = 0U;
		MultiwayWorkflowProfileKind profile_kind = MultiwayWorkflowProfileKind::Acceptance;
		MultiwayProducerIdentity producer{};

		/** Checks the header against the expected report schema version. */
		void validate(std::uint32_t expected_schema_version) const;
	};

	/** Rejects producer metadata that differs from the expected identity. */
	void validate_matching_producer_identity(
		const MultiwayProducerIdentity& expected,
		const MultiwayProducerIdentity& actual);

	/** Serializes a validated evidence header into stable text. */
	[[nodiscard]] std::string serialize_multiway_evidence_header(
		const MultiwayEvidenceHeader& header);

} // namespace texas::solver::multiway
