#include "core/atomic_publish.hpp"
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

	texas::MultiwayBlueprintSnapshot snapshot()
	{
		texas::MultiwayBlueprintConfig config;
		texas::MultiwayBlueprintSnapshot result;
		result.identity = texas::make_multiway_model_identity(config);
		result.public_state = { 71U };
		result.infoset = { { 71U }, 0 };
		result.trajectories = 12U;
		result.training.trajectories = 12U;
		result.training.deterministic_seed = 9U;
		result.actions = { { { texas::MultiwayAction::Check, 0U, 0, 17U }, 65535U } };
		result.validate();
		return result;
	}

	std::filesystem::path artifact_path(const char* name)
	{
		return std::filesystem::temp_directory_path() / (std::string("texas_solver_") + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
	}

	texas::MultiwayModelIdentity golden_identity()
	{
		texas::MultiwayModelIdentity identity;
		identity.rules_hash = 1U;
		identity.rules_schema_hash = 1U;
		identity.action_abstraction_hash = 1U;
		identity.bucket_model_hash = 1U;
		identity.terminal_model_hash = 1U;
		identity.resolver_schema_hash = 1U;
		identity.code_schema_hash = 1U;
		identity.range_semantics_hash = 1U;
		identity.future_bucket_model_hash = 1U;
		identity.off_tree_policy_hash = 1U;
		identity.continuation_policy_hash = 1U;
		identity.runtime_search_schema_hash = 1U;
		identity.combined_hash = 1U;
		return identity;
	}

	std::vector<texas::MultiwayBlueprintRow> full_rows(const texas::MultiwayModelIdentity& identity)
	{
		(void)identity;
		return {
			{ { { 31U }, 0 }, 0U, 17U,
				{ { { texas::MultiwayAction::Check, 0U, 0, 17U }, 65535U } } },
			{ { { 71U }, 0 }, 1U, 19U,
				{ { { texas::MultiwayAction::Call, 0U, 3, 19U }, 65535U } } },
		};
	}

	std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		std::vector<std::uint8_t> bytes;
		char value = 0;
		while (input.get(value))
			bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
		return bytes;
	}

	std::vector<std::uint8_t> hex_bytes(const char* text)
	{
		const auto digit = [](char value) -> std::uint8_t {
			if (value >= '0' && value <= '9')
				return static_cast<std::uint8_t>(value - '0');
			if (value >= 'A' && value <= 'F')
				return static_cast<std::uint8_t>(value - 'A' + 10);
			return static_cast<std::uint8_t>(value - 'a' + 10);
		};
		std::string encoded(text);
		std::vector<std::uint8_t> bytes;
		bytes.reserve(encoded.size() / 2U);
		for (std::size_t index = 0U; index < encoded.size(); index += 2U)
			bytes.push_back(static_cast<std::uint8_t>((digit(encoded[index]) << 4U) | digit(encoded[index + 1U])));
		return bytes;
	}

	bool produce_full_row(
		std::uint64_t index,
		texas::MultiwayFullBlueprintRowView& view,
		void* context)
	{
		const auto& rows = *static_cast<const std::vector<texas::MultiwayBlueprintRow>*>(context);
		if (index >= rows.size())
			return false;
		view = texas::MultiwayFullBlueprintRowView(rows[static_cast<std::size_t>(index)]);
		return true;
	}

} // namespace

TEST_CASE(multiway_full_blueprint_schema2_matches_checked_in_golden_bytes)
{
	texas::MultiwayFullBlueprintArtifact artifact;
	artifact.identity = golden_identity();
	artifact.training.batches = 2U;
	artifact.training.trajectories = 3U;
	artifact.training.deterministic_seed = 4U;
	artifact.training.late_window_start_batch = 5U;
	artifact.training.schedule_hash = 6U;
	artifact.training.pruned_negative_regrets = 7U;
	artifact.training.linear_iteration_weighting = 8U;
	artifact.training.discounting_enabled = 9U;
	artifact.training.negative_regret_pruning_enabled = 10U;
	artifact.training.reserved = 11U;
	artifact.rows = {
		{ { { 0x0102030405060708ULL }, 2 }, 0x11223344U, 0x0102030405060708ULL,
			{ { { texas::MultiwayAction::Check, 0U, -9, 0x0102030405060708ULL }, 65535U } } },
	};
	const auto path = artifact_path("schema2_golden");
	texas::MultiwayFullBlueprintArtifacts::save_atomic(path, artifact);
	EXPECT_TRUE(!texas::MultiwayFullBlueprintArtifacts::is_sharded_manifest(path));
	auto expected = hex_bytes(
		"4D57464230303032020000000100000000000000010000000000000001000000000000000100000000000000010000000000"
		"0000010000000000000001000000000000000100000000000000010000000000000001000000000000000100000000000000"
		"0100000000000000010000000000000002000000000000000300000000000000040000000000000005000000000000000600"
		"000000000000070000000000000008090A0B0100000000000000080706050403020102000000443322110807060504030201"
		"010000000100000000F7FFFFFF0807060504030201FFFFEDBFD0CAB509497E");
	EXPECT_EQ(read_bytes(path), expected);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_full_blueprint_materialized_and_streamed_bytes_match)
{
	const auto identity = snapshot().identity;
	auto rows = full_rows(identity);
	texas::MultiwayFullBlueprintArtifact artifact;
	artifact.identity = identity;
	artifact.training.batches = 12U;
	artifact.training.trajectories = 24U;
	artifact.rows = rows;
	const auto materialized_path = artifact_path("materialized_equivalence");
	const auto streamed_path = artifact_path("streamed_equivalence");
	texas::MultiwayFullBlueprintArtifacts::save_atomic(materialized_path, artifact);
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, artifact.training, 2U, 2U);
	const auto result = texas::MultiwayFullBlueprintArtifacts::save_atomic_streaming(
		streamed_path, plan, &produce_full_row, &rows);
	EXPECT_EQ(read_bytes(materialized_path), read_bytes(streamed_path));
	EXPECT_EQ(result.row_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.action_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.payload_hash, texas::MultiwayFullBlueprintArtifacts::payload_hash(artifact));
	EXPECT_EQ(result.policy_content_hash, texas::MultiwayFullBlueprintArtifacts::policy_content_hash(artifact));
	EXPECT_EQ(result.serialized_bytes, plan.serialized_bytes);
	const auto loaded = texas::MultiwayFullBlueprintArtifacts::load_verified(streamed_path, identity);
	EXPECT_EQ(loaded.rows.size(), std::size_t{ 2U });
	{
		std::fstream corrupt(streamed_path, std::ios::binary | std::ios::in | std::ios::out);
		corrupt.seekp(12, std::ios::beg);
		corrupt.put('\0');
	}
	EXPECT_THROW(
		texas::MultiwayFullBlueprintArtifacts::load_verified(streamed_path, identity),
		std::invalid_argument);
	std::filesystem::remove(materialized_path);
	std::filesystem::remove(streamed_path);
}

