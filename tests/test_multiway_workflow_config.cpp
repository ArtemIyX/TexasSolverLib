#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <cstring>

namespace
{
	const char* valid_config =
		"schema_version=1\nprofile_kind=acceptance\nprofile_id=F1-DEV-12-v1\nplayers=6\ninitial_stack_chips=10000\n"
		"small_blind_chips=50\nbig_blind_chips=100\nante_chips=0\nrake=0\npreflop_classes=169\n"
		"flop_buckets=12\nturn_buckets=12\nriver_buckets=12\nstorage_backend=CompactInt32\n"
		"max_decision_depth=64\nmax_public_chance_depth=3\ndeterministic_seed=1\n"
		"reference_worker_count=1\ntarget_trajectories=50000000\nmaximum_sparse_rows=UNRESOLVED\n";
}

TEST_CASE(multiway_workflow_config_parses_f1_profile)
{
	const auto config = texas::solver::multiway::parse_multiway_workflow_config(valid_config);
	EXPECT_EQ(config.model.player_count, 6U);
	EXPECT_EQ(config.model.flop_bucket_count, 12U);
	EXPECT_EQ(config.target_trajectories, 50000000U);
	EXPECT_EQ(config.process_memory_limit_bytes, 0U);
	EXPECT_EQ(config.process_working_set_limit_bytes,
		texas::MULTIWAY_DEFAULT_PROCESS_WORKING_SET_LIMIT_BYTES);
	EXPECT_TRUE(config.placement_policy == texas::MultiwayWorkerPlacementPolicy::OsDefault);
}

