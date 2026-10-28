#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "core/canonical_combo.hpp"
#include "core/fingerprint.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

	texas::solver::multiway::MultiwayModelIdentity identity()
	{
		texas::solver::multiway::MultiwayBlueprintConfig config;
		return texas::solver::multiway::make_multiway_model_identity(config);
	}

	std::vector<texas::solver::multiway::MultiwayBucketBoardRequest> boards()
	{
		return {
			{ texas::core::Street::Flop, { 0U, 5U, 10U } },
			{ texas::core::Street::Turn, { 0U, 5U, 10U, 15U } },
			{ texas::core::Street::River, { 0U, 5U, 10U, 15U, 20U } },
		};
	}

	void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
	{
		for (unsigned shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
	{
		for (std::size_t byte = 0U; byte < 4U; ++byte)
			bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
	}

	std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
	{
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
	}

	void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value)
	{
		for (std::size_t byte = 0U; byte < 8U; ++byte)
			bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
	}

	std::uint64_t hash_section(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length)
	{
		auto hash = texas::core::fingerprint::FNV1A_OFFSET;
		for (std::size_t index = 0U; index < length; ++index)
			texas::core::fingerprint::append_u8(hash, bytes[offset + index]);
		return hash;
	}

	std::vector<std::uint8_t> schema4_bytes(const texas::solver::multiway::MultiwayBucketRegistry& registry)
	{
		const auto compact = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
		std::vector<std::uint8_t> legacy(compact.begin(), compact.begin() + 4U + 4U + 13U * 8U);
		legacy[4] = 4U;
		legacy[5] = legacy[6] = legacy[7] = 0U;
		append_u32(legacy, static_cast<std::uint32_t>(registry.tables().size()));
		for (const auto& table : registry.tables())
		{
			legacy.push_back(static_cast<std::uint8_t>(table.street()));
			legacy.push_back(static_cast<std::uint8_t>(table.canonical_board().size()));
			legacy.insert(legacy.end(), table.canonical_board().begin(), table.canonical_board().end());
			append_u32(legacy, table.bucket_count());
			for (std::size_t id = 0U; id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
				append_u32(legacy, table.assignment_at(id));
		}
		return legacy;
	}

} // namespace

TEST_CASE(multiway_bucket_artifact_builds_canonical_coverage_with_baseline_counts)
{
	const auto profile = texas::solver::multiway::MultiwayBucketBaselineProfile::standard();
	const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards(), profile);

	texas::solver::multiway::validate_multiway_bucket_coverage(registry, boards());
	EXPECT_EQ(registry.table(texas::core::Street::Flop, boards()[0].canonical_board).bucket_count(), 96U);
	EXPECT_EQ(registry.table(texas::core::Street::Turn, boards()[1].canonical_board).bucket_count(), 128U);
	EXPECT_EQ(registry.table(texas::core::Street::River, boards()[2].canonical_board).bucket_count(), 192U);
}

TEST_CASE(multiway_bucket_artifact_assignments_are_stable_for_the_same_inputs)
{
	const auto first = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto second = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const std::array<std::uint8_t, 2> hole = { 1U, 2U };

	EXPECT_EQ(first.lookup(texas::core::Street::Flop, boards()[0].canonical_board, hole),
		second.lookup(texas::core::Street::Flop, boards()[0].canonical_board, hole));
	EXPECT_EQ(texas::solver::multiway::serialize_multiway_bucket_registry(first),
		texas::solver::multiway::serialize_multiway_bucket_registry(second));
}

