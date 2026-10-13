#include "solver/multiway/engine/multiway_solver.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{

	MultiwayPublicCatalog::MultiwayPublicCatalog(std::size_t max_nodes)
		: max_nodes_(max_nodes)
	{
		nodes_.reserve(std::min<std::size_t>(max_nodes_, 256U));
		index_.reserve(std::min<std::size_t>(max_nodes_, 256U));
	}

	void MultiwayPublicCatalog::clear() noexcept
	{
		nodes_.clear();
		index_.clear();
		menus_.clear();
	}

	std::size_t MultiwayPublicCatalog::find_menu(const std::vector<MultiwayActionDescriptor>& menu) const noexcept
	{
		for (std::size_t index = 0U; index < menus_.size(); ++index)
		{
			if (menus_[index] == menu)
				return index;
		}
		return std::numeric_limits<std::size_t>::max();
	}

	const MultiwayPackedPublicNode* MultiwayPublicCatalog::node(MultiwayPublicStateId id) const noexcept
	{
		const auto found = std::lower_bound(index_.begin(), index_.end(), id,
			[](const IndexEntry& entry, MultiwayPublicStateId key) { return entry.id < key; });
		return found == index_.end() || found->id != id ? nullptr : &nodes_[found->node_index];
	}

	bool MultiwayPublicCatalog::contains(MultiwayPublicStateId id) const noexcept
	{
		return node(id) != nullptr;
	}

	void MultiwayPublicCatalog::admit(const MultiwayPublicStateDescriptor& state)
	{
		if (state.id.value == 0U || state.board.size() > 5U || state.incoming_edge.dealt_cards.size() > 3U || state.incoming_edge.transition_board.size() > 5U)
			throw std::invalid_argument("packed public catalog state is malformed");
		if (contains(state.id))
			return;
		if (nodes_.size() >= max_nodes_)
			throw std::length_error("packed public catalog capacity exceeded");
		if (state.betting.stacks.size() > 6U || state.betting.contributions.size() != state.betting.stacks.size() || state.betting.street_contributions.size() != state.betting.stacks.size() || state.betting.folded.size() != state.betting.stacks.size() || state.betting.all_in.size() != state.betting.stacks.size() || state.betting.may_raise.size() != state.betting.stacks.size() || state.betting.pending.size() != state.betting.stacks.size() || state.betting.has_acted.size() != state.betting.stacks.size() || state.betting.bet_faced_when_acted.size() != state.betting.stacks.size())
			throw std::invalid_argument("packed public catalog betting state is malformed");

		const auto menu = find_menu(state.legal_actions);
		std::size_t menu_handle = menu;
		if (menu_handle == std::numeric_limits<std::size_t>::max())
		{
			menus_.push_back(state.legal_actions);
			menu_handle = menus_.size() - 1U;
		}
		if (menu_handle > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("packed public catalog menu handle overflow");

		MultiwayPackedPublicNode packed;
		packed.id = state.id;
		packed.parent_id = state.parent_id;
		packed.canonical_history_id = state.canonical_history_id;
		packed.board_count = static_cast<std::uint8_t>(state.board.size());
		std::copy(state.board.begin(), state.board.end(), packed.board.begin());
		packed.board_runout = state.board_runout;
		packed.incoming_kind = state.incoming_edge.kind;
		packed.incoming_action = state.incoming_edge.action;
		packed.dealt_card = state.incoming_edge.dealt_card;
		packed.dealt_card_count = static_cast<std::uint8_t>(state.incoming_edge.dealt_cards.size());
		std::copy(state.incoming_edge.dealt_cards.begin(), state.incoming_edge.dealt_cards.end(), packed.dealt_cards.begin());
		packed.transition_board_count = static_cast<std::uint8_t>(state.incoming_edge.transition_board.size());
		std::copy(state.incoming_edge.transition_board.begin(), state.incoming_edge.transition_board.end(), packed.transition_board.begin());
		packed.legal_menu_handle = static_cast<std::uint32_t>(menu_handle);
		if (!state.history.empty())
		{
			packed.incoming_actor = state.history.back().actor;
			if (packed.incoming_kind != MultiwayPublicParentEdgeKind::BettingAction)
				packed.incoming_actor = -1;
		}

		packed.betting.seat_count = static_cast<std::uint8_t>(state.betting.stacks.size());
		packed.betting.current_player = state.betting.current_player;
		packed.betting.last_aggressor = state.betting.last_aggressor;
		packed.betting.current_bet = state.betting.current_bet;
		packed.betting.last_full_raise_size = state.betting.last_full_raise_size;
		packed.betting.big_blind = state.betting.big_blind;
		packed.betting.street = state.betting.street;
		for (std::size_t seat = 0U; seat < state.betting.stacks.size(); ++seat)
		{
			packed.betting.stacks[seat] = state.betting.stacks[seat];
			packed.betting.contributions[seat] = state.betting.contributions[seat];
			packed.betting.street_contributions[seat] = state.betting.street_contributions[seat];
			packed.betting.folded[seat] = state.betting.folded[seat] ? 1U : 0U;
			packed.betting.all_in[seat] = state.betting.all_in[seat] ? 1U : 0U;
			packed.betting.may_raise[seat] = state.betting.may_raise[seat] ? 1U : 0U;
			packed.betting.pending[seat] = state.betting.pending[seat] ? 1U : 0U;
			packed.betting.has_acted[seat] = state.betting.has_acted[seat] ? 1U : 0U;
			packed.betting.bet_faced_when_acted[seat] = state.betting.bet_faced_when_acted[seat];
		}

		const auto node_index = nodes_.size();
		nodes_.push_back(packed);
		const auto insertion = std::lower_bound(index_.begin(), index_.end(), state.id,
			[](const IndexEntry& entry, MultiwayPublicStateId key) { return entry.id < key; });
		index_.insert(insertion, IndexEntry{ state.id, node_index });
	}

	bool MultiwayPublicCatalog::reconstruct(MultiwayPublicStateId id, MultiwayPublicStateDescriptor& output) const
	{
		const auto* packed = node(id);
		if (packed == nullptr || packed->legal_menu_handle >= menus_.size())
			return false;
		output = {};
		output.id = packed->id;
		output.parent_id = packed->parent_id;
		output.canonical_history_id = packed->canonical_history_id;
		output.board_runout = packed->board_runout;
		output.board.assign(packed->board.begin(), packed->board.begin() + packed->board_count);
		output.legal_actions = menus_[packed->legal_menu_handle];
		output.incoming_edge.kind = packed->incoming_kind;
		output.incoming_edge.action = packed->incoming_action;
		output.incoming_edge.dealt_card = packed->dealt_card;
		output.incoming_edge.dealt_cards.assign(packed->dealt_cards.begin(), packed->dealt_cards.begin() + packed->dealt_card_count);
		output.incoming_edge.transition_board.assign(packed->transition_board.begin(), packed->transition_board.begin() + packed->transition_board_count);
		output.betting.current_player = packed->betting.current_player;
		output.betting.last_aggressor = packed->betting.last_aggressor;
		output.betting.current_bet = packed->betting.current_bet;
		output.betting.last_full_raise_size = packed->betting.last_full_raise_size;
		output.betting.big_blind = packed->betting.big_blind;
		output.betting.street = packed->betting.street;
		output.betting.stacks.assign(packed->betting.stacks.begin(), packed->betting.stacks.begin() + packed->betting.seat_count);
		output.betting.contributions.assign(packed->betting.contributions.begin(), packed->betting.contributions.begin() + packed->betting.seat_count);
		output.betting.street_contributions.assign(packed->betting.street_contributions.begin(), packed->betting.street_contributions.begin() + packed->betting.seat_count);
		output.betting.folded.resize(packed->betting.seat_count);
		output.betting.all_in.resize(packed->betting.seat_count);
		output.betting.may_raise.resize(packed->betting.seat_count);
		output.betting.pending.resize(packed->betting.seat_count);
		output.betting.has_acted.resize(packed->betting.seat_count);
		output.betting.bet_faced_when_acted.assign(packed->betting.bet_faced_when_acted.begin(), packed->betting.bet_faced_when_acted.begin() + packed->betting.seat_count);
		for (std::size_t seat = 0U; seat < packed->betting.seat_count; ++seat)
		{
			output.betting.folded[seat] = packed->betting.folded[seat] != 0U;
			output.betting.all_in[seat] = packed->betting.all_in[seat] != 0U;
			output.betting.may_raise[seat] = packed->betting.may_raise[seat] != 0U;
			output.betting.pending[seat] = packed->betting.pending[seat] != 0U;
			output.betting.has_acted[seat] = packed->betting.has_acted[seat] != 0U;
		}

		std::vector<MultiwayPublicHistoryEntry> reverse_history;
		const auto* current = packed;
		std::size_t guard = 0U;
		while (current->parent_id.value != 0U && guard++ <= nodes_.size())
		{
			if (current->incoming_kind == MultiwayPublicParentEdgeKind::BettingAction)
			{
				auto history_action = current->incoming_action;
				// Public history canonicalizes the selected action index to zero;
				// the incoming edge retains the original parent-menu index.
				history_action.action_index = 0U;
				reverse_history.push_back({ current->incoming_actor, history_action });
			}
			current = node(current->parent_id);
			if (current == nullptr)
				return false;
		}
		if (guard > nodes_.size())
			return false;
		output.history.assign(reverse_history.rbegin(), reverse_history.rend());
		return true;
	}

	void MultiwayPublicCatalog::export_descriptors(std::vector<MultiwayPublicStateDescriptor>& output) const
	{
		output.clear();
		output.reserve(index_.size());
		for (const auto& entry : index_)
		{
			MultiwayPublicStateDescriptor descriptor;
			if (!reconstruct(entry.id, descriptor))
				throw std::logic_error("packed public catalog cannot reconstruct an admitted node");
			output.push_back(std::move(descriptor));
		}
	}

	std::size_t MultiwayPublicCatalog::memory_bytes() const noexcept
	{
		std::size_t bytes = nodes_.capacity() * sizeof(MultiwayPackedPublicNode) + index_.capacity() * sizeof(IndexEntry) + menus_.capacity() * sizeof(std::vector<MultiwayActionDescriptor>);
		for (const auto& menu : menus_)
			bytes += menu.capacity() * sizeof(MultiwayActionDescriptor);
		return bytes;
	}

} // namespace texas::solver::multiway