TEST_CASE(multiway_workflow_config_round_trips_opt_in_placement_policy)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "placement_policy=physical-spread\n";
	const auto config = parse_multiway_workflow_config(text);
	EXPECT_TRUE(config.placement_policy == MultiwayWorkerPlacementPolicy::PhysicalSpread);
	const auto round_trip = parse_multiway_workflow_config(serialize_multiway_workflow_config(config));
	EXPECT_TRUE(round_trip.placement_policy == MultiwayWorkerPlacementPolicy::PhysicalSpread);
	EXPECT_EQ(round_trip.fingerprint(), config.fingerprint());
	EXPECT_THROW(parse_multiway_workflow_config(std::string(valid_config) + "placement_policy=hard-affinity\n"), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_round_trips_scheduler_policy_and_chunk_size)
{
	using namespace texas::solver::multiway;
	const auto text = std::string(valid_config) + "scheduler_mode=dynamic\ndynamic_chunk_size=8\n";
	const auto config = parse_multiway_workflow_config(text);
	EXPECT_EQ(config.scheduler_mode, MultiwaySchedulerMode::Dynamic);
	EXPECT_EQ(config.dynamic_chunk_size, 8U);
	const auto round_trip = parse_multiway_workflow_config(serialize_multiway_workflow_config(config));
	EXPECT_EQ(round_trip.scheduler_mode, MultiwaySchedulerMode::Dynamic);
	EXPECT_EQ(round_trip.dynamic_chunk_size, 8U);
	EXPECT_EQ(round_trip.fingerprint(), config.fingerprint());

	const auto changed_mode = parse_multiway_workflow_config(
		std::string(valid_config) + "scheduler_mode=static-block\n");
	EXPECT_TRUE(changed_mode.fingerprint() != config.fingerprint());
	EXPECT_THROW(parse_multiway_workflow_config(std::string(valid_config) + "dynamic_chunk_size=0\n"), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_accepts_legacy_64_gib_working_set_gate)
{
	auto text = std::string(valid_config) + "process_working_set_limit_bytes=68719476736\n";
	const auto config = texas::solver::multiway::parse_multiway_workflow_config(text);
	EXPECT_EQ(config.process_working_set_limit_bytes,
		texas::MULTIWAY_COMPAT_PROCESS_WORKING_SET_LIMIT_BYTES);
}

TEST_CASE(multiway_workflow_config_accepts_distinct_bucket_counts)
{
	using namespace texas::solver::multiway;
	const auto f1 = parse_multiway_workflow_config(valid_config);
	auto alternate_text = std::string(valid_config);
	alternate_text.replace(alternate_text.find("flop_buckets=12"), std::strlen("flop_buckets=12"), "flop_buckets=96");
	alternate_text.replace(alternate_text.find("turn_buckets=12"), std::strlen("turn_buckets=12"), "turn_buckets=128");
	alternate_text.replace(alternate_text.find("river_buckets=12"), std::strlen("river_buckets=12"), "river_buckets=192");
	const auto alternate = parse_multiway_workflow_config(alternate_text);
	EXPECT_EQ(alternate.model.flop_bucket_count, 96U);
	EXPECT_EQ(alternate.model.turn_bucket_count, 128U);
	EXPECT_EQ(alternate.model.river_bucket_count, 192U);
	EXPECT_TRUE(f1.fingerprint() != alternate.fingerprint());

	auto zero_bucket_text = std::string(valid_config);
	zero_bucket_text.replace(zero_bucket_text.find("river_buckets=12"),
		std::strlen("river_buckets=12"), "river_buckets=0");
	EXPECT_THROW(parse_multiway_workflow_config(zero_bucket_text), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_rejects_unknown_and_duplicate_keys)
{
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(std::string(valid_config) + "bogus=1\n"), std::invalid_argument);
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(std::string(valid_config) + "schema_version=1\n"), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_binds_external_storage_identity_and_resources)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config);
	text.replace(text.find("storage_backend=CompactInt32"), std::strlen("storage_backend=CompactInt32"),
		"storage_backend=ExternalCompactInt32");
	text += "storage_directory=external-store\npublic_catalog_cache_bytes=4096\nrow_page_cache_bytes=8192\n"
		"maximum_wal_bytes=65536\nsegment_target_bytes=65536\ncompaction_fan_in=2\n"
		"compaction_scratch_bytes=4096\ndisk_reserve_bytes=1000000\nsynchronous_durability=false\n"
		"maximum_public_states=1000\nmaximum_sparse_values=10000\n"
		"worker_delta_capacity=10000\ndiscovery_capacity=1000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
		"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"), std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto config = parse_multiway_workflow_config(text);
	const auto round_trip = parse_multiway_workflow_config(serialize_multiway_workflow_config(config));
	EXPECT_EQ(round_trip.fingerprint(), config.fingerprint());
	EXPECT_EQ(round_trip.storage_backend, std::string("ExternalCompactInt32"));
	EXPECT_EQ(round_trip.storage_directory, std::filesystem::path("external-store"));
	const auto preflight = preflight_multiway_workflow(config, 0U, 1U);
	EXPECT_TRUE(!preflight.accepted);
	EXPECT_EQ(std::string(preflight.rejected_category), std::string("storage backend"));
	EXPECT_EQ(preflight.memory.estimate.ledger.external_storage_cache_bytes, 12288U);
	EXPECT_EQ(preflight.memory.estimate.ledger.external_storage_scratch_bytes, 69632U);
	EXPECT_EQ(preflight.memory.estimate.ledger.external_storage_disk_reserve_bytes, 1000000U);
	auto modified = text;
	modified.replace(modified.find("row_page_cache_bytes=8192"), std::strlen("row_page_cache_bytes=8192"), "row_page_cache_bytes=8193");
	EXPECT_TRUE(parse_multiway_workflow_config(modified).fingerprint() != config.fingerprint());
}

