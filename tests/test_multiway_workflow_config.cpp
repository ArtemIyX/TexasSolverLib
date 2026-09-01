#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <cstring>

namespace {
const char* valid_config =
    "schema_version=1\nprofile_kind=acceptance\nprofile_id=F1-DEV-12-v1\nplayers=6\ninitial_stack_chips=10000\n"
    "small_blind_chips=50\nbig_blind_chips=100\nante_chips=0\nrake=0\npreflop_classes=169\n"
    "flop_buckets=12\nturn_buckets=12\nriver_buckets=12\nstorage_backend=CompactInt32\n"
    "max_decision_depth=64\nmax_public_chance_depth=3\ndeterministic_seed=1\n"
    "reference_worker_count=1\ntarget_trajectories=50000000\nmaximum_sparse_rows=UNRESOLVED\n";
}

TEST_CASE(multiway_workflow_config_parses_f1_profile) {
    const auto config = texas::solver::multiway::parse_multiway_workflow_config(valid_config);
    EXPECT_EQ(config.model.player_count, 6U);
    EXPECT_EQ(config.model.flop_bucket_count, 12U);
    EXPECT_EQ(config.target_trajectories, 50000000U);
}

TEST_CASE(multiway_workflow_config_rejects_unknown_and_duplicate_keys) {
    EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(std::string(valid_config) + "bogus=1\n"), std::invalid_argument);
    EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(std::string(valid_config) + "schema_version=1\n"), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_fingerprint_binds_semantic_fields) {
    const auto first = texas::solver::multiway::parse_multiway_workflow_config(valid_config);
    auto changed_text = std::string(valid_config);
    changed_text.replace(changed_text.find("profile_kind=acceptance"),
        std::strlen("profile_kind=acceptance"), "profile_kind=sizing");
    changed_text.replace(changed_text.find("50000000"), 8U, "40000000");
    const auto second = texas::solver::multiway::parse_multiway_workflow_config(changed_text);
    EXPECT_TRUE(first.fingerprint() != second.fingerprint());
}

TEST_CASE(multiway_workflow_config_requires_all_or_no_capacity_limits) {
    auto resolved = std::string(valid_config) +
        "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
        "worker_delta_capacity=10000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
        "disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000\n";
    resolved.replace(resolved.find("maximum_sparse_rows=UNRESOLVED"),
                     std::strlen("maximum_sparse_rows=UNRESOLVED"),
                     "maximum_sparse_rows=1000");
    EXPECT_TRUE(texas::solver::multiway::parse_multiway_workflow_config(resolved).capacities_resolved());

    auto partial = std::string(valid_config);
    partial += "maximum_public_states=1000\n";
    EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(partial), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_separates_sizing_and_acceptance_profiles) {
    using namespace texas::solver::multiway;
    auto sizing_text = std::string(valid_config);
    sizing_text.replace(
        sizing_text.find("profile_kind=acceptance"),
        std::strlen("profile_kind=acceptance"), "profile_kind=sizing");
    sizing_text.replace(sizing_text.find("target_trajectories=50000000"),
        std::strlen("target_trajectories=50000000"), "target_trajectories=100");
    const auto sizing = parse_multiway_workflow_config(sizing_text);
    EXPECT_TRUE(sizing.profile_kind == MultiwayWorkflowProfileKind::Sizing);
    EXPECT_EQ(sizing.target_trajectories, 100U);

    auto invalid_acceptance = std::string(valid_config);
    invalid_acceptance.replace(
        invalid_acceptance.find("target_trajectories=50000000"),
        std::strlen("target_trajectories=50000000"), "target_trajectories=100");
    EXPECT_THROW(parse_multiway_workflow_config(invalid_acceptance), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_schema_two_supports_sixteen_training_workers) {
    using namespace texas::solver::multiway;
    auto text = std::string(valid_config);
    text.replace(text.find("schema_version=1"), std::strlen("schema_version=1"), "schema_version=2");
    text.replace(text.find("reference_worker_count=1"),
        std::strlen("reference_worker_count=1"), "training_worker_count=16");
    const auto config = parse_multiway_workflow_config(text);
    EXPECT_EQ(config.training_worker_count, 16U);

    auto zero = text;
    zero.replace(zero.find("training_worker_count=16"),
        std::strlen("training_worker_count=16"), "training_worker_count=0");
    EXPECT_THROW(parse_multiway_workflow_config(zero), std::invalid_argument);

    auto seventeen = text;
    seventeen.replace(seventeen.find("training_worker_count=16"),
        std::strlen("training_worker_count=16"), "training_worker_count=17");
    EXPECT_THROW(parse_multiway_workflow_config(seventeen), std::invalid_argument);

    auto overflowing = text;
    overflowing.replace(overflowing.find("training_worker_count=16"),
        std::strlen("training_worker_count=16"), "training_worker_count=4294967297");
    EXPECT_THROW(parse_multiway_workflow_config(overflowing), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_worker_key_is_schema_specific) {
    auto schema_two_with_legacy_key = std::string(valid_config);
    schema_two_with_legacy_key.replace(schema_two_with_legacy_key.find("schema_version=1"),
        std::strlen("schema_version=1"), "schema_version=2");
    EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(schema_two_with_legacy_key),
        std::invalid_argument);
}

TEST_CASE(multiway_training_worker_override_takes_precedence_and_caps_to_batch) {
    using namespace texas::solver::multiway;
    const auto configured = resolve_multiway_training_workers(4U, 0U, 100U);
    EXPECT_EQ(configured.requested, 4U);
    EXPECT_EQ(configured.effective, 4U);
    const auto overridden = resolve_multiway_training_workers(4U, 16U, 8U);
    EXPECT_EQ(overridden.requested, 16U);
    EXPECT_EQ(overridden.effective, 8U);
    EXPECT_THROW(resolve_multiway_training_workers(4U, 17U, 100U), std::invalid_argument);
    EXPECT_THROW(resolve_multiway_training_workers(4U, 0U, 0U), std::invalid_argument);
}
