#pragma once

#include "solver/multiway/abstraction/multiway_model_identity.hpp"

#include <cstdint>
#include <string>

namespace texas::solver::multiway {

constexpr std::uint32_t MULTIWAY_EVIDENCE_SCHEMA_VERSION = 1U;
constexpr std::uint32_t MULTIWAY_SIZING_REPORT_SCHEMA_VERSION = 1U;
constexpr std::uint32_t MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION = 2U;
constexpr std::uint32_t MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION = 1U;
constexpr std::uint32_t MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION = 1U;
constexpr std::uint32_t MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION = 2U;
constexpr std::uint32_t MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION = 1U;

enum class MultiwayWorkflowProfileKind : std::uint8_t {
    Sizing = 1,
    Acceptance = 2,
};

struct MultiwayArtifactSchemaVersions {
    std::uint32_t bucket_artifact = 0U;
    std::uint32_t blueprint_artifact = 0U;
    std::uint32_t checkpoint_artifact = 0U;

    void validate() const;
    constexpr bool operator==(const MultiwayArtifactSchemaVersions& other) const noexcept {
        return bucket_artifact == other.bucket_artifact &&
            blueprint_artifact == other.blueprint_artifact &&
            checkpoint_artifact == other.checkpoint_artifact;
    }
};

struct MultiwayProducerIdentity {
    std::uint32_t schema_version = MULTIWAY_EVIDENCE_SCHEMA_VERSION;
    std::string git_commit;
    std::string build_configuration;
    std::string compiler_identity;
    MultiwayModelIdentity model_identity{};
    std::uint64_t workflow_config_fingerprint = 0U;
    MultiwayArtifactSchemaVersions artifact_schemas{};

    void validate() const;
    bool operator==(const MultiwayProducerIdentity& other) const noexcept {
        return schema_version == other.schema_version &&
            git_commit == other.git_commit &&
            build_configuration == other.build_configuration &&
            compiler_identity == other.compiler_identity &&
            model_identity == other.model_identity &&
            workflow_config_fingerprint == other.workflow_config_fingerprint &&
            artifact_schemas == other.artifact_schemas;
    }
};

struct MultiwayEvidenceHeader {
    std::uint32_t schema_version = 0U;
    MultiwayWorkflowProfileKind profile_kind = MultiwayWorkflowProfileKind::Acceptance;
    MultiwayProducerIdentity producer{};

    void validate(std::uint32_t expected_schema_version) const;
};

void validate_matching_producer_identity(
    const MultiwayProducerIdentity& expected,
    const MultiwayProducerIdentity& actual);

[[nodiscard]] std::string serialize_multiway_evidence_header(
    const MultiwayEvidenceHeader& header);

}  // namespace texas::solver::multiway