TEST_CASE(multiway_full_blueprint_shard_layout_is_deterministic_and_worst_case_bounded)
{
	using namespace texas::solver::multiway;
	const auto plan = make_multiway_full_blueprint_stream_plan(snapshot().identity, {}, 5U, 5U);
	constexpr std::uint64_t fixed_bytes = 176U + 8U;
	constexpr std::uint64_t maximum_row_bytes = 28U + 64U * 19U;
	const auto byte_bound = fixed_bytes + 2U * maximum_row_bytes;
	const auto first = make_multiway_full_blueprint_shard_layout(plan, byte_bound);
	const auto second = make_multiway_full_blueprint_shard_layout(plan, byte_bound);
	EXPECT_EQ(first.maximum_rows_per_shard, std::uint64_t{ 2U });
	EXPECT_EQ(first.ranges.size(), std::size_t{ 3U });
	EXPECT_EQ(first.ranges[0].first_row, std::uint64_t{ 0U });
	EXPECT_EQ(first.ranges[0].row_count, std::uint64_t{ 2U });
	EXPECT_EQ(first.ranges[1].first_row, std::uint64_t{ 2U });
	EXPECT_EQ(first.ranges[1].row_count, std::uint64_t{ 2U });
	EXPECT_EQ(first.ranges[2].first_row, std::uint64_t{ 4U });
	EXPECT_EQ(first.ranges[2].row_count, std::uint64_t{ 1U });
	EXPECT_EQ(first.ranges.size(), second.ranges.size());
	for (std::size_t index = 0U; index < first.ranges.size(); ++index)
	{
		EXPECT_EQ(first.ranges[index].first_row, second.ranges[index].first_row);
		EXPECT_EQ(first.ranges[index].row_count, second.ranges[index].row_count);
		EXPECT_TRUE(fixed_bytes + first.ranges[index].row_count * maximum_row_bytes <= byte_bound);
	}
	first.validate(plan);
	auto invalid = first;
	invalid.ranges[1].first_row += 1U;
	EXPECT_THROW(invalid.validate(plan), std::invalid_argument);
	EXPECT_THROW(make_multiway_full_blueprint_shard_layout(plan, fixed_bytes + maximum_row_bytes - 1U),
		std::invalid_argument);
	const auto excessive_plan = make_multiway_full_blueprint_stream_plan(
		snapshot().identity, {}, MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS + 1U,
		MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS + 1U);
	EXPECT_THROW(make_multiway_full_blueprint_shard_layout(excessive_plan,
		fixed_bytes + maximum_row_bytes), std::length_error);
}

