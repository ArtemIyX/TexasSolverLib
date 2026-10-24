#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstring>
#include <cstdint>
#include <utility>

namespace
{

	std::uint64_t probability_bits(double value)
	{
		std::uint64_t bits = 0U;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

}

TEST_CASE(multiway_compact_storage_memory_snapshot_attributes_each_storage_array)
{
	using namespace texas::solver::multiway;
	static_assert(noexcept(std::declval<const MultiwayCompactStorage&>().memory_snapshot()));
	const MultiwayInfosetId infoset{ { 33U }, 0 };
	MultiwayCompactStorage storage(4U, 32U, 4U);
	const auto empty = storage.memory_snapshot();
	EXPECT_EQ(empty.row_metadata.live_element_bytes, 0U);
	EXPECT_EQ(empty.row_metadata.retained_capacity_bytes, 4U * sizeof(MultiwaySparseRowMetadata));
	EXPECT_EQ(empty.page_metadata.live_element_bytes, 0U);
	storage.admit_row({ infoset, 5U, 3U });
	const auto populated = storage.memory_snapshot();
	EXPECT_EQ(populated.row_metadata.live_element_bytes, sizeof(MultiwaySparseRowMetadata));
	EXPECT_TRUE(populated.page_metadata.live_element_bytes > 0U);
	EXPECT_EQ(populated.regret_cells.live_element_bytes, 4U * 3U * sizeof(std::int32_t));
	EXPECT_EQ(populated.strategy_cells.live_element_bytes, 4U * 3U * sizeof(std::uint64_t));
	EXPECT_EQ(populated.total.retained_capacity_bytes,
		populated.row_metadata.retained_capacity_bytes + populated.page_metadata.retained_capacity_bytes +
		populated.regret_cells.retained_capacity_bytes + populated.strategy_cells.retained_capacity_bytes);
}

TEST_CASE(multiway_compact_storage_saturates_regrets_and_normalizes_policies)
{
	texas::solver::multiway::MultiwayCompactStorage storage(2U, 6U);
	const texas::solver::multiway::MultiwayInfosetId infoset{ { 7U }, 0 };
	storage.admit_row({ infoset, 2U, 3U });
	storage.apply_delta(infoset, 0U, 0U, 10'000'000.0, 2.0);
	storage.apply_delta(infoset, 0U, 1U, -10'000'000.0, 1.0);
	const auto current = storage.regret_matched_strategy(infoset, 0U);
	const auto average = storage.average_strategy(infoset, 0U);
	EXPECT_NEAR(current[0] + current[1] + current[2], 1.0, 1e-12);
	EXPECT_NEAR(average[0] + average[1] + average[2], 1.0, 1e-12);
	EXPECT_TRUE(current[0] > 0.99);
	EXPECT_NEAR(average[0], 2.0 / 3.0, 1e-6);
	EXPECT_TRUE(storage.memory_bytes() < 256U);
}

TEST_CASE(multiway_compact_storage_rejects_conflicting_rows)
{
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 3U);
	const texas::solver::multiway::MultiwayInfosetId infoset{ { 7U }, 0 };
	storage.admit_row({ infoset, 1U, 3U });
	EXPECT_THROW(storage.admit_row({ infoset, 3U, 1U }), std::invalid_argument);
}

TEST_CASE(multiway_compact_storage_matches_reference_policy_shape)
{
	const texas::solver::multiway::MultiwayInfosetId infoset{ { 9U }, 1 };
	texas::solver::multiway::MultiwayCompactStorage compact(1U, 4U);
	compact.admit_row({ infoset, 2U, 2U });
	compact.apply_delta(infoset, 0U, 0U, 1.0, 3.0);
	compact.apply_delta(infoset, 0U, 1U, -0.5, 1.0);
	EXPECT_TRUE(!compact.action_below_regret(infoset, 0U, 0U, 0.0));
	EXPECT_TRUE(compact.action_below_regret(infoset, 0U, 1U, 0.0));
	const auto current = compact.regret_matched_strategy(infoset, 0U);
	const auto average = compact.average_strategy(infoset, 0U);
	EXPECT_NEAR(current[0], 1.0, 1e-12);
	EXPECT_NEAR(current[1], 0.0, 1e-12);
	EXPECT_NEAR(average[0], 0.75, 1e-6);
	EXPECT_NEAR(average[1], 0.25, 1e-6);
}

