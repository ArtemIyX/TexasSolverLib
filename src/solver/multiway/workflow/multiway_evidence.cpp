#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <stdexcept>
#include <string>

namespace texas::solver::multiway {

namespace {

bool is_supported_schema(std::uint32_t schema_version) noexcept {
    return schema_version == MULTIWAY_SIZING_REPORT_SCHEMA_VERSION ||
        schema_version == MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION ||
        schema_version == MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION ||
        schema_version == MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION ||
        schema_version == MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION ||
        schema_version == MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION;
}

}  // namespace

void MultiwayArtifactSchemaVersions::validate() const {
    if (bucket_artifact == 0U || blueprint_artifact == 0U || checkpoint_artifact == 0U) {
        throw std::invalid_argument("multiway evidence requires artifact schema versions");
    }
}

void MultiwayProducerIdentity::validate() const {
    if (schema_version != MULTIWAY_EVIDENCE_SCHEMA_VERSION || git_commit.empty() ||
        build_configuration.empty() || compiler_identity.empty() ||
        workflow_config_fingerprint == 0U) {
        throw std::invalid_argument("multiway evidence producer identity is incomplete");
    }
    model_identity.validate();
    artifact_schemas.validate();
    for (const char character : git_commit) {
        if (static_cast<unsigned char>(character) < 0x20U) {
            throw std::invalid_argument("multiway evidence identity contains control characters");
        }
    }
    for (const char character : build_configuration) {
        if (static_cast<unsigned char>(character) < 0x20U) {
            throw std::invalid_argument("multiway evidence identity contains control characters");
        }
    }
    for (const char character : compiler_identity) {
        if (static_cast<unsigned char>(character) < 0x20U) {
            throw std::invalid_argument("multiway evidence identity contains control characters");
        }
    }
}

void MultiwayEvidenceHeader::validate(std::uint32_t expected_schema_version) const {
    if (!is_supported_schema(schema_version) || expected_schema_version == 0U ||
        schema_version != expected_schema_version ||
        (profile_kind != MultiwayWorkflowProfileKind::Sizing &&
         profile_kind != MultiwayWorkflowProfileKind::Acceptance)) {
        throw std::invalid_argument("unsupported or mismatched multiway evidence schema");
    }
    producer.validate();
}

void validate_matching_producer_identity(
    const MultiwayProducerIdentity& expected,
    const MultiwayProducerIdentity& actual) {
    expected.validate();
    actual.validate();
    if (!(expected == actual)) {
        throw std::invalid_argument("multiway evidence producer identity mismatch");
    }
}

namespace {

void append_json_string(std::string& output, const std::string& value) {
    output.push_back('"');
    for (const char character : value) {
        if (character == '\\' || character == '"') output.push_back('\\');
        output.push_back(character);
    }
    output.push_back('"');
}

void append_json_u64(std::string& output, const char* name, std::uint64_t value, bool& first) {
    if (!first) output += ',';
    first = false;
    output += '"';
    output += name;
    output += "\":";
    output += std::to_string(value);
}

}  // namespace

std::string serialize_multiway_evidence_header(const MultiwayEvidenceHeader& header) {
    header.validate(header.schema_version);
    std::string output = "{\"schema_version\":" + std::to_string(header.schema_version) +
        ",\"profile_kind\":" + std::to_string(static_cast<unsigned>(header.profile_kind)) +
        ",\"producer\":{";
    bool first = true;
    append_json_u64(output, "producer_schema_version", header.producer.schema_version, first);
    auto append_string = [&output, &first](const char* name, const std::string& value) {
        if (!first) output += ',';
        first = false;
        output += '"';
        output += name;
        output += "\":";
        append_json_string(output, value);
    };
    append_string("git_commit", header.producer.git_commit);
    append_string("build_configuration", header.producer.build_configuration);
    append_string("compiler_identity", header.producer.compiler_identity);
    append_json_u64(output, "workflow_config_fingerprint", header.producer.workflow_config_fingerprint, first);
    append_json_u64(output, "model_identity", header.producer.model_identity.combined_hash, first);
    append_json_u64(output, "rules_hash", header.producer.model_identity.rules_hash, first);
    append_json_u64(output, "rules_schema_hash", header.producer.model_identity.rules_schema_hash, first);
    append_json_u64(output, "action_abstraction_hash", header.producer.model_identity.action_abstraction_hash, first);
    append_json_u64(output, "bucket_model_hash", header.producer.model_identity.bucket_model_hash, first);
    append_json_u64(output, "terminal_model_hash", header.producer.model_identity.terminal_model_hash, first);
    append_json_u64(output, "resolver_schema_hash", header.producer.model_identity.resolver_schema_hash, first);
    append_json_u64(output, "code_schema_hash", header.producer.model_identity.code_schema_hash, first);
    append_json_u64(output, "range_semantics_hash", header.producer.model_identity.range_semantics_hash, first);
    append_json_u64(output, "future_bucket_model_hash", header.producer.model_identity.future_bucket_model_hash, first);
    append_json_u64(output, "off_tree_policy_hash", header.producer.model_identity.off_tree_policy_hash, first);
    append_json_u64(output, "continuation_policy_hash", header.producer.model_identity.continuation_policy_hash, first);
    append_json_u64(output, "runtime_search_schema_hash", header.producer.model_identity.runtime_search_schema_hash, first);
    append_json_u64(output, "bucket_artifact_schema", header.producer.artifact_schemas.bucket_artifact, first);
    append_json_u64(output, "blueprint_artifact_schema", header.producer.artifact_schemas.blueprint_artifact, first);
    append_json_u64(output, "checkpoint_artifact_schema", header.producer.artifact_schemas.checkpoint_artifact, first);
    output += "}}";
    return output;
}

}  // namespace texas::solver::multiway