TEST_CASE(multiway_full_blueprint_sharded_stream_publishes_verified_manifest_last)
{
	using namespace texas::solver::multiway;
	const auto identity = snapshot().identity;
	auto rows = full_rows(identity);
	rows[0].actions[0].probability = 30000U;
	rows[0].actions.push_back({ { texas::MultiwayAction::Call, 1U, 0, rows[0].action_menu_id }, 35535U });
	const auto plan = make_multiway_full_blueprint_stream_plan(identity, {}, 2U, 3U);
	const auto manifest_path = artifact_path("sharded_manifest");
	const auto shard_bound = 176U + 8U + 28U + 64U * 19U;
	const auto shard_layout = make_multiway_full_blueprint_shard_layout(plan, shard_bound);
	const auto size_estimate = estimate_multiway_full_blueprint_sharded_size(
		plan, shard_layout, manifest_path);
	const auto result = MultiwayFullBlueprintArtifacts::save_atomic_sharded_streaming(
		manifest_path, plan, shard_bound, &produce_full_row, &rows);
	EXPECT_TRUE(MultiwayFullBlueprintArtifacts::is_sharded_manifest(manifest_path));
	const auto manifest = MultiwayFullBlueprintArtifacts::load_sharded_manifest(manifest_path, identity);
	MultiwayFullBlueprintArtifacts::verify_sharded_artifact(manifest_path, identity);
	const auto mapped = MultiwayMappedShardedBlueprintProvider::load_verified(manifest_path, identity);
	MultiwayBlueprintPolicyProvider mapped_provider(*mapped);
	MultiwayBlueprintStore in_memory_store(identity, rows);
	MultiwayBlueprintPolicyProvider in_memory_provider(in_memory_store);
	EXPECT_EQ(result.row_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.action_count, std::uint64_t{ 3U });
	EXPECT_EQ(result.shard_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.manifest_bytes, std::filesystem::file_size(manifest_path));
	EXPECT_EQ(size_estimate.manifest_bytes, result.manifest_bytes);
	EXPECT_EQ(size_estimate.shard_bytes,
		manifest.shards[0].serialized_bytes + manifest.shards[1].serialized_bytes);
	EXPECT_EQ(size_estimate.total_bytes, result.serialized_bytes);
	EXPECT_EQ(result.serialized_bytes,
		manifest.shards[0].serialized_bytes + manifest.shards[1].serialized_bytes + result.manifest_bytes);
	EXPECT_EQ(result.policy_content_hash, MultiwayFullBlueprintArtifacts::policy_content_hash(
		MultiwayFullBlueprintArtifact{ MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION, identity, {}, rows, 0U }));
	EXPECT_EQ(manifest.shards.size(), std::size_t{ 2U });
	EXPECT_EQ(manifest.shards[0].range.first_row, std::uint64_t{ 0U });
	EXPECT_EQ(manifest.shards[1].range.first_row, std::uint64_t{ 1U });
	EXPECT_TRUE(mapped->mapped_virtual_bytes() >= result.serialized_bytes - result.manifest_bytes);
	EXPECT_TRUE(mapped->sparse_index_bytes() > 0U);
	for (const auto& row : rows)
	{
		std::array<texas::Probability, 64U> mapped_output{};
		std::array<texas::Probability, 64U> in_memory_output{};
		std::array<MultiwayActionDescriptor, 64U> legal_actions{};
		for (std::size_t index = 0U; index < row.actions.size(); ++index)
			legal_actions[index] = row.actions[index].action;
		const auto mapped_status = mapped_provider.strategy_into(row.infoset, row.bucket,
			legal_actions.data(), row.actions.size(), mapped_output.data());
		const auto in_memory_status = in_memory_provider.strategy_into(row.infoset, row.bucket,
			legal_actions.data(), row.actions.size(), in_memory_output.data());
		EXPECT_EQ(mapped_status, in_memory_status);
		EXPECT_EQ(mapped_status, MultiwayBlueprintLookupStatus::Hit);
		for (std::size_t index = 0U; index < row.actions.size(); ++index)
			EXPECT_EQ(mapped_output[index], in_memory_output[index]);
	}
	{
		std::array<texas::Probability, 64U> mapped_output{};
		std::array<texas::Probability, 64U> in_memory_output{};
		std::array<MultiwayActionDescriptor, 2U> wrong_menu{
			rows[0].actions[0].action, rows[0].actions[1].action };
		const std::array<MultiwayActionDescriptor, 2U> legal_actions{
			rows[0].actions[0].action, rows[0].actions[1].action };
		++wrong_menu[0].action_menu_id;
		EXPECT_EQ(mapped_provider.strategy_into(rows[0].infoset, rows[0].bucket,
			wrong_menu.data(), wrong_menu.size(), mapped_output.data()), MultiwayBlueprintLookupStatus::IncompatibleMenu);
		EXPECT_EQ(in_memory_provider.strategy_into(rows[0].infoset, rows[0].bucket,
			wrong_menu.data(), wrong_menu.size(), in_memory_output.data()), MultiwayBlueprintLookupStatus::IncompatibleMenu);
		EXPECT_EQ(mapped_provider.strategy_into(rows[0].infoset, 99U,
			legal_actions.data(), legal_actions.size(), mapped_output.data()), MultiwayBlueprintLookupStatus::MissingBucket);
		EXPECT_EQ(mapped_provider.strategy_into({ { 45U }, 0 }, 0U,
			legal_actions.data(), legal_actions.size(), mapped_output.data()), MultiwayBlueprintLookupStatus::Missing);
	}

	const auto previous_manifest = read_bytes(manifest_path);
	MultiwayFullBlueprintStreamOptions failure_options;
	failure_options.failure_point = MultiwayFullBlueprintStreamFailurePoint::ManifestPublication;
	EXPECT_THROW(MultiwayFullBlueprintArtifacts::save_atomic_sharded_streaming(
		manifest_path, plan, shard_bound, &produce_full_row,
		&rows, failure_options), std::runtime_error);
	EXPECT_EQ(read_bytes(manifest_path), previous_manifest);

	const auto shard_path = manifest_path.parent_path() / std::filesystem::u8path(manifest.shards[0].relative_path);
	std::filesystem::remove(shard_path);
	EXPECT_THROW(MultiwayFullBlueprintArtifacts::verify_sharded_artifact(manifest_path, identity), std::runtime_error);
	EXPECT_THROW(MultiwayMappedShardedBlueprintProvider::load_verified(manifest_path, identity), std::runtime_error);
	std::filesystem::remove_all(shard_path.parent_path());
	std::filesystem::remove(manifest_path);

	const auto corrupt_manifest_path = artifact_path("sharded_corrupt_manifest");
	(void)MultiwayFullBlueprintArtifacts::save_atomic_sharded_streaming(
		corrupt_manifest_path, plan, shard_bound, &produce_full_row, &rows);
	const auto corrupt_manifest = MultiwayFullBlueprintArtifacts::load_sharded_manifest(
		corrupt_manifest_path, identity);
	const auto corrupt_shard_path = corrupt_manifest_path.parent_path() /
		std::filesystem::u8path(corrupt_manifest.shards[0].relative_path);
	{
		std::fstream corrupt(corrupt_shard_path, std::ios::binary | std::ios::in | std::ios::out);
		corrupt.put('X');
	}
	EXPECT_THROW(MultiwayFullBlueprintArtifacts::verify_sharded_artifact(corrupt_manifest_path, identity),
		std::runtime_error);
	EXPECT_THROW(MultiwayMappedShardedBlueprintProvider::load_verified(corrupt_manifest_path, identity),
		std::invalid_argument);
	std::filesystem::remove_all(corrupt_shard_path.parent_path());
	std::filesystem::remove(corrupt_manifest_path);
}