TEST_CASE(multiway_workflow_config_rejects_external_storage_without_bounded_resources)
{
	auto text = std::string(valid_config);
	text.replace(text.find("storage_backend=CompactInt32"), std::strlen("storage_backend=CompactInt32"),
		"storage_backend=ExternalCompactInt32");
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(text), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_fingerprint_binds_semantic_fields)
{
	const auto first = texas::solver::multiway::parse_multiway_workflow_config(valid_config);
	auto changed_text = std::string(valid_config);
	changed_text.replace(changed_text.find("profile_kind=acceptance"),
		std::strlen("profile_kind=acceptance"), "profile_kind=sizing");
	changed_text.replace(changed_text.find("50000000"), 8U, "40000000");
	const auto second = texas::solver::multiway::parse_multiway_workflow_config(changed_text);
	EXPECT_TRUE(first.fingerprint() != second.fingerprint());
}

TEST_CASE(multiway_workflow_config_requires_all_or_no_capacity_limits)
{
	auto resolved = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
												"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
												"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000\n";
	resolved.replace(resolved.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"),
		"maximum_sparse_rows=1000");
	EXPECT_TRUE(texas::solver::multiway::parse_multiway_workflow_config(resolved).capacities_resolved());

	auto partial = std::string(valid_config);
	partial += "maximum_public_states=1000\n";
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(partial), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_legacy_capacity_fallback_and_round_trip)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
											"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
											"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto legacy = parse_multiway_workflow_config(text);
	EXPECT_EQ(legacy.resolved_regret_delta_capacity(), 10000U);
	EXPECT_EQ(legacy.resolved_continuation_delta_capacity(), 10000U);
	EXPECT_EQ(legacy.resolved_discovery_public_state_capacity(), 40000U);
	EXPECT_EQ(legacy.resolved_discovery_row_capacity(), 40000U);
	const auto round_trip = parse_multiway_workflow_config(serialize_multiway_workflow_config(legacy));
	EXPECT_EQ(round_trip.worker_delta_capacity, legacy.worker_delta_capacity);
	EXPECT_EQ(round_trip.discovery_capacity, legacy.discovery_capacity);
	EXPECT_EQ(round_trip.explicit_stream_capacity_mask, 0U);
	EXPECT_EQ(round_trip.fingerprint(), legacy.fingerprint());
}

TEST_CASE(multiway_workflow_config_explicit_capacity_round_trip_and_identity)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
											"max_regret_delta_entries_per_worker=64000\nmax_continuation_delta_entries_per_worker=1024\n"
											"max_discovery_public_states_per_worker=200000\nmax_discovery_rows_per_worker=100000\n"
											"trajectories_per_batch=10\ncheckpoint_interval=1\ndisk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto explicit_config = parse_multiway_workflow_config(text);
	EXPECT_EQ(explicit_config.resolved_regret_delta_capacity(), 64000U);
	EXPECT_EQ(explicit_config.resolved_continuation_delta_capacity(), 1024U);
	EXPECT_EQ(explicit_config.resolved_discovery_public_state_capacity(), 200000U);
	EXPECT_EQ(explicit_config.resolved_discovery_row_capacity(), 100000U);
	const auto round_trip = parse_multiway_workflow_config(serialize_multiway_workflow_config(explicit_config));
	EXPECT_EQ(round_trip.max_regret_delta_entries_per_worker, 64000U);
	EXPECT_EQ(round_trip.max_continuation_delta_entries_per_worker, 1024U);
	EXPECT_EQ(round_trip.max_discovery_public_states_per_worker, 200000U);
	EXPECT_EQ(round_trip.max_discovery_rows_per_worker, 100000U);
	EXPECT_EQ(round_trip.fingerprint(), explicit_config.fingerprint());

	auto changed = text;
	changed.replace(changed.find("max_regret_delta_entries_per_worker=64000"),
		std::strlen("max_regret_delta_entries_per_worker=64000"),
		"max_regret_delta_entries_per_worker=64001");
	EXPECT_TRUE(parse_multiway_workflow_config(changed).fingerprint() != explicit_config.fingerprint());
}