TEST_CASE(multiway_bucket_artifact_round_trips_and_rejects_corruption)
{
	const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
	const auto restored = texas::solver::multiway::deserialize_multiway_bucket_registry(bytes);

	EXPECT_EQ(restored.identity(), registry.identity());
	EXPECT_EQ(restored.lookup(texas::core::Street::River, boards()[2].canonical_board, { 1U, 2U }),
		registry.lookup(texas::core::Street::River, boards()[2].canonical_board, { 1U, 2U }));

	bytes.pop_back();
	EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_schema4_and_schema5_match_exhaustive_assignments)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto schema4 = texas::solver::multiway::deserialize_multiway_bucket_registry(schema4_bytes(source));
	const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	const auto schema5 = texas::solver::multiway::deserialize_multiway_bucket_registry(bytes);
	const auto schema4_path = std::filesystem::temp_directory_path() / "texas_solver_bucket_exhaustive_schema4_mapped_test.bin";
	const auto schema4_file = schema4_bytes(source);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_exhaustive_mapped_test.bin";
	{
		std::ofstream output(schema4_path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(schema4_file.data()), static_cast<std::streamsize>(schema4_file.size()));
	}
	{
		auto schema4_mapped = texas::solver::multiway::load_multiway_bucket_registry(schema4_path);
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		auto mapped = texas::solver::multiway::load_multiway_bucket_registry(path);
		for (const auto& board : boards())
		{
			const auto& reference = source.table(board.street, board.canonical_board);
			const auto& old_table = schema4.table(board.street, board.canonical_board);
			const auto& old_mapped_table = schema4_mapped.table(board.street, board.canonical_board);
			const auto& compact_table = schema5.table(board.street, board.canonical_board);
			const auto& mapped_table = mapped.table(board.street, board.canonical_board);
			EXPECT_EQ(mapped_table.identity(), source.identity());
			for (std::size_t id = 0U; id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
			{
				EXPECT_EQ(old_table.assignment_at(id), reference.assignment_at(id));
				EXPECT_EQ(old_mapped_table.assignment_at(id), reference.assignment_at(id));
				EXPECT_EQ(compact_table.assignment_at(id), reference.assignment_at(id));
				EXPECT_EQ(mapped_table.assignment_at(id), reference.assignment_at(id));
				const auto& hole = texas::core::canonical_combos().cards(static_cast<texas::core::CanonicalComboId>(id));
				if (reference.assignment_at(id) == texas::solver::multiway::MULTIWAY_INVALID_BUCKET)
				{
					EXPECT_THROW(old_table.lookup(hole), std::invalid_argument);
					EXPECT_THROW(old_mapped_table.lookup(hole), std::invalid_argument);
					EXPECT_THROW(compact_table.lookup(hole), std::invalid_argument);
					EXPECT_THROW(mapped_table.lookup(hole), std::invalid_argument);
				}
				else
				{
					EXPECT_EQ(old_table.lookup(hole), reference.assignment_at(id));
					EXPECT_EQ(old_mapped_table.lookup(hole), reference.assignment_at(id));
					EXPECT_EQ(compact_table.lookup(hole), reference.assignment_at(id));
					EXPECT_EQ(mapped_table.lookup(hole), reference.assignment_at(id));
				}
			}
		}
	}
	std::filesystem::remove(path);
	std::filesystem::remove(schema4_path);
}

TEST_CASE(multiway_mapped_bucket_views_fit_metadata_budget_for_full_catalog)
{
	const auto table_count = texas::solver::multiway::multiway_bucket_board_count(texas::core::Street::Flop) + texas::solver::multiway::multiway_bucket_board_count(texas::core::Street::Turn) + texas::solver::multiway::multiway_bucket_board_count(texas::core::Street::River);
	const auto bytes = static_cast<std::uint64_t>(table_count) * sizeof(texas::solver::multiway::MultiwayBucketTable) + sizeof(texas::solver::multiway::MultiwayModelIdentity);
	EXPECT_TRUE(sizeof(texas::solver::multiway::MultiwayBucketTable) <= 40U);
	EXPECT_TRUE(bytes < 128ULL * 1024ULL * 1024ULL);
}