TEST_CASE(multiway_full_blueprint_sharded_writer_accepts_ordered_rows_without_materializing_them)
{
	using namespace texas::solver::multiway;
	const auto identity = snapshot().identity;
	auto rows = full_rows(identity);
	rows[0].actions[0].probability = 30000U;
	rows[0].actions.push_back({ { texas::MultiwayAction::Call, 1U, 0, rows[0].action_menu_id }, 35535U });
	const auto plan = make_multiway_full_blueprint_stream_plan(identity, {}, 2U, 3U);
	const auto maximum_shard_bytes = 176U + 8U + 28U + 64U * 19U;
	const auto layout = make_multiway_full_blueprint_shard_layout(plan, maximum_shard_bytes);
	const auto manifest_path = artifact_path("incremental_sharded_manifest");
	MultiwayFullBlueprintShardedStreamWriter writer(manifest_path, plan, layout, { 2U, 1U });
	for (const auto& row : rows)
		writer.write_row(MultiwayFullBlueprintRowView(row));
	const auto result = writer.finish();
	MultiwayFullBlueprintArtifacts::verify_sharded_artifact(manifest_path, identity);
	EXPECT_EQ(result.row_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.action_count, std::uint64_t{ 3U });
	EXPECT_EQ(result.shard_count, std::uint64_t{ 2U });
	EXPECT_EQ(result.policy_content_hash, MultiwayFullBlueprintArtifacts::policy_content_hash(
		MultiwayFullBlueprintArtifact{ MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION, identity, {}, rows, 0U }));
	const auto manifest = MultiwayFullBlueprintArtifacts::load_sharded_manifest(manifest_path, identity);
	std::filesystem::remove_all(manifest_path.parent_path() / std::filesystem::u8path(
		manifest.shards[0].relative_path).parent_path());
	std::filesystem::remove(manifest_path);
}

TEST_CASE(multiway_full_blueprint_incremental_hash_matches_materialized_hash)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto training = texas::MultiwayBlueprintTrainingMetadata{};
	const auto expected = [&]() {
		texas::MultiwayFullBlueprintArtifact artifact;
		artifact.identity = identity;
		artifact.training = training;
		artifact.rows = rows;
		return texas::MultiwayFullBlueprintArtifacts::payload_hash(artifact);
	}();
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, training, 2U, 2U);
	texas::MultiwayFullBlueprintPayloadHasher hasher;
	hasher.begin(plan.schema_version, plan.identity, plan.training, plan.row_count);
	for (const auto& row : rows)
		hasher.append_row(texas::MultiwayFullBlueprintRowView(row));
	EXPECT_EQ(hasher.finish(), expected);
}

TEST_CASE(multiway_full_blueprint_policy_content_hash_ignores_execution_metadata_and_row_order)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	texas::MultiwayFullBlueprintArtifact first;
	first.identity = identity;
	first.training.batches = 1U;
	first.rows = rows;
	texas::MultiwayFullBlueprintArtifact second = first;
	second.identity.rules_hash = 99U;
	second.training.batches = 99U;
	std::reverse(second.rows.begin(), second.rows.end());
	EXPECT_EQ(
		texas::MultiwayFullBlueprintArtifacts::policy_content_hash(first),
		texas::MultiwayFullBlueprintArtifacts::policy_content_hash(second));
	EXPECT_TRUE(
		texas::MultiwayFullBlueprintArtifacts::payload_hash(first)
		!= texas::MultiwayFullBlueprintArtifacts::payload_hash(second));
	second.rows.front().action_menu_id = 20U;
	second.rows.front().actions.front().action.action_menu_id = 20U;
	EXPECT_TRUE(
		texas::MultiwayFullBlueprintArtifacts::policy_content_hash(first)
		!= texas::MultiwayFullBlueprintArtifacts::policy_content_hash(second));
}

