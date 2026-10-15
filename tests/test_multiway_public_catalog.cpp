#include "solver/multiway/engine/multiway_solver.hpp"
#include "test_harness.hpp"

namespace
{

	texas::MultiwayPublicStateDescriptor make_descriptor(std::uint64_t id, std::uint64_t parent = 0U)
	{
		texas::MultiwayPublicStateDescriptor state;
		state.id = { id };
		state.parent_id = { parent };
		state.canonical_history_id = id * 17U;
		state.betting.stacks = { 100, 100 };
		state.betting.contributions = { 1, 2 };
		state.betting.street_contributions = { 1, 2 };
		state.betting.folded = { false, false };
		state.betting.all_in = { false, false };
		state.betting.may_raise = { true, true };
		state.betting.pending = { true, true };
		state.betting.has_acted = { false, false };
		state.betting.bet_faced_when_acted = { 2, 2 };
		state.betting.current_player = 0;
		state.betting.last_aggressor = -1;
		state.betting.current_bet = 2;
		state.betting.last_full_raise_size = 2;
		state.betting.big_blind = 2;
		state.betting.street = texas::core::Street::Flop;
		state.legal_actions = { { texas::games::multiway::MultiwayAction::Check, 0U, 2, 11U } };
		if (parent != 0U)
		{
			state.incoming_edge.kind = texas::MultiwayPublicParentEdgeKind::BettingAction;
			state.incoming_edge.action = state.legal_actions.front();
			state.history = { { 0, state.incoming_edge.action } };
		}
		return state;
	}

} // namespace

TEST_CASE(multiway_public_catalog_reconstructs_parent_linked_state)
{
	texas::MultiwayPublicCatalog catalog(4U);
	const auto root = make_descriptor(1U);
	const auto child = make_descriptor(2U, 1U);
	catalog.admit(root);
	catalog.admit(child);

	texas::MultiwayPublicStateDescriptor restored;
	EXPECT_TRUE(catalog.reconstruct({ 2U }, restored));
	EXPECT_EQ(restored.id.value, 2U);
	EXPECT_EQ(restored.parent_id.value, 1U);
	EXPECT_EQ(restored.canonical_history_id, child.canonical_history_id);
	EXPECT_EQ(restored.betting.stacks, child.betting.stacks);
	EXPECT_EQ(restored.legal_actions, child.legal_actions);
	EXPECT_EQ(restored.history, child.history);
}

TEST_CASE(multiway_public_catalog_uses_fixed_node_memory_for_repeated_menus)
{
	texas::MultiwayPublicCatalog catalog(8U);
	const auto root = make_descriptor(1U);
	catalog.admit(root);
	for (std::uint64_t id = 2U; id <= 8U; ++id)
		catalog.admit(make_descriptor(id, 1U));
	EXPECT_EQ(catalog.size(), 8U);
	EXPECT_TRUE(catalog.memory_bytes() < 8U * sizeof(texas::MultiwayPublicStateDescriptor));
}

TEST_CASE(multiway_public_catalog_single_admission_reuses_exact_menu)
{
	texas::MultiwayPublicCatalog repeated(4U);
	repeated.admit(make_descriptor(1U));
	repeated.admit(make_descriptor(2U));

	texas::MultiwayPublicCatalog distinct(4U);
	distinct.admit(make_descriptor(1U));
	auto different = make_descriptor(2U);
	different.legal_actions.front().target_street_contribution = 3;
	distinct.admit(different);

	EXPECT_TRUE(repeated.memory_bytes() < distinct.memory_bytes());
}