TEST_CASE(multiway_workflow_config_rejects_zero_and_overflow_explicit_capacities)
{
	auto text = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
											"max_regret_delta_entries_per_worker=64000\nmax_continuation_delta_entries_per_worker=1024\n"
											"max_discovery_public_states_per_worker=200000\nmax_discovery_rows_per_worker=100000\n"
											"trajectories_per_batch=10\ncheckpoint_interval=1\ndisk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=1000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	auto zero = text;
	zero.replace(zero.find("max_continuation_delta_entries_per_worker=1024"),
		std::strlen("max_continuation_delta_entries_per_worker=1024"),
		"max_continuation_delta_entries_per_worker=0");
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(zero), std::invalid_argument);

	auto overflow = text;
	overflow.replace(overflow.find("max_regret_delta_entries_per_worker=64000"),
		std::strlen("max_regret_delta_entries_per_worker=64000"),
		"max_regret_delta_entries_per_worker=18446744073709551616");
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(overflow), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_rejects_capacity_above_resource_policy)
{
	auto text = std::string(valid_config) + "maximum_public_states=34000001\nmaximum_sparse_values=10000\n"
											"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
											"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=100000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(text), std::invalid_argument);
}

TEST_CASE(multiway_workflow_config_accepts_500k_qualification_state_capacity)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "maximum_public_states=34000000\nmaximum_sparse_values=10000\n"
												"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
												"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=100000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto config = parse_multiway_workflow_config(text);
	EXPECT_EQ(config.maximum_public_states, 34'000'000U);
}

TEST_CASE(multiway_workflow_resource_preflight_rejects_aggregate_memory)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
											"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
											"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=100000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto config = parse_multiway_workflow_config(text);
	const auto admitted = preflight_multiway_workflow(config, 1U, 1U);
	EXPECT_TRUE(admitted.accepted);
	EXPECT_EQ(admitted.requested_training_trajectories, 10U);

	auto constrained = config;
	constrained.process_memory_limit_bytes = admitted.memory.estimate.total_bytes - 1U;
	const auto rejected = preflight_multiway_workflow(constrained, 1U, 1U);
	EXPECT_TRUE(!rejected.accepted);
	EXPECT_EQ(std::string(rejected.rejected_category), "worker deltas");
}

TEST_CASE(multiway_workflow_resource_preflight_rejects_excess_workload)
{
	using namespace texas::solver::multiway;
	auto text = std::string(valid_config) + "maximum_public_states=1000\nmaximum_sparse_values=10000\n"
											"worker_delta_capacity=10000\ndiscovery_capacity=40000\ntrajectories_per_batch=10\ncheckpoint_interval=1\n"
											"disk_space_requirement_bytes=1000000\nprocess_memory_limit_bytes=100000000\n";
	text.replace(text.find("maximum_sparse_rows=UNRESOLVED"),
		std::strlen("maximum_sparse_rows=UNRESOLVED"), "maximum_sparse_rows=1000");
	const auto config = parse_multiway_workflow_config(text);
	const auto rejected = preflight_multiway_workflow(config, 1U, 5'000'001U);
	EXPECT_TRUE(!rejected.accepted);
	EXPECT_EQ(std::string(rejected.rejected_category), "workload");
}

TEST_CASE(multiway_workflow_config_separates_sizing_and_acceptance_profiles)
{
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

TEST_CASE(multiway_workflow_config_schema_two_supports_sixteen_training_workers)
{
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

TEST_CASE(multiway_workflow_config_worker_key_is_schema_specific)
{
	auto schema_two_with_legacy_key = std::string(valid_config);
	schema_two_with_legacy_key.replace(schema_two_with_legacy_key.find("schema_version=1"),
		std::strlen("schema_version=1"), "schema_version=2");
	EXPECT_THROW(texas::solver::multiway::parse_multiway_workflow_config(schema_two_with_legacy_key),
		std::invalid_argument);
}

TEST_CASE(multiway_training_worker_override_takes_precedence_and_caps_to_batch)
{
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