TEST_CASE(multiway_full_blueprint_accepts_unsorted_materialized_rows_but_rejects_duplicate_keys)
{
	const auto identity = snapshot().identity;
	const auto path = artifact_path("unsorted_compatibility");
	texas::MultiwayFullBlueprintArtifact unsorted;
	unsorted.identity = identity;
	unsorted.rows = full_rows(identity);
	std::reverse(unsorted.rows.begin(), unsorted.rows.end());
	unsorted.payload_hash = texas::MultiwayFullBlueprintArtifacts::payload_hash(unsorted);
	unsorted.validate();

	auto duplicate = unsorted;
	duplicate.rows.back() = duplicate.rows.front();
	duplicate.payload_hash = texas::MultiwayFullBlueprintArtifacts::payload_hash(duplicate);
	EXPECT_THROW(duplicate.validate(), std::invalid_argument);
	texas::MultiwayFullBlueprintArtifacts::save_atomic(path, unsorted);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_full_blueprint_stream_rejects_order_and_malformed_rows)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, {}, 2U, 2U);
	const auto path = artifact_path("stream_rejection");
	{
		std::ofstream old_value(path, std::ios::binary);
		old_value << "previous";
	}
	{
		texas::MultiwayFullBlueprintStreamWriter writer(path, plan);
		writer.write_row(texas::MultiwayFullBlueprintRowView(rows[1]));
		EXPECT_THROW(writer.write_row(texas::MultiwayFullBlueprintRowView(rows[0])), std::invalid_argument);
	}
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	const std::vector<std::uint8_t> previous_bytes = { 'p', 'r', 'e', 'v', 'i', 'o', 'u', 's' };
	EXPECT_EQ(read_bytes(path), previous_bytes);

	auto malformed = rows.front();
	malformed.actions.front().probability = 65534U;
	{
		texas::MultiwayFullBlueprintStreamWriter writer(path, plan, { true, texas::MultiwayFullBlueprintStreamFailurePoint::None });
		EXPECT_THROW(writer.write_row(texas::MultiwayFullBlueprintRowView(malformed)), std::invalid_argument);
	}
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	std::filesystem::remove(path);
}

TEST_CASE(multiway_full_blueprint_stream_progress_distinguishes_buffered_bytes)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, {}, 1U, 1U);
	const auto path = artifact_path("stream_progress");
	const auto baseline_path = artifact_path("stream_progress_baseline");
	{
		texas::solver::multiway::MultiwayFullBlueprintWriterProfile profile;
		texas::MultiwayFullBlueprintStreamWriter writer(path, plan,
			{ true, texas::MultiwayFullBlueprintStreamFailurePoint::None, &profile });
		const auto header = writer.progress();
		EXPECT_TRUE(header.encoded_bytes > 0U);
		EXPECT_EQ(header.stream_write_bytes, 0U);
		writer.write_row(texas::MultiwayFullBlueprintRowView(rows.front()));
		const auto row = writer.progress();
		EXPECT_TRUE(row.encoded_bytes > header.encoded_bytes);
		EXPECT_EQ(row.stream_write_bytes, 0U);
		const auto result = writer.finish();
		const auto complete = writer.progress();
		EXPECT_EQ(complete.encoded_bytes, result.serialized_bytes);
		EXPECT_EQ(complete.stream_write_bytes, result.serialized_bytes);
		EXPECT_EQ(profile.sampled_rows, 1U);
		EXPECT_EQ(profile.flushes, 1U);
	}
	{
		texas::MultiwayFullBlueprintStreamWriter baseline(baseline_path, plan);
		baseline.write_row(texas::MultiwayFullBlueprintRowView(rows.front()));
		(void)baseline.finish();
	}
	EXPECT_EQ(read_bytes(path), read_bytes(baseline_path));
	std::filesystem::remove(path);
	std::filesystem::remove(baseline_path);
}

TEST_CASE(multiway_sharded_stream_progress_counts_completed_shards)
{
	using namespace texas::solver::multiway;
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto plan = make_multiway_full_blueprint_stream_plan(identity, {}, 2U, 2U);
	const auto single = make_multiway_full_blueprint_stream_plan(identity, {}, 1U, 1U);
	const auto maximum = make_multiway_full_blueprint_stream_plan(
		identity, {}, 1U, MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW);
	auto layout = make_multiway_full_blueprint_shard_layout(plan, maximum.serialized_bytes);
	const auto path = artifact_path("sharded_stream_progress");
	{
		MultiwayFullBlueprintShardedStreamWriter writer(path, plan, std::move(layout), { 1U, 1U });
		EXPECT_EQ(writer.progress().encoded_bytes, 0U);
		writer.write_row(MultiwayFullBlueprintRowView(rows[0]));
		const auto first = writer.progress();
		EXPECT_EQ(first.encoded_bytes, single.serialized_bytes);
		EXPECT_EQ(first.stream_write_bytes, single.serialized_bytes);
		writer.write_row(MultiwayFullBlueprintRowView(rows[1]));
		const auto second = writer.progress();
		EXPECT_EQ(second.encoded_bytes, 2U * single.serialized_bytes);
		EXPECT_EQ(second.stream_write_bytes, second.encoded_bytes);
		const auto result = writer.finish();
		EXPECT_EQ(result.serialized_bytes, second.encoded_bytes + result.manifest_bytes);
	}
	const auto manifest = MultiwayFullBlueprintArtifacts::load_sharded_manifest(path, identity);
	const auto shard_path = path.parent_path() / std::filesystem::u8path(manifest.shards[0].relative_path);
	std::filesystem::remove_all(shard_path.parent_path());
	std::filesystem::remove(path);
}