TEST_CASE(multiway_bucket_artifact_selects_width_at_reserved_sentinel_boundaries)
{
	for (const auto fixture : std::array<std::pair<std::uint32_t, std::uint32_t>, 4>{
			 std::pair<std::uint32_t, std::uint32_t>{ 256U, 1U }, { 257U, 2U }, { 65536U, 2U }, { 65537U, 4U } })
	{
		auto table = texas::solver::multiway::build_multiway_baseline_bucket_table(
			identity(), texas::core::Street::Flop, boards()[0].canonical_board);
		std::vector<std::uint32_t> values(table.assignments().begin(), table.assignments().end());
		const std::array<std::uint8_t, 2U> highest_bucket_hole = { 1U, 2U };
		values[texas::solver::multiway::MultiwayBucketTable::hole_index(highest_bucket_hole)] = fixture.first - 1U;
		table = texas::solver::multiway::MultiwayBucketTable(identity(), texas::core::Street::Flop,
			boards()[0].canonical_board, fixture.first, std::move(values));
		texas::solver::multiway::MultiwayBucketRegistry registry({ std::move(table) });
		const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
		EXPECT_EQ(read_u32(bytes, 116U), fixture.second);
		const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_width_test.bin";
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		{
			const auto mapped = texas::solver::multiway::load_multiway_bucket_registry(path);
			const auto& expected = registry.table(texas::core::Street::Flop, boards()[0].canonical_board);
			const auto& actual = mapped.table(texas::core::Street::Flop, boards()[0].canonical_board);
			for (std::size_t id = 0U; id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
				EXPECT_EQ(actual.assignment_at(id), expected.assignment_at(id));
			EXPECT_EQ(actual.lookup(highest_bucket_hole), fixture.first - 1U);
		}
		std::filesystem::remove(path);
	}
}

TEST_CASE(multiway_bucket_artifact_rejects_bad_schema5_section_hash)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	bytes.back() ^= 0x1U;
	EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_rejects_schema5_offset_overflow)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	for (std::size_t index = 120U; index < 128U; ++index)
		bytes[index] = 0xffU;
	EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_bad_offset_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	EXPECT_THROW(texas::solver::multiway::load_multiway_bucket_registry(path), std::invalid_argument);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_rejects_duplicate_schema5_directory_board)
{
	const std::vector<texas::solver::multiway::MultiwayBucketBoardRequest> one_board = {
		{ texas::core::Street::Flop, { 0U, 5U, 10U } },
	};
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), one_board);
	const auto single = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	constexpr std::size_t header_size = 168U;
	constexpr std::size_t old_directory = 168U;
	constexpr std::size_t old_assignment_offset = 200U;
	const auto one_assignment_length = static_cast<std::size_t>(read_u32(single, 116U)) * texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT;
	const auto new_assignment_offset = header_size + 2U * 32U;
	std::vector<std::uint8_t> duplicate(single.begin(), single.begin() + header_size);
	duplicate.insert(duplicate.end(), single.begin() + old_directory, single.begin() + old_directory + 32U);
	duplicate.insert(duplicate.end(), single.begin() + old_directory, single.begin() + old_directory + 32U);
	write_u32(duplicate, 112U, 2U);
	write_u64(duplicate, 128U, 64U);
	write_u64(duplicate, 136U, new_assignment_offset);
	write_u64(duplicate, 144U, 2U * one_assignment_length);
	write_u64(duplicate, header_size + 16U, new_assignment_offset);
	write_u64(duplicate, header_size + 32U + 16U, new_assignment_offset + one_assignment_length);
	duplicate.insert(duplicate.end(), single.begin() + old_assignment_offset, single.end());
	duplicate.insert(duplicate.end(), single.begin() + old_assignment_offset, single.end());
	write_u64(duplicate, 152U, hash_section(duplicate, header_size, 64U));
	write_u64(duplicate, 160U, hash_section(duplicate, new_assignment_offset, 2U * one_assignment_length));
	EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(duplicate), std::invalid_argument);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_duplicate_board_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(duplicate.data()), static_cast<std::streamsize>(duplicate.size()));
	}
	EXPECT_THROW(texas::solver::multiway::load_multiway_bucket_registry(path), std::invalid_argument);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_inspector_rejects_identity_mismatch)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_wrong_identity_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	auto different_config = texas::solver::multiway::MultiwayBlueprintConfig{};
	different_config.player_count = 4U;
	const auto wrong_identity = texas::solver::multiway::make_multiway_model_identity(different_config);
	EXPECT_THROW(texas::solver::multiway::inspect_multiway_bucket_artifact(path, wrong_identity), std::invalid_argument);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_inspector_reads_schema5_content)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_inspect_schema5_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	const auto inspected = texas::solver::multiway::inspect_multiway_bucket_artifact(path, identity());
	EXPECT_EQ(inspected.flop_tables, 1U);
	EXPECT_EQ(inspected.turn_tables, 1U);
	EXPECT_EQ(inspected.river_tables, 1U);
	EXPECT_EQ(inspected.live_assignments, 3U * 1326U - (150U + 198U + 245U));
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_stream_load_matches_memory_deserializer)
{
	const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_stream_load_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	{
		const auto streamed = texas::solver::multiway::load_multiway_bucket_registry(path);
		EXPECT_TRUE(streamed.is_mapped());
		EXPECT_TRUE(streamed.mapped_artifact_bytes() == bytes.size());
		EXPECT_TRUE(streamed.owned_memory_bytes() < streamed.mapped_artifact_bytes());
		const auto expected_metadata_bytes = streamed.tables().capacity() * sizeof(texas::solver::multiway::MultiwayBucketTable) + sizeof(texas::solver::multiway::MultiwayModelIdentity);
		EXPECT_EQ(streamed.owned_memory_bytes(), expected_metadata_bytes);
		EXPECT_EQ(streamed.memory_snapshot().owned_metadata.retained_capacity_bytes, expected_metadata_bytes);
		EXPECT_EQ(streamed.identity(), registry.identity());
		EXPECT_EQ(streamed.lookup(texas::core::Street::Turn, boards()[1].canonical_board, { 1U, 2U }),
			registry.lookup(texas::core::Street::Turn, boards()[1].canonical_board, { 1U, 2U }));
		EXPECT_EQ(texas::solver::multiway::serialize_multiway_bucket_registry(streamed), bytes);
		for (std::size_t table_index = 0U; table_index < boards().size(); ++table_index)
		{
			const auto& expected = registry.table(boards()[table_index].street, boards()[table_index].canonical_board);
			const auto& mapped = streamed.table(boards()[table_index].street, boards()[table_index].canonical_board);
			for (std::size_t id = 0U; id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
				EXPECT_EQ(mapped.assignment_at(id), expected.assignments()[id]);
		}
	}
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_mapped_registry_survives_loader_return)
{
	const auto source = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), boards());
	const auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(source);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_bucket_mapped_lifetime_test.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	{
		auto loaded = texas::solver::multiway::load_multiway_bucket_registry(path);
		EXPECT_EQ(loaded.lookup(texas::core::Street::Flop, boards()[0].canonical_board, { 1U, 2U }),
			source.lookup(texas::core::Street::Flop, boards()[0].canonical_board, { 1U, 2U }));
	}
	std::filesystem::remove(path);
}