TEST_CASE(multiway_compact_storage_allocation_free_policy_matches_vector_api)
{
	const texas::solver::multiway::MultiwayInfosetId infoset{ { 10U }, 1 };
	texas::solver::multiway::MultiwayCompactStorage compact(1U, 3U);
	compact.admit_row({ infoset, 1U, 3U });
	compact.apply_delta(infoset, 0U, 0U, 3.0, 0.0);
	compact.apply_delta(infoset, 0U, 1U, 1.0, 0.0);
	compact.apply_delta(infoset, 0U, 2U, -2.0, 0.0);
	const auto expected = compact.regret_matched_strategy(infoset, 0U);
	std::array<texas::Probability, 3> actual{};
	compact.regret_matched_strategy_into(infoset, 0U, actual.data(), actual.size());
	for (std::size_t action = 0U; action < actual.size(); ++action)
	{
		EXPECT_NEAR(actual[action], expected[action], 0.0);
	}
	EXPECT_THROW(compact.regret_matched_strategy_into(infoset, 0U, actual.data(), 2U),
		std::invalid_argument);
}

TEST_CASE(multiway_compact_storage_average_strategy_into_preserves_bits)
{
	const texas::MultiwayInfosetId infoset{ { 101U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 24U, 4U);
	storage.admit_row({ infoset, 6U, 4U });
	storage.apply_delta(infoset, 0U, 0U, 0.0, 1.0);
	storage.apply_delta(infoset, 0U, 1U, 0.0, 3.0);
	storage.apply_delta(infoset, 4U, 2U, 0.0, 7.0);

	for (std::uint32_t bucket : { 0U, 1U, 4U, 5U })
	{
		const auto expected = storage.average_strategy(infoset, bucket);
		std::array<texas::Probability, 4U> actual{};
		storage.average_strategy_into(infoset, bucket, actual.data(), actual.size());
		for (std::size_t action = 0U; action < actual.size(); ++action)
			EXPECT_EQ(probability_bits(actual[action]), probability_bits(expected[action]));
	}
}

TEST_CASE(multiway_float64_storage_average_strategy_into_preserves_uniform_bits)
{
	const texas::MultiwayInfosetId infoset{ { 102U }, 0 };
	texas::solver::multiway::MultiwaySparseRowStorage storage(1U, 6U);
	storage.admit_rows({ { infoset, 2U, 3U } }, { 1U });
	for (std::uint32_t bucket = 0U; bucket < 2U; ++bucket)
	{
		const auto expected = storage.average_strategy(infoset, bucket);
		std::array<texas::Probability, 3U> actual{};
		storage.average_strategy_into(infoset, bucket, actual.data(), actual.size());
		for (std::size_t action = 0U; action < actual.size(); ++action)
			EXPECT_EQ(probability_bits(actual[action]), probability_bits(expected[action]));
	}
}

TEST_CASE(multiway_compact_storage_pruning_keeps_configured_floor)
{
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 2U);
	const texas::solver::multiway::MultiwayInfosetId infoset{ { 11U }, 0 };
	storage.admit_row({ infoset, 1U, 2U });
	storage.apply_delta(infoset, 0U, 0U, -2.0, 0.0);
	storage.apply_delta(infoset, 0U, 1U, 1.0, 0.0);
	storage.prune_negative_regrets(-1.0, -0.25);
	const auto policy = storage.regret_matched_strategy(infoset, 0U);
	EXPECT_NEAR(policy[0], 0.0, 1e-12);
	EXPECT_NEAR(policy[1], 1.0, 1e-12);
}

TEST_CASE(multiway_compact_storage_saturates_extreme_finite_deltas)
{
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 1U);
	const texas::MultiwayInfosetId infoset{ { 1U }, 0 };
	storage.admit_row({ infoset, 1U, 1U });
	storage.apply_delta(infoset, 0U, 0U, 1.0e300, 1.0e300);
	EXPECT_TRUE(storage.regret_matched_strategy(infoset, 0U)[0] > 0.0);
	EXPECT_TRUE(storage.average_strategy(infoset, 0U)[0] == 1.0);
	storage.apply_delta(infoset, 0U, 0U, -1.0e300, 0.0);
	EXPECT_TRUE(storage.regret_matched_strategy(infoset, 0U)[0] == 1.0);
}