TEST_CASE(multiway_full_blueprint_stream_failure_points_cleanup_and_preserve_destination)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, {}, 1U, 1U);
	const texas::MultiwayFullBlueprintStreamFailurePoint points[] = {
		texas::MultiwayFullBlueprintStreamFailurePoint::Write,
		texas::MultiwayFullBlueprintStreamFailurePoint::Close,
		texas::MultiwayFullBlueprintStreamFailurePoint::Publication,
	};
	for (const auto point : points)
	{
		const auto path = artifact_path("stream_failure");
		{
			std::ofstream old_value(path, std::ios::binary);
			old_value << "previous";
		}
		{
			texas::MultiwayFullBlueprintStreamWriter writer(path, plan, { true, point });
			if (point == texas::MultiwayFullBlueprintStreamFailurePoint::Write)
				EXPECT_THROW(writer.write_row(texas::MultiwayFullBlueprintRowView(rows.front())), std::runtime_error);
			else
			{
				writer.write_row(texas::MultiwayFullBlueprintRowView(rows.front()));
				EXPECT_THROW(writer.finish(), std::runtime_error);
			}
		}
		EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
		const std::vector<std::uint8_t> previous_bytes = { 'p', 'r', 'e', 'v', 'i', 'o', 'u', 's' };
		EXPECT_EQ(read_bytes(path), previous_bytes);
		std::filesystem::remove(path);
	}
}

TEST_CASE(multiway_profiled_stream_failures_preserve_destination)
{
	const auto identity = snapshot().identity;
	const auto rows = full_rows(identity);
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(identity, {}, 1U, 1U);
	const texas::MultiwayFullBlueprintStreamFailurePoint points[] = {
		texas::MultiwayFullBlueprintStreamFailurePoint::Write,
		texas::MultiwayFullBlueprintStreamFailurePoint::Close,
		texas::MultiwayFullBlueprintStreamFailurePoint::Publication,
	};
	for (const auto point : points)
	{
		const auto path = artifact_path("profiled_stream_failure");
		{
			std::ofstream old_value(path, std::ios::binary);
			old_value << "previous";
		}
		{
			texas::solver::multiway::MultiwayFullBlueprintWriterProfile profile;
			texas::MultiwayFullBlueprintStreamWriter writer(path, plan, { true, point, &profile });
			if (point == texas::MultiwayFullBlueprintStreamFailurePoint::Write)
				EXPECT_THROW(writer.write_row(texas::MultiwayFullBlueprintRowView(rows.front())), std::runtime_error);
			else
			{
				writer.write_row(texas::MultiwayFullBlueprintRowView(rows.front()));
				EXPECT_THROW(writer.finish(), std::runtime_error);
				EXPECT_EQ(profile.sampled_rows, 1U);
			}
		}
		EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
		const std::vector<std::uint8_t> previous_bytes = { 'p', 'r', 'e', 'v', 'i', 'o', 'u', 's' };
		EXPECT_EQ(read_bytes(path), previous_bytes);
		std::filesystem::remove(path);
	}
}

TEST_CASE(multiway_full_blueprint_stream_rejects_invalid_identity_plan)
{
	texas::MultiwayFullBlueprintStreamPlan plan;
	EXPECT_THROW(plan.validate(), std::invalid_argument);
}