TEST_CASE(multiway_bucket_artifact_rejects_missing_board_and_legacy_schema)
{
	const std::vector<texas::solver::multiway::MultiwayBucketBoardRequest> flop_only = {
		{ texas::core::Street::Flop, { 0U, 5U, 10U } },
	};
	const auto registry = texas::solver::multiway::build_multiway_baseline_bucket_registry(identity(), flop_only);
	EXPECT_THROW(texas::solver::multiway::validate_multiway_bucket_coverage(registry, boards()), std::out_of_range);

	auto bytes = texas::solver::multiway::serialize_multiway_bucket_registry(registry);
	bytes[4] = 2U;
	EXPECT_THROW(texas::solver::multiway::deserialize_multiway_bucket_registry(bytes), std::invalid_argument);
}

TEST_CASE(multiway_bucket_artifact_requires_sorted_canonical_boards)
{
	const std::vector<std::uint8_t> noncanonical = { 10U, 0U, 5U };
	EXPECT_TRUE(!texas::solver::multiway::is_multiway_canonical_board(texas::core::Street::Flop, noncanonical));
	EXPECT_THROW(texas::solver::multiway::build_multiway_baseline_bucket_table(
					 identity(), texas::core::Street::Flop, noncanonical,
					 texas::solver::multiway::MultiwayBucketBaselineProfile::standard()),
		std::invalid_argument);
}