TEST_CASE(multiway_compact_storage_fused_discount_and_prune_matches_sequential_order)
{
	const texas::MultiwayInfosetId infoset{ { 12U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage fused(1U, 3U);
	texas::solver::multiway::MultiwayCompactStorage sequential(1U, 3U);
	fused.admit_row({ infoset, 1U, 3U });
	sequential.admit_row({ infoset, 1U, 3U });
	for (std::uint8_t action = 0U; action < 3U; ++action)
	{
		fused.apply_delta(infoset, 0U, action, static_cast<double>(action) - 1.5, 0.0);
		sequential.apply_delta(infoset, 0U, action, static_cast<double>(action) - 1.5, 0.0);
	}
	const auto fused_pruned = fused.scale_and_prune_regrets(0.5, -0.25, -0.1);
	sequential.scale_regrets(0.5);
	const auto sequential_pruned = sequential.prune_negative_regrets(-0.25, -0.1);
	EXPECT_EQ(fused_pruned, sequential_pruned);
	const auto fused_policy = fused.regret_matched_strategy(infoset, 0U);
	const auto sequential_policy = sequential.regret_matched_strategy(infoset, 0U);
	for (std::size_t action = 0U; action < fused_policy.size(); ++action)
	{
		EXPECT_NEAR(fused_policy[action], sequential_policy[action], 0.0);
	}
}

TEST_CASE(multiway_compact_storage_cached_row_delta_matches_infoset_lookup)
{
	texas::solver::multiway::MultiwayCompactStorage storage(2U, 16U);
	const texas::MultiwayInfosetId infoset{ { 13U }, 0 };
	storage.admit_row({ infoset, 2U, 2U });
	const auto* row = storage.metadata(infoset);
	EXPECT_TRUE(row != nullptr);
	storage.apply_delta(infoset, 0U, 0U, 1.25, 2.0);
	storage.apply_delta(*row, 0U, 1U, -0.5, 3.0);
	EXPECT_NEAR(storage.strategy_sums(infoset, 0U)[0], 2.0, 1e-9);
	EXPECT_NEAR(storage.strategy_sums(infoset, 0U)[1], 3.0, 1e-9);
}

TEST_CASE(multiway_compact_storage_admits_bucket_pages_lazily)
{
	const texas::MultiwayInfosetId infoset{ { 14U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 40U, 4U);
	storage.admit_row({ infoset, 10U, 3U });
	EXPECT_EQ(storage.page_count(), 1U);
	EXPECT_EQ(storage.active_cell_count(), 12U);
	EXPECT_TRUE(!storage.has_page(infoset, 2U));
	const auto uniform = storage.regret_matched_strategy(infoset, 8U);
	EXPECT_NEAR(uniform[0], 1.0 / 3.0, 1e-12);
	storage.admit_page(infoset, 2U);
	EXPECT_EQ(storage.page_count(), 2U);
	EXPECT_EQ(storage.active_cell_count(), 18U);
	EXPECT_TRUE(storage.has_page(infoset, 2U));
}

TEST_CASE(multiway_compact_storage_checkpoint_expands_unadmitted_pages_as_zeroes)
{
	const texas::MultiwayInfosetId infoset{ { 15U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 8U, 2U);
	storage.admit_row({ infoset, 4U, 2U });
	storage.apply_delta(infoset, 0U, 0U, 1.0, 2.0);
	texas::solver::multiway::MultiwaySparseStorageCheckpoint checkpoint;
	storage.export_checkpoint(checkpoint);
	EXPECT_EQ(checkpoint.shapes.size(), 1U);
	EXPECT_EQ(checkpoint.regrets.size(), 8U);
	EXPECT_NEAR(checkpoint.regrets[0], 1.0, 1e-9);
	EXPECT_NEAR(checkpoint.regrets[2], 0.0, 1e-9);
	EXPECT_NEAR(checkpoint.strategy_sums[0], 2.0, 1e-9);
	EXPECT_NEAR(checkpoint.strategy_sums[2], 0.0, 1e-9);
}

TEST_CASE(multiway_compact_storage_checkpoint_matches_scalar_action_major_reference)
{
	const texas::solver::multiway::MultiwayInfosetId later_state{ { 20U }, 1 };
	const texas::solver::multiway::MultiwayInfosetId earlier_state{ { 10U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage storage(2U, 12U, 2U);
	storage.admit_row({ later_state, 3U, 2U });
	storage.admit_row({ earlier_state, 3U, 2U });

	storage.apply_delta(earlier_state, 0U, 0U, 1.0, 2.0);
	storage.apply_delta(earlier_state, 1U, 0U, -0.5, 3.0);
	storage.apply_delta(earlier_state, 1U, 1U, 4.0, 5.0);
	storage.apply_delta(later_state, 0U, 1U, 2.0, 7.0);
	storage.apply_delta(later_state, 2U, 0U, -3.0, 11.0);
	storage.apply_delta(later_state, 2U, 1U, 6.0, 13.0);

	texas::solver::multiway::MultiwaySparseStorageCheckpoint checkpoint;
	storage.export_checkpoint(checkpoint);
	const std::array<double, 12> expected_regrets = {
		1.0,
		-0.5,
		0.0,
		0.0,
		4.0,
		0.0,
		0.0,
		0.0,
		-3.0,
		2.0,
		0.0,
		6.0,
	};
	const std::array<double, 12> expected_strategy_sums = {
		2.0,
		3.0,
		0.0,
		0.0,
		5.0,
		0.0,
		0.0,
		0.0,
		11.0,
		7.0,
		0.0,
		13.0,
	};

	EXPECT_EQ(checkpoint.shapes.size(), 2U);
	EXPECT_EQ(checkpoint.shapes[0].infoset, earlier_state);
	EXPECT_EQ(checkpoint.shapes[1].infoset, later_state);
	EXPECT_EQ(checkpoint.regrets.size(), expected_regrets.size());
	EXPECT_EQ(checkpoint.strategy_sums.size(), expected_strategy_sums.size());
	for (std::size_t index = 0U; index < expected_regrets.size(); ++index)
	{
		EXPECT_NEAR(checkpoint.regrets[index], expected_regrets[index], 1e-12);
		EXPECT_NEAR(checkpoint.strategy_sums[index], expected_strategy_sums[index], 1e-12);
	}
}

TEST_CASE(multiway_compact_storage_checkpoint_preserves_active_pages)
{
	const texas::MultiwayInfosetId infoset{ { 21U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage storage(1U, 16U, 2U);
	storage.admit_row({ infoset, 4U, 2U });
	storage.admit_page(infoset, 1U);

	texas::solver::multiway::MultiwaySparseStorageCheckpoint checkpoint;
	storage.export_checkpoint(checkpoint);
	EXPECT_EQ(checkpoint.active_pages.size(), 2U);
	EXPECT_EQ(checkpoint.active_pages[0].infoset, infoset);
	EXPECT_EQ(checkpoint.active_pages[0].page_index, 0U);
	EXPECT_EQ(checkpoint.active_pages[1].page_index, 1U);
}

TEST_CASE(multiway_compact_storage_checkpoint_preserves_raw_values)
{
	const texas::MultiwayInfosetId infoset{ { 22U }, 0 };
	texas::solver::multiway::MultiwayCompactStorage source(1U, 16U, 2U);
	source.admit_row({ infoset, 4U, 1U });
	source.admit_page(infoset, 1U);
	source.admit_page(infoset, 0U);
	source.apply_delta(infoset, 0U, 0U, 1.0, 1.0e10);
	source.apply_delta(infoset, 3U, 0U, -2.0, 3.0);

	texas::solver::multiway::MultiwaySparseStorageCheckpoint checkpoint;
	source.export_checkpoint(checkpoint);
	EXPECT_EQ(checkpoint.compact_regrets.size(), 4U);
	EXPECT_EQ(checkpoint.compact_strategy_masses.size(), 4U);
	EXPECT_TRUE(checkpoint.compact_strategy_masses[0] > (1ULL << 53U));

	texas::solver::multiway::MultiwayCompactStorage restored(1U, 16U, 2U);
	restored.admit_row({ infoset, 4U, 1U });
	for (const auto& page : checkpoint.active_pages)
		restored.admit_page(page.infoset, page.page_index);
	restored.restore_checkpoint_values(checkpoint.compact_regrets, checkpoint.compact_strategy_masses);

	texas::solver::multiway::MultiwaySparseStorageCheckpoint restored_checkpoint;
	restored.export_checkpoint(restored_checkpoint);
	EXPECT_EQ(restored_checkpoint.compact_regrets, checkpoint.compact_regrets);
	EXPECT_EQ(restored_checkpoint.compact_strategy_masses, checkpoint.compact_strategy_masses);
}