TEST_CASE(multiway_artifact_rejects_checkpoint_integrity_mismatch)
{
	const auto expected = snapshot();
	const auto path = artifact_path("integrity");
	texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
	{
		std::ofstream out(path, std::ios::binary | std::ios::app);
		out.put('\0');
	}
	EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
	std::filesystem::remove(path);
	std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_full_blueprint_artifact_round_trips_and_rejects_corruption)
{
	const auto expected = snapshot();
	const auto path = artifact_path("full_blueprint");
	texas::MultiwayFullBlueprintArtifact artifact;
	artifact.identity = expected.identity;
	artifact.training = expected.training;
	artifact.rows = { { { { 31U }, 0 }, 0U, 17U, { { { texas::MultiwayAction::Check, 0U, 0, 17U }, 65535U } } } };
	texas::MultiwayFullBlueprintArtifacts::save_atomic(path, artifact);
	const auto loaded = texas::MultiwayFullBlueprintArtifacts::load_verified(path, expected.identity);
	EXPECT_EQ(loaded.rows.size(), std::size_t{ 1U });
	EXPECT_EQ(loaded.rows.front().infoset.public_state.value, 31U);

	{
		std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
		out.seekp(-1, std::ios::end);
		out.put('\0');
	}
	EXPECT_THROW(texas::MultiwayFullBlueprintArtifacts::load_verified(path, expected.identity), std::invalid_argument);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_full_blueprint_rejects_unsafe_row_capacity_before_resize)
{
	const auto expected = snapshot();
	const auto path = artifact_path("full_blueprint_row_budget");
	texas::MultiwayFullBlueprintArtifact artifact;
	artifact.identity = expected.identity;
	artifact.training = expected.training;
	texas::MultiwayFullBlueprintArtifacts::save_atomic(path, artifact);

	// The row count follows the fixed magic, schema, identity, and training
	// fields. A large count must fail the memory preflight before rows.resize.
	std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
	out.seekp(168, std::ios::beg);
	const std::uint64_t row_count = 100'000'000U;
	for (std::uint32_t byte = 0U; byte < sizeof(row_count); ++byte)
	{
		out.put(static_cast<char>((row_count >> (byte * 8U)) & 0xffU));
	}
	out.close();

	EXPECT_THROW(
		texas::MultiwayFullBlueprintArtifacts::load_verified(path, expected.identity),
		std::length_error);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_artifact_rejects_mismatched_identity)
{
	const auto expected = snapshot();
	const auto path = artifact_path("identity");
	texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
	auto different_config = texas::MultiwayBlueprintConfig{};
	++different_config.resolver_schema_version;
	EXPECT_THROW(
		texas::MultiwayBlueprintArtifacts::load_verified(path, texas::make_multiway_model_identity(different_config)),
		std::invalid_argument);
	std::filesystem::remove(path);
	std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_manifest_rejects_each_semantic_identity_mutation)
{
	const auto expected = snapshot();
	const auto path = artifact_path("all_identity_inputs");
	texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
	const auto expect_rejected = [&path](const auto& mutate) {
		texas::MultiwayBlueprintConfig config;
		mutate(config);
		EXPECT_THROW(
			texas::MultiwayBlueprintArtifacts::load_verified(
				path, texas::make_multiway_model_identity(config)),
			std::invalid_argument);
	};

	expect_rejected([](auto& config) { --config.player_count; });
	expect_rejected([](auto& config) { ++config.initial_stack_chips; });
	expect_rejected([](auto& config) { ++config.small_blind_chips; });
	expect_rejected([](auto& config) { ++config.big_blind_chips; });
	expect_rejected([](auto& config) { ++config.ante_chips; });
	expect_rejected([](auto& config) {
		config.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
		config.rake_policy.basis_points = 100U;
		config.rake_policy.cap = 100;
	});
	expect_rejected([](auto& config) { ++config.flop_bucket_count; });
	expect_rejected([](auto& config) { ++config.turn_bucket_count; });
	expect_rejected([](auto& config) { ++config.river_bucket_count; });
	expect_rejected([](auto& config) { ++config.action_abstraction_version; });
	expect_rejected([](auto& config) { ++config.bucket_model_version; });
	expect_rejected([](auto& config) { ++config.terminal_model_version; });
	expect_rejected([](auto& config) { ++config.rules_profile_version; });
	expect_rejected([](auto& config) { ++config.resolver_schema_version; });
	expect_rejected([](auto& config) { ++config.code_schema_version; });
	expect_rejected([](auto& config) { ++config.range_semantics_version; });
	expect_rejected([](auto& config) { ++config.future_bucket_model_version; });
	expect_rejected([](auto& config) { ++config.off_tree_policy_version; });
	expect_rejected([](auto& config) { ++config.continuation_policy_version; });
	expect_rejected([](auto& config) { ++config.runtime_search_schema_version; });
	std::filesystem::remove(path);
	std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_artifact_selects_known_good_fallback)
{
	const auto expected = snapshot();
	const auto primary = artifact_path("primary");
	const auto fallback = artifact_path("fallback");
	texas::MultiwayBlueprintArtifacts::save_atomic(fallback, expected);

	const auto loaded = texas::MultiwayBlueprintArtifacts::load_with_fallback(
		primary, fallback, expected.identity);
	EXPECT_EQ(loaded.source, texas::MultiwayArtifactSource::KnownGoodFallback);
	EXPECT_EQ(loaded.snapshot.identity, expected.identity);
	std::filesystem::remove(fallback);
	std::filesystem::remove(fallback.string() + ".manifest");
}

TEST_CASE(multiway_public_decision_log_redacts_private_resolver_inputs)
{
	const auto expected = snapshot();
	texas::MultiwayResolverRequest request;
	request.blueprint_identity = expected.identity;
	request.hero_seat = 2;
	request.hero_cards = { 12U, 31U };
	request.opponent_ranges = { { 1, { { { 4U, 5U }, 1.0 } } } };

	texas::MultiwayResolverResult result;
	result.sampled_action = { texas::MultiwayAction::Check, 0U, 0, 17U };
	result.has_sampled_action = true;
	result.policy = { { result.sampled_action, 1.0 } };
	result.diagnostics.status = texas::MultiwayResolverStatus::Solved;
	result.diagnostics.policy_provenance = texas::MultiwayPolicyProvenance::RuntimeSearch;
	result.diagnostics.search_engine = texas::MultiwayResolverEngine::RootExternalSamplingMCCFR;
	result.diagnostics.search_engine_version = texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION;
	result.diagnostics.policy_normalized = true;
	result.diagnostics.resolved_public_state_id = 71U;
	const auto log = texas::make_multiway_public_decision_log(request, result, 1U);

	log.validate();
	EXPECT_EQ(log.acting_seat, 2);
	EXPECT_EQ(log.policy.size(), 1U);
	EXPECT_EQ(log.policy.front().probability, 65535U);
	EXPECT_EQ(
		log.policy_provenance,
		texas::MultiwayPolicyProvenance::RuntimeSearch);
	EXPECT_EQ(
		log.search_engine,
		texas::MultiwayResolverEngine::RootExternalSamplingMCCFR);
	EXPECT_EQ(log.search_engine_version, texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION);

	auto malformed = log;
	malformed.policy.front().probability = 0U;
	EXPECT_THROW(malformed.validate(), std::invalid_argument);

	auto no_provenance = result;
	no_provenance.diagnostics.policy_provenance = texas::MultiwayPolicyProvenance::None;
	EXPECT_THROW(
		texas::make_multiway_public_decision_log(request, no_provenance, 1U),
		std::invalid_argument);
}