TEST_CASE(multiway_public_catalog_batch_reuses_exact_menus)
{
	texas::MultiwayPublicCatalog catalog(4U);
	const auto first = make_descriptor(1U);
	const auto second = make_descriptor(2U);
	texas::MultiwayAdmissionDiagnostics diagnostics;
	catalog.admit_batch({ first, second }, { 1U, 1U }, nullptr, nullptr, nullptr, &diagnostics);

	texas::MultiwayPublicStateDescriptor restored_first;
	texas::MultiwayPublicStateDescriptor restored_second;
	EXPECT_TRUE(catalog.reconstruct(first.id, restored_first));
	EXPECT_TRUE(catalog.reconstruct(second.id, restored_second));
	EXPECT_EQ(restored_first.legal_actions, restored_second.legal_actions);
	EXPECT_EQ(catalog.size(), 2U);
	EXPECT_EQ(diagnostics.catalog_new_menus, 1U);
	EXPECT_EQ(diagnostics.catalog_batch_menu_reuse_hits, 1U);
}

TEST_CASE(multiway_public_catalog_indexes_empty_menus)
{
	texas::MultiwayPublicCatalog catalog(4U);
	auto first = make_descriptor(1U);
	auto second = make_descriptor(2U);
	first.legal_actions.clear();
	second.legal_actions.clear();
	catalog.admit_batch({ first, second }, { 1U, 1U });

	texas::MultiwayPublicStateDescriptor restored;
	EXPECT_TRUE(catalog.reconstruct(second.id, restored));
	EXPECT_TRUE(restored.legal_actions.empty());
}

TEST_CASE(multiway_public_catalog_keeps_same_key_menus_distinct)
{
	texas::MultiwayPublicCatalog catalog(4U);
	auto first = make_descriptor(1U);
	auto second = make_descriptor(2U);
	second.legal_actions.front().target_street_contribution = 3;
	second.legal_actions.front().action_menu_id = first.legal_actions.front().action_menu_id;
	catalog.admit_batch({ first, second }, { 1U, 1U });

	texas::MultiwayPublicStateDescriptor restored_first;
	texas::MultiwayPublicStateDescriptor restored_second;
	EXPECT_TRUE(catalog.reconstruct(first.id, restored_first));
	EXPECT_TRUE(catalog.reconstruct(second.id, restored_second));
	EXPECT_NE(restored_first.legal_actions, restored_second.legal_actions);
}

TEST_CASE(multiway_public_catalog_batch_mixes_existing_and_new_states)
{
	texas::MultiwayPublicCatalog catalog(4U);
	const auto existing = make_descriptor(1U);
	const auto added = make_descriptor(2U, 1U);
	catalog.admit(existing);
	catalog.admit_batch({ existing, added }, { 0U, 1U });

	EXPECT_EQ(catalog.size(), 2U);
	EXPECT_TRUE(catalog.contains(existing.id));
	EXPECT_TRUE(catalog.contains(added.id));
}

TEST_CASE(multiway_public_catalog_capacity_failure_preserves_catalog)
{
	texas::MultiwayPublicCatalog catalog(1U);
	const auto first = make_descriptor(1U);
	const auto second = make_descriptor(2U);
	catalog.admit(first);
	const auto before = catalog.memory_bytes();
	EXPECT_THROW(catalog.admit_batch({ first, second }, { 0U, 1U }), std::length_error);
	EXPECT_EQ(catalog.size(), 1U);
	EXPECT_EQ(catalog.memory_bytes(), before);
	EXPECT_TRUE(catalog.contains(first.id));
	EXPECT_FALSE(catalog.contains(second.id));
}

TEST_CASE(multiway_public_catalog_clear_rebuilds_menu_index)
{
	texas::MultiwayPublicCatalog catalog(4U);
	auto first = make_descriptor(1U);
	auto second = make_descriptor(2U);
	second.legal_actions.clear();
	catalog.admit_batch({ first, second }, { 1U, 1U });
	catalog.clear();

	auto rebuilt = make_descriptor(3U);
	rebuilt.legal_actions.clear();
	catalog.admit(rebuilt);
	texas::MultiwayPublicStateDescriptor restored;
	EXPECT_TRUE(catalog.reconstruct(rebuilt.id, restored));
	EXPECT_TRUE(restored.legal_actions.empty());
}
