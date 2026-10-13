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

