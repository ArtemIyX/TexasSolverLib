#include "solver/multiway/engine/multiway_solver.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	namespace
	{
		void record_nested_descriptor_copy(
			MultiwayAdmissionDiagnostics* diagnostics,
			std::size_t element_count,
			std::size_t element_size) noexcept
		{
			if (diagnostics == nullptr || element_count == 0U)
				return;
			diagnostics->catalog_nested_descriptor_copy_count += static_cast<std::uint64_t>(element_count);
			diagnostics->catalog_nested_descriptor_copy_bytes += static_cast<std::uint64_t>(element_count * element_size);
		}

		void record_descriptor_copy(
			const MultiwayPublicStateDescriptor& state,
			MultiwayAdmissionDiagnostics* diagnostics) noexcept
		{
			if (diagnostics == nullptr)
				return;
			++diagnostics->catalog_public_descriptor_copy_count;
			diagnostics->catalog_public_descriptor_copy_bytes += sizeof(MultiwayPublicStateDescriptor);
			record_nested_descriptor_copy(diagnostics, state.board.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.incoming_edge.dealt_cards.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.incoming_edge.transition_board.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.stacks.size(), sizeof(int));
			record_nested_descriptor_copy(diagnostics, state.betting.contributions.size(), sizeof(int));
			record_nested_descriptor_copy(diagnostics, state.betting.street_contributions.size(), sizeof(int));
			record_nested_descriptor_copy(diagnostics, state.betting.folded.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.all_in.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.may_raise.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.pending.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.has_acted.size(), sizeof(std::uint8_t));
			record_nested_descriptor_copy(diagnostics, state.betting.bet_faced_when_acted.size(), sizeof(int));
		}

		void record_output_capacity_reuse(
			MultiwayAdmissionDiagnostics* diagnostics,
			bool reused,
			std::size_t element_count,
			std::size_t element_size) noexcept
		{
			if (diagnostics != nullptr && reused)
			{
				++diagnostics->catalog_output_capacity_reuse_events;
				diagnostics->catalog_output_capacity_reuse_bytes += static_cast<std::uint64_t>(element_count * element_size);
			}
		}
	} // namespace

	MultiwayPublicCatalog::MultiwayPublicCatalog(std::size_t max_nodes)
		: max_nodes_(max_nodes)
	{
		nodes_.reserve(std::min<std::size_t>(max_nodes_, 256U));
		index_.reserve(std::min<std::size_t>(max_nodes_, 256U));
		menu_index_.reserve(std::min<std::size_t>(max_nodes_, 256U));
	}

	void MultiwayPublicCatalog::clear() noexcept
	{
		nodes_.clear();
		index_.clear();
		menus_.clear();
		menu_index_.clear();
		batch_pending_menus_.clear();
		batch_menu_keys_.clear();
		batch_menu_handles_.clear();
		batch_pending_menu_indexes_.clear();
		batch_pending_index_.clear();
		batch_merged_index_.clear();
		batch_pending_menu_index_.clear();
		batch_merged_menu_index_.clear();
	}

	std::size_t MultiwayPublicCatalog::find_menu(const std::vector<MultiwayActionDescriptor>& menu) const noexcept
	{
		return find_menu_by_key(menu, menu_key(menu));
	}

	std::uint64_t MultiwayPublicCatalog::menu_key(const std::vector<MultiwayActionDescriptor>& menu) noexcept
	{
		return menu.empty() ? 0U : menu.front().action_menu_id;
	}

	void MultiwayPublicCatalog::validate_menu_index_debug() const noexcept
	{
#ifndef NDEBUG
		assert(std::is_sorted(menu_index_.begin(), menu_index_.end(), [](const MenuIndexEntry& left, const MenuIndexEntry& right) {
			return left.canonical_menu_id < right.canonical_menu_id || (left.canonical_menu_id == right.canonical_menu_id && left.menu_handle < right.menu_handle);
		}));
		for (const auto& entry : menu_index_)
			assert(entry.menu_handle < menus_.size());
#endif
	}

	std::size_t MultiwayPublicCatalog::find_menu_by_key(
		const std::vector<MultiwayActionDescriptor>& menu,
		std::uint64_t canonical_menu_id,
		MultiwayAdmissionDiagnostics* diagnostics,
		std::uint64_t* index_lookup_nanoseconds,
		std::uint64_t* equality_nanoseconds) const noexcept
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.find_menu_by_key");
		const auto index_lookup_started = std::chrono::steady_clock::now();
		const auto first = std::lower_bound(menu_index_.begin(), menu_index_.end(), canonical_menu_id,
			[](const MenuIndexEntry& entry, std::uint64_t key) { return entry.canonical_menu_id < key; });
		const auto last = std::upper_bound(first, menu_index_.end(), canonical_menu_id,
			[](std::uint64_t key, const MenuIndexEntry& entry) { return key < entry.canonical_menu_id; });
		const auto index_lookup_elapsed = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - index_lookup_started)
				.count());
		if (index_lookup_nanoseconds != nullptr)
			*index_lookup_nanoseconds += index_lookup_elapsed;
		if (diagnostics != nullptr)
			diagnostics->catalog_menu_index_probes += static_cast<std::uint64_t>(last - first);
		const auto equality_started = std::chrono::steady_clock::now();
		for (auto candidate = first; candidate != last; ++candidate)
		{
			if (diagnostics != nullptr)
				++diagnostics->catalog_menu_equality_checks;
			if (menus_[candidate->menu_handle] == menu)
			{
				if (equality_nanoseconds != nullptr)
					*equality_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - equality_started)
							.count());
				return candidate->menu_handle;
			}
		}
		if (equality_nanoseconds != nullptr)
			*equality_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - equality_started)
					.count());
		if (diagnostics != nullptr && first != last)
			++diagnostics->catalog_menu_id_collision_misses;
		return std::numeric_limits<std::size_t>::max();
	}

	void MultiwayPublicCatalog::classify_new_states(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		std::vector<std::uint8_t>& new_state_flags) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.classify_new_states");
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
		const MultiwayPublicStateDescriptor& state,
		std::uint32_t menu_handle,
		MultiwayAdmissionDiagnostics* diagnostics) const noexcept
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.pack_node");
		record_descriptor_copy(state, diagnostics);
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
		std::uint64_t* index_insertion_nanoseconds,
		MultiwayAdmissionDiagnostics* diagnostics)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit");
		if (state.id.value == 0U || state.board.size() > 5U || state.incoming_edge.dealt_cards.size() > 3U || state.incoming_edge.transition_board.size() > 5U)
			throw std::invalid_argument("packed public catalog state is malformed");
		if (contains(state.id))
			return;
		if (nodes_.size() >= max_nodes_)
			throw std::length_error("packed public catalog capacity exceeded");
		if (state.betting.stacks.size() > 6U || state.betting.contributions.size() != state.betting.stacks.size() || state.betting.street_contributions.size() != state.betting.stacks.size() || state.betting.folded.size() != state.betting.stacks.size() || state.betting.all_in.size() != state.betting.stacks.size() || state.betting.may_raise.size() != state.betting.stacks.size() || state.betting.pending.size() != state.betting.stacks.size() || state.betting.has_acted.size() != state.betting.stacks.size() || state.betting.bet_faced_when_acted.size() != state.betting.stacks.size())
			throw std::invalid_argument("packed public catalog betting state is malformed");

		const auto menu_started = std::chrono::steady_clock::now();
		const auto menu_key_started = std::chrono::steady_clock::now();
		const auto canonical_menu_id = menu_key(state.legal_actions);
		if (diagnostics != nullptr)
			diagnostics->catalog_menu_key_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - menu_key_started)
					.count());
		const auto menu = find_menu_by_key(state.legal_actions, canonical_menu_id, diagnostics);
		std::size_t menu_handle = menu;
		if (menu_handle == std::numeric_limits<std::size_t>::max())
		{
			if (menus_.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::length_error("packed public catalog menu handle overflow");
			record_output_capacity_reuse(diagnostics, menus_.capacity() >= menus_.size() + 1U, 1U, sizeof(menus_.front()));
			record_output_capacity_reuse(diagnostics, menu_index_.capacity() >= menu_index_.size() + 1U, 1U, sizeof(MenuIndexEntry));
			menus_.reserve(menus_.size() + 1U);
			menu_index_.reserve(menu_index_.size() + 1U);
			record_nested_descriptor_copy(diagnostics, state.legal_actions.size(), sizeof(MultiwayActionDescriptor));
			menus_.push_back(state.legal_actions);
			menu_handle = menus_.size() - 1U;
			const auto insertion = std::lower_bound(menu_index_.begin(), menu_index_.end(), MenuIndexEntry{ canonical_menu_id, static_cast<std::uint32_t>(menu_handle) },
				[](const MenuIndexEntry& left, const MenuIndexEntry& right) {
					return left.canonical_menu_id < right.canonical_menu_id || (left.canonical_menu_id == right.canonical_menu_id && left.menu_handle < right.menu_handle);
				});
			menu_index_.insert(insertion, MenuIndexEntry{ canonical_menu_id, static_cast<std::uint32_t>(menu_handle) });
			if (diagnostics != nullptr)
				++diagnostics->catalog_new_menus;
		}
		else if (diagnostics != nullptr)
			++diagnostics->catalog_existing_menu_hits;
		if (diagnostics != nullptr)
		{
			const auto elapsed = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - menu_started)
					.count());
			diagnostics->catalog_menu_lookup_nanoseconds += elapsed;
			diagnostics->catalog_menu_index_lookup_nanoseconds += elapsed;
		}
		if (menu_handle > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("packed public catalog menu handle overflow");

		record_output_capacity_reuse(diagnostics, nodes_.capacity() >= nodes_.size() + 1U, 1U, sizeof(MultiwayPackedPublicNode));
		record_output_capacity_reuse(diagnostics, index_.capacity() >= index_.size() + 1U, 1U, sizeof(IndexEntry));
		const auto packed = pack_node(state, static_cast<std::uint32_t>(menu_handle), diagnostics);
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
		validate_menu_index_debug();
	}

	void MultiwayPublicCatalog::admit_batch(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		const std::vector<std::uint8_t>& new_state_flags,
		std::uint64_t* index_insertion_nanoseconds,
		std::uint64_t* menu_lookup_nanoseconds,
		std::uint64_t* index_merge_nanoseconds,
		MultiwayAdmissionDiagnostics* diagnostics,
		const std::vector<std::uint64_t>* validated_menu_keys,
		std::vector<std::uint32_t>* resolved_menu_handles)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch");
		if (states.size() != new_state_flags.size())
			throw std::invalid_argument("packed public catalog batch flags do not match states");
		if (validated_menu_keys != nullptr && validated_menu_keys->size() != states.size())
			throw std::invalid_argument("packed public catalog batch menu keys do not match states");
		if (resolved_menu_handles != nullptr)
			resolved_menu_handles->assign(states.size(), std::numeric_limits<std::uint32_t>::max());
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
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit.validate_state");
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
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit.resolve_menu");
			if (new_state_flags[index] == 0U)
				continue;
			while (existing_index < index_.size() && index_[existing_index].id < states[index].id)
				++existing_index;
			if (existing_index < index_.size() && index_[existing_index].id == states[index].id)
				throw std::invalid_argument("packed public catalog batch contains an admitted state");
		}
		if (new_count == 0U)
		{
			if (resolved_menu_handles != nullptr)
			{
				for (std::size_t index = 0U; index < states.size(); ++index)
				{
					const auto* admitted = node(states[index].id);
					if (admitted != nullptr)
						(*resolved_menu_handles)[index] = admitted->legal_menu_handle;
				}
			}
			return;
		}

		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch.prepare_pending");
			batch_pending_menus_.clear();
			batch_menu_keys_.assign(states.size(), 0U);
			batch_menu_handles_.assign(states.size(), std::numeric_limits<std::uint32_t>::max());
			batch_pending_menu_indexes_.assign(states.size(), std::numeric_limits<std::uint32_t>::max());
			batch_pending_index_.clear();
			batch_pending_menu_index_.clear();
			batch_pending_menus_.reserve(new_count);
			batch_pending_index_.reserve(new_count);
			batch_pending_menu_index_.reserve(new_count);
		}

		std::size_t existing_menu_hits = 0U;
		std::size_t batch_menu_reuse_hits = 0U;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch.resolve_menus");
			for (std::size_t index = 0U; index < states.size(); ++index)
			{
			if (new_state_flags[index] == 0U)
				continue;
			const auto menu_started = std::chrono::steady_clock::now();
			std::uint64_t canonical_menu_id = 0U;
			if (validated_menu_keys != nullptr)
				canonical_menu_id = (*validated_menu_keys)[index];
			else
			{
				const auto key_started = std::chrono::steady_clock::now();
				canonical_menu_id = menu_key(states[index].legal_actions);
				if (diagnostics != nullptr)
					diagnostics->catalog_menu_key_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - key_started)
							.count());
			}
			batch_menu_keys_[index] = canonical_menu_id;
			std::size_t menu_handle = std::numeric_limits<std::size_t>::max();
			if (!menu_index_.empty())
			{
				std::uint64_t index_lookup_nanoseconds = 0U;
				std::uint64_t equality_nanoseconds = 0U;
				menu_handle = find_menu_by_key(
					states[index].legal_actions,
					canonical_menu_id,
					diagnostics,
					&index_lookup_nanoseconds,
					&equality_nanoseconds);
				if (diagnostics != nullptr)
				{
					diagnostics->catalog_menu_index_lookup_nanoseconds += index_lookup_nanoseconds;
					diagnostics->catalog_menu_equality_nanoseconds += equality_nanoseconds;
				}
			}
			const auto record_menu_lookup = [&]() {
				if (menu_lookup_nanoseconds != nullptr)
					*menu_lookup_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - menu_started)
							.count());
			};
			if (menu_handle != std::numeric_limits<std::size_t>::max())
			{
				batch_menu_handles_[index] = static_cast<std::uint32_t>(menu_handle);
				++existing_menu_hits;
				record_menu_lookup();
				continue;
			}

			std::size_t pending_menu = std::numeric_limits<std::size_t>::max();
			bool pending_key_collision = false;
			const auto pending_equality_started = std::chrono::steady_clock::now();
			const auto pending_first = std::lower_bound(batch_pending_menu_index_.begin(), batch_pending_menu_index_.end(), canonical_menu_id,
				[](const PendingMenuIndexEntry& entry, std::uint64_t key) { return entry.canonical_menu_id < key; });
			const auto pending_last = std::upper_bound(pending_first, batch_pending_menu_index_.end(), canonical_menu_id,
				[](std::uint64_t key, const PendingMenuIndexEntry& entry) { return key < entry.canonical_menu_id; });
			pending_key_collision = pending_first != pending_last;
			for (auto pending_entry = pending_first; pending_entry != pending_last; ++pending_entry)
			{
				if (diagnostics != nullptr)
					++diagnostics->catalog_menu_equality_checks;
				if (batch_pending_menus_[pending_entry->pending_menu_index] == states[index].legal_actions)
				{
					pending_menu = pending_entry->pending_menu_index;
					break;
				}
			}
			if (diagnostics != nullptr)
				diagnostics->catalog_menu_equality_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - pending_equality_started)
						.count());
			if (diagnostics != nullptr && pending_key_collision && pending_menu == std::numeric_limits<std::size_t>::max())
				++diagnostics->catalog_menu_id_collision_misses;
			if (pending_menu != std::numeric_limits<std::size_t>::max())
			{
				batch_menu_handles_[index] = static_cast<std::uint32_t>(menus_.size() + pending_menu);
				batch_pending_menu_indexes_[index] = static_cast<std::uint32_t>(pending_menu);
				++batch_menu_reuse_hits;
				record_menu_lookup();
				continue;
			}

			if (menus_.size() + batch_pending_menus_.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::length_error("packed public catalog menu handle overflow");
			record_nested_descriptor_copy(diagnostics, states[index].legal_actions.size(), sizeof(MultiwayActionDescriptor));
			batch_pending_menus_.push_back(states[index].legal_actions);
			const auto pending_index = batch_pending_menus_.size() - 1U;
			batch_menu_handles_[index] = static_cast<std::uint32_t>(menus_.size() + pending_index);
			batch_pending_menu_indexes_[index] = static_cast<std::uint32_t>(pending_index);
			const PendingMenuIndexEntry pending_entry{ canonical_menu_id, static_cast<std::uint32_t>(pending_index) };
			const auto pending_index_insertion = std::lower_bound(batch_pending_menu_index_.begin(), batch_pending_menu_index_.end(), pending_entry,
				[](const PendingMenuIndexEntry& left, const PendingMenuIndexEntry& right) {
					return left.canonical_menu_id < right.canonical_menu_id || (left.canonical_menu_id == right.canonical_menu_id && left.pending_menu_index < right.pending_menu_index);
				});
			batch_pending_menu_index_.insert(pending_index_insertion, pending_entry);
			record_menu_lookup();
		}
		}

		if (diagnostics != nullptr)
		{
			diagnostics->catalog_existing_menu_hits += existing_menu_hits;
			diagnostics->catalog_batch_menu_reuse_hits += batch_menu_reuse_hits;
			diagnostics->catalog_new_menus += batch_pending_menus_.size();
		}
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags[index] != 0U)
				batch_pending_index_.push_back({ states[index].id, nodes_.size() + batch_pending_index_.size() });
			else if (resolved_menu_handles != nullptr)
			{
				const auto* admitted = node(states[index].id);
				if (admitted != nullptr)
					(*resolved_menu_handles)[index] = admitted->legal_menu_handle;
			}
			if (resolved_menu_handles != nullptr && new_state_flags[index] != 0U)
				(*resolved_menu_handles)[index] = batch_menu_handles_[index];
		}

		record_output_capacity_reuse(diagnostics, nodes_.capacity() >= nodes_.size() + batch_pending_index_.size(), batch_pending_index_.size(), sizeof(MultiwayPackedPublicNode));
		record_output_capacity_reuse(diagnostics, index_.capacity() >= index_.size() + batch_pending_index_.size(), batch_pending_index_.size(), sizeof(IndexEntry));
		record_output_capacity_reuse(diagnostics, menus_.capacity() >= menus_.size() + batch_pending_menus_.size(), batch_pending_menus_.size(), sizeof(menus_.front()));
		record_output_capacity_reuse(diagnostics, menu_index_.capacity() >= menu_index_.size() + batch_pending_menu_index_.size(), batch_pending_menu_index_.size(), sizeof(MenuIndexEntry));
		const auto reserve_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch.reserve");
			nodes_.reserve(nodes_.size() + batch_pending_index_.size());
			index_.reserve(index_.size() + batch_pending_index_.size());
			menus_.reserve(menus_.size() + batch_pending_menus_.size());
			menu_index_.reserve(menu_index_.size() + batch_pending_menu_index_.size());
		}
		if (diagnostics != nullptr)
			diagnostics->catalog_reserve_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - reserve_started)
					.count());

		const auto index_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch.merge_indexes");
			const auto index_merge_started = std::chrono::steady_clock::now();
			batch_merged_index_.clear();
			batch_merged_index_.reserve(index_.size() + batch_pending_index_.size());
			std::size_t old_index = 0U;
			std::size_t new_index = 0U;
			while (old_index < index_.size() || new_index < batch_pending_index_.size())
			{
				if (new_index == batch_pending_index_.size() || (old_index < index_.size() && index_[old_index].id < batch_pending_index_[new_index].id))
					batch_merged_index_.push_back(index_[old_index++]);
				else
					batch_merged_index_.push_back(batch_pending_index_[new_index++]);
			}
			batch_merged_menu_index_.clear();
			batch_merged_menu_index_.reserve(menu_index_.size() + batch_pending_menu_index_.size());
			std::size_t old_menu_index = 0U;
			std::size_t new_menu_index = 0U;
			while (old_menu_index < menu_index_.size() || new_menu_index < batch_pending_menu_index_.size())
			{
				bool take_old = new_menu_index == batch_pending_menu_index_.size();
				if (!take_old && old_menu_index < menu_index_.size())
				{
					const auto pending_menu_handle = static_cast<std::uint32_t>(menus_.size() + batch_pending_menu_index_[new_menu_index].pending_menu_index);
					take_old = menu_index_[old_menu_index].canonical_menu_id < batch_pending_menu_index_[new_menu_index].canonical_menu_id || (menu_index_[old_menu_index].canonical_menu_id == batch_pending_menu_index_[new_menu_index].canonical_menu_id && menu_index_[old_menu_index].menu_handle < pending_menu_handle);
				}
				if (take_old)
					batch_merged_menu_index_.push_back(menu_index_[old_menu_index++]);
				else
				{
					const auto& pending_entry = batch_pending_menu_index_[new_menu_index++];
					batch_merged_menu_index_.push_back({ pending_entry.canonical_menu_id, static_cast<std::uint32_t>(menus_.size() + pending_entry.pending_menu_index) });
				}
			}
			if (index_merge_nanoseconds != nullptr)
				*index_merge_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - index_merge_started)
						.count());
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.admit_batch.append");
			const auto append_started = std::chrono::steady_clock::now();
			const auto pack_started = std::chrono::steady_clock::now();
			for (auto& menu : batch_pending_menus_)
				menus_.push_back(std::move(menu));
			for (std::size_t index = 0U; index < states.size(); ++index)
				if (new_state_flags[index] != 0U)
					nodes_.push_back(pack_node(states[index], batch_menu_handles_[index], diagnostics));
			if (diagnostics != nullptr)
				diagnostics->catalog_node_pack_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - pack_started)
						.count());
			index_.swap(batch_merged_index_);
			menu_index_.swap(batch_merged_menu_index_);
			if (diagnostics != nullptr)
				diagnostics->catalog_append_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - append_started)
						.count());
		}
		if (index_insertion_nanoseconds != nullptr)
			*index_insertion_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - index_started)
					.count());
		validate_menu_index_debug();
	}

	bool MultiwayPublicCatalog::reconstruct(MultiwayPublicStateId id, MultiwayPublicStateDescriptor& output) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.reconstruct");
		const auto* packed = node(id);
		if (packed == nullptr || packed->legal_menu_handle >= menus_.size())
			return false;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.reconstruct.scalar_payload");
			output = {};
			output.id = packed->id;
			output.parent_id = packed->parent_id;
			output.canonical_history_id = packed->canonical_history_id;
			output.board_runout = packed->board_runout;
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.reconstruct.vector_payload");
			output.board.assign(packed->board.begin(), packed->board.begin() + packed->board_count);
			output.legal_actions = menus_[packed->legal_menu_handle];
			output.incoming_edge.kind = packed->incoming_kind;
			output.incoming_edge.action = packed->incoming_action;
			output.incoming_edge.dealt_card = packed->dealt_card;
			output.incoming_edge.dealt_cards.assign(packed->dealt_cards.begin(), packed->dealt_cards.begin() + packed->dealt_card_count);
			output.incoming_edge.transition_board.assign(packed->transition_board.begin(), packed->transition_board.begin() + packed->transition_board_count);
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.reconstruct.betting_payload");
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
		}

		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.reconstruct.history");
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
		}
		return true;
	}

	void MultiwayPublicCatalog::export_descriptors(std::vector<MultiwayPublicStateDescriptor>& output) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.export_descriptors");
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.catalog.export_descriptors.prepare_output");
			output.clear();
			output.reserve(index_.size());
		}
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
		std::size_t bytes = nodes_.capacity() * sizeof(MultiwayPackedPublicNode) + index_.capacity() * sizeof(IndexEntry) + menus_.capacity() * sizeof(std::vector<MultiwayActionDescriptor>) + menu_index_.capacity() * sizeof(MenuIndexEntry);
		for (const auto& menu : menus_)
			bytes += menu.capacity() * sizeof(MultiwayActionDescriptor);
		bytes += batch_pending_menus_.capacity() * sizeof(std::vector<MultiwayActionDescriptor>);
		for (const auto& menu : batch_pending_menus_)
			bytes += menu.capacity() * sizeof(MultiwayActionDescriptor);
		bytes += batch_menu_keys_.capacity() * sizeof(std::uint64_t);
		bytes += batch_menu_handles_.capacity() * sizeof(std::uint32_t);
		bytes += batch_pending_menu_indexes_.capacity() * sizeof(std::uint32_t);
		bytes += batch_pending_index_.capacity() * sizeof(IndexEntry);
		bytes += batch_merged_index_.capacity() * sizeof(IndexEntry);
		bytes += batch_pending_menu_index_.capacity() * sizeof(PendingMenuIndexEntry);
		bytes += batch_merged_menu_index_.capacity() * sizeof(MenuIndexEntry);
		return bytes;
	}

} // namespace texas::solver::multiway
