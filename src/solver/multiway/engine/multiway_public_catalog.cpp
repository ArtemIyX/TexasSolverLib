#include "solver/multiway/engine/multiway_solver.hpp"

#include <algorithm>
#include <chrono>
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

	void MultiwayPublicCatalog::classify_new_states(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		std::vector<std::uint8_t>& new_state_flags) const
	{
		new_state_flags.assign(states.size(), 1U);
		std::size_t catalog_index = 0U;
		for (std::size_t state_index = 0U; state_index < states.size(); ++state_index)
		{
			while (catalog_index < index_.size() && index_[catalog_index].id < states[state_index].id)
				++catalog_index;
			if (catalog_index < index_.size() && index_[catalog_index].id == states[state_index].id)
				new_state_flags[state_index] = 0U;
		}
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

	MultiwayPackedPublicNode MultiwayPublicCatalog::pack_node(
		const MultiwayPublicStateDescriptor& state, std::uint32_t menu_handle) const noexcept
	{
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
		packed.legal_menu_handle = menu_handle;
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
		return packed;
	}

	void MultiwayPublicCatalog::admit(
		const MultiwayPublicStateDescriptor& state,
		std::uint64_t* index_insertion_nanoseconds)
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

		const auto packed = pack_node(state, static_cast<std::uint32_t>(menu_handle));
		const auto node_index = nodes_.size();
		nodes_.push_back(packed);
		const auto insertion = std::lower_bound(index_.begin(), index_.end(), state.id,
			[](const IndexEntry& entry, MultiwayPublicStateId key) { return entry.id < key; });
		const auto index_started = std::chrono::steady_clock::now();
		index_.insert(insertion, IndexEntry{ state.id, node_index });
		if (index_insertion_nanoseconds != nullptr)
			*index_insertion_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - index_started)
					.count());
	}

	void MultiwayPublicCatalog::admit_batch(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		const std::vector<std::uint8_t>& new_state_flags,
		std::uint64_t* index_insertion_nanoseconds,
		std::uint64_t* menu_lookup_nanoseconds,
		std::uint64_t* index_merge_nanoseconds)
	{
		if (states.size() != new_state_flags.size())
			throw std::invalid_argument("packed public catalog batch flags do not match states");
		if (!std::is_sorted(states.begin(), states.end(), [](const auto& left, const auto& right) {
			return left.id < right.id;
		}))
			throw std::invalid_argument("packed public catalog batch states must be sorted by id");
		for (std::size_t index = 1U; index < states.size(); ++index)
			if (states[index - 1U].id == states[index].id)
				throw std::invalid_argument("packed public catalog batch states must be unique");

		std::size_t new_count = 0U;
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags[index] == 0U)
				continue;
			if (states[index].id.value == 0U || states[index].board.size() > 5U || states[index].incoming_edge.dealt_cards.size() > 3U || states[index].incoming_edge.transition_board.size() > 5U)
				throw std::invalid_argument("packed public catalog state is malformed");
			if (states[index].betting.stacks.size() > 6U || states[index].betting.contributions.size() != states[index].betting.stacks.size() || states[index].betting.street_contributions.size() != states[index].betting.stacks.size() || states[index].betting.folded.size() != states[index].betting.stacks.size() || states[index].betting.all_in.size() != states[index].betting.stacks.size() || states[index].betting.may_raise.size() != states[index].betting.stacks.size() || states[index].betting.pending.size() != states[index].betting.stacks.size() || states[index].betting.has_acted.size() != states[index].betting.stacks.size() || states[index].betting.bet_faced_when_acted.size() != states[index].betting.stacks.size())
				throw std::invalid_argument("packed public catalog betting state is malformed");
			++new_count;
		}
		if (nodes_.size() > max_nodes_ || new_count > max_nodes_ - nodes_.size())
			throw std::length_error("packed public catalog capacity exceeded");
		std::size_t existing_index = 0U;
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags[index] == 0U)
				continue;
			while (existing_index < index_.size() && index_[existing_index].id < states[index].id)
				++existing_index;
			if (existing_index < index_.size() && index_[existing_index].id == states[index].id)
				throw std::invalid_argument("packed public catalog batch contains an admitted state");
		}
		if (new_count == 0U)
			return;

		std::vector<MultiwayPackedPublicNode> pending_nodes;
		std::vector<IndexEntry> pending_index;
		pending_nodes.reserve(new_count);
		pending_index.reserve(new_count);
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags[index] == 0U)
				continue;
			const auto menu_started = std::chrono::steady_clock::now();
			const auto menu = find_menu(states[index].legal_actions);
			if (menu_lookup_nanoseconds != nullptr)
				*menu_lookup_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - menu_started)
						.count());
			std::size_t menu_handle = menu;
			if (menu_handle == std::numeric_limits<std::size_t>::max())
			{
				menus_.push_back(states[index].legal_actions);
				menu_handle = menus_.size() - 1U;
			}
			if (menu_handle > std::numeric_limits<std::uint32_t>::max())
				throw std::length_error("packed public catalog menu handle overflow");
			const auto node_index = nodes_.size() + pending_nodes.size();
			pending_nodes.push_back(pack_node(states[index], static_cast<std::uint32_t>(menu_handle)));
			pending_index.push_back({ states[index].id, node_index });
		}

		nodes_.reserve(nodes_.size() + pending_nodes.size());
		index_.reserve(index_.size() + pending_index.size());
		const auto index_started = std::chrono::steady_clock::now();
		const auto index_merge_started = std::chrono::steady_clock::now();
		std::vector<IndexEntry> merged_index;
		merged_index.reserve(index_.size() + pending_index.size());
		std::size_t old_index = 0U;
		std::size_t new_index = 0U;
		while (old_index < index_.size() || new_index < pending_index.size())
		{
			if (new_index == pending_index.size() || (old_index < index_.size() && index_[old_index].id < pending_index[new_index].id))
				merged_index.push_back(index_[old_index++]);
			else
				merged_index.push_back(pending_index[new_index++]);
		}
		if (index_merge_nanoseconds != nullptr)
			*index_merge_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - index_merge_started)
					.count());
		nodes_.insert(nodes_.end(), pending_nodes.begin(), pending_nodes.end());
		index_.swap(merged_index);
		if (index_insertion_nanoseconds != nullptr)
			*index_insertion_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - index_started)
					.count());
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
