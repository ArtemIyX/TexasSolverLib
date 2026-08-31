#include "solver/multiway/workflow/multiway_evidence.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

texas::solver::multiway::MultiwayProducerIdentity valid_identity() {
    using namespace texas::solver::multiway;
    MultiwayProducerIdentity identity;
    identity.git_commit = "0123456789abcdef";
    identity.build_configuration = "Debug";
    identity.compiler_identity = "test-compiler";
    identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    identity.workflow_config_fingerprint = 17U;
    identity.artifact_schemas = {1U, 1U, 1U};
    return identity;
}

}  // namespace

TEST_CASE(multiway_evidence_identity_accepts_complete_provenance) {
    const auto identity = valid_identity();
    identity.validate();

    texas::solver::multiway::MultiwayEvidenceHeader header;
    header.schema_version = texas::solver::multiway::MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION;
    header.producer = identity;
    header.validate(texas::solver::multiway::MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
}

TEST_CASE(multiway_evidence_identity_rejects_missing_required_fields) {
    auto identity = valid_identity();
    identity.git_commit.clear();
    EXPECT_THROW(identity.validate(), std::invalid_argument);

    identity = valid_identity();
    identity.artifact_schemas.checkpoint_artifact = 0U;
    EXPECT_THROW(identity.validate(), std::invalid_argument);
}

TEST_CASE(multiway_evidence_identity_rejects_schema_and_identity_mismatch) {
    using namespace texas::solver::multiway;
    auto first = valid_identity();
    auto second = first;
    ++second.workflow_config_fingerprint;
    EXPECT_THROW(validate_matching_producer_identity(first, second), std::invalid_argument);

    MultiwayEvidenceHeader header;
    header.schema_version = MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION + 1U;
    header.producer = first;
    EXPECT_THROW(
        header.validate(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION), std::invalid_argument);
}

TEST_CASE(multiway_evidence_identity_serial_components_compare_deterministically) {
    using namespace texas::solver::multiway;
    const auto first_identity = valid_identity();
    const auto second_identity = valid_identity();
    EXPECT_TRUE(first_identity == second_identity);

    MultiwayEvidenceHeader first;
    first.schema_version = MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION;
    first.producer = first_identity;
    MultiwayEvidenceHeader second = first;
    EXPECT_EQ(serialize_multiway_evidence_header(first), serialize_multiway_evidence_header(second));
    EXPECT_TRUE(serialize_multiway_evidence_header(first).find("producer_schema_version") != std::string::npos);
}

TEST_CASE(multiway_evidence_serialization_rejects_unknown_schema) {
    using namespace texas::solver::multiway;
    MultiwayEvidenceHeader header;
    header.schema_version = 99U;
    header.producer = valid_identity();
    EXPECT_THROW(serialize_multiway_evidence_header(header), std::invalid_argument);
    EXPECT_THROW(header.validate(99U), std::invalid_argument);
}