TEST_CASE(multiway_public_decision_log_accepts_forward_compatible_delivery_statuses)
{
	const auto expected = snapshot();
	texas::MultiwayPublicDecisionLog log;
	log.identity = expected.identity;
	log.public_state_id = expected.public_state.value;
	log.decision_index = 1U;
	log.acting_seat = 0;
	log.sampled_action = expected.actions.front().action;
	log.policy_provenance = texas::MultiwayPolicyProvenance::StaticLegalFallback;
	log.search_engine = texas::MultiwayResolverEngine::NoRuntimeSearch;
	log.search_engine_version = texas::MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION;
	log.used_fallback = true;
	log.policy = { { log.sampled_action, 65535U } };

	const texas::MultiwayResolverStatus accepted_statuses[] = {
		texas::MultiwayResolverStatus::Solved,
		texas::MultiwayResolverStatus::Partial,
		texas::MultiwayResolverStatus::DeadlineFallback,
		texas::MultiwayResolverStatus::ArtifactMismatch,
		texas::MultiwayResolverStatus::BucketUnavailable,
		texas::MultiwayResolverStatus::ResourceExhausted,
		texas::MultiwayResolverStatus::RejectedByBudget,
	};
	for (const auto status : accepted_statuses)
	{
		log.resolver_status = status;
		log.validate();
	}

	log.resolver_status = texas::MultiwayResolverStatus::InvalidRequest;
	EXPECT_THROW(log.validate(), std::invalid_argument);
}

TEST_CASE(multiway_protected_replay_record_is_deterministic_and_sealed)
{
	const auto expected = snapshot();
	auto history = texas::MultiwayHandHistory::from_rules(texas::MultiwayGameRules::standard_6max(), 0, 44U);
	history.events.push_back({ texas::MultiwayReplayEventKind::Decision,
		{ 0, texas::MultiwayAction::Fold, 0, 93U },
		texas::Street::Flop,
		0 });

	const auto first = texas::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
	const auto second = texas::MultiwayProtectedReplayRecord::from_history(expected.identity, history);
	EXPECT_EQ(first.integrity_hash, second.integrity_hash);
	EXPECT_EQ(first.decision_seeds.front(), 93U);

	auto altered = first;
	altered.decision_seeds.front() = 94U;
	EXPECT_THROW(altered.validate(), std::invalid_argument);
}

TEST_CASE(multiway_artifact_fails_closed_for_malformed_manifest)
{
	const auto expected = snapshot();
	const auto path = artifact_path("malformed_manifest");
	texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
	{
		std::ofstream out(path.string() + ".manifest", std::ios::binary | std::ios::trunc);
		out.write("broken", 6);
	}
	EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
	std::filesystem::remove(path);
	std::filesystem::remove(path.string() + ".manifest");
}

TEST_CASE(multiway_atomic_publish_replaces_destination_without_leaving_temporary_files)
{
	const auto destination = artifact_path("atomic_destination");
	const auto temporary = artifact_path("atomic_temporary");
	{
		std::ofstream old_value(destination, std::ios::binary);
		old_value << "old";
		std::ofstream new_value(temporary, std::ios::binary);
		new_value << "new";
	}

	const auto publication_mode = texas::core::publish_atomic_replace(temporary, destination, "atomic publish failed");
	EXPECT_TRUE(publication_mode != texas::core::AtomicPublishMode::Unknown);

	std::string value;
	{
		std::ifstream published(destination, std::ios::binary);
		published >> value;
		EXPECT_EQ(value, "new");
	}
	EXPECT_TRUE(!std::filesystem::exists(temporary));
	EXPECT_TRUE(!std::filesystem::exists(destination.string() + ".previous"));
	std::filesystem::remove(destination);

	{
		std::ofstream existing(destination, std::ios::binary);
		existing << "preserved";
	}
	EXPECT_THROW(
		texas::core::publish_atomic_replace(temporary, destination, "atomic publish failed"),
		std::runtime_error);
	{
		std::ifstream restored(destination, std::ios::binary);
		restored >> value;
		EXPECT_EQ(value, "preserved");
	}
	EXPECT_TRUE(!std::filesystem::exists(destination.string() + ".previous"));
	std::filesystem::remove(destination);
}

TEST_CASE(multiway_artifact_rejects_schema_v3_manifest_magic)
{
	const auto expected = snapshot();
	const auto path = artifact_path("schema_v3_manifest");
	const auto manifest = path.string() + ".manifest";
	texas::MultiwayBlueprintArtifacts::save_atomic(path, expected);
	{
		std::ifstream in(manifest, std::ios::binary);
		std::array<char, 8> magic = {};
		in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
		EXPECT_TRUE(static_cast<bool>(in));
		EXPECT_EQ(magic[7], '4');
	}
	{
		std::fstream out(manifest, std::ios::binary | std::ios::in | std::ios::out);
		out.seekp(7);
		out.put('3');
	}
	EXPECT_THROW(texas::MultiwayBlueprintArtifacts::load_verified(path, expected.identity), std::runtime_error);
	std::filesystem::remove(path);
	std::filesystem::remove(manifest);
}
