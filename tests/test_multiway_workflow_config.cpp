#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {
const char* valid_config =
    "schema_version=1\nprofile_id=F1-DEV-12-v1\nplayers=6\ninitial_stack_chips=10000\n"
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
    changed_text.replace(changed_text.find("50000000"), 8U, "40000000");
    const auto second = texas::solver::multiway::parse_multiway_workflow_config(changed_text);
    EXPECT_TRUE(first.fingerprint() != second.fingerprint());
}
