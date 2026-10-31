#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	void encode_external_public_node(const MultiwayPackedPublicNode& node,
		std::vector<std::uint8_t>& output)
	{
		if (node.id.value == 0U || node.betting.seat_count > node.betting.stacks.size() || node.board_count > node.board.size() || node.dealt_card_count > node.dealt_cards.size() || node.transition_board_count > node.transition_board.size() || static_cast<std::uint8_t>(node.betting.street) > static_cast<std::uint8_t>(core::Street::Showdown) || static_cast<std::uint8_t>(node.incoming_kind) > static_cast<std::uint8_t>(MultiwayPublicParentEdgeKind::StreetTransition) || static_cast<std::uint8_t>(node.incoming_action.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn))
			throw std::invalid_argument("packed public node exceeds its fixed record bounds");
		for (const auto value : node.betting.folded)
			if (value > 1U)
				throw std::invalid_argument("packed public node has invalid folded flags");
		for (const auto value : node.betting.all_in)
			if (value > 1U)
				throw std::invalid_argument("packed public node has invalid all-in flags");
		for (const auto value : node.betting.may_raise)
			if (value > 1U)
				throw std::invalid_argument("packed public node has invalid raise flags");
		for (const auto value : node.betting.pending)
			if (value > 1U)
				throw std::invalid_argument("packed public node has invalid pending flags");
		for (const auto value : node.betting.has_acted)
			if (value > 1U)
				throw std::invalid_argument("packed public node has invalid acted flags");
		output.clear();
		output.reserve(240U);
		append_u32(output, kNodeRecordMagic);
		append_u32(output, MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA);
		append_u64(output, node.id.value);
		append_u64(output, node.parent_id.value);
		append_u64(output, node.canonical_history_id);
		for (const auto value : node.betting.stacks)
			append_i32(output, value);
		for (const auto value : node.betting.contributions)
			append_i32(output, value);
		for (const auto value : node.betting.street_contributions)
			append_i32(output, value);
		for (const auto value : node.betting.folded)
			output.push_back(value);
		for (const auto value : node.betting.all_in)
			output.push_back(value);
		for (const auto value : node.betting.may_raise)
			output.push_back(value);
		for (const auto value : node.betting.pending)
			output.push_back(value);
		for (const auto value : node.betting.has_acted)
			output.push_back(value);
		for (const auto value : node.betting.bet_faced_when_acted)
			append_i32(output, value);
		output.push_back(node.betting.seat_count);
		append_i32(output, node.betting.current_player);
		append_i32(output, node.betting.last_aggressor);
		append_i32(output, node.betting.current_bet);
		append_i32(output, node.betting.last_full_raise_size);
		append_i32(output, node.betting.big_blind);
		output.push_back(static_cast<std::uint8_t>(node.betting.street));
		output.insert(output.end(), node.board.begin(), node.board.end());
		output.push_back(node.board_count);
		output.push_back(node.board_runout.remaining_board_cards);
		output.push_back(node.board_runout.chance_only_runout ? 1U : 0U);
		output.push_back(static_cast<std::uint8_t>(node.incoming_kind));
		output.push_back(static_cast<std::uint8_t>(node.incoming_action.action));
		append_u32(output, node.incoming_action.action_index);
		append_i32(output, node.incoming_action.target_street_contribution);
		append_u64(output, node.incoming_action.action_menu_id);
		append_i32(output, node.incoming_actor);
		output.push_back(node.dealt_card);
		output.insert(output.end(), node.dealt_cards.begin(), node.dealt_cards.end());
		output.push_back(node.dealt_card_count);
		output.insert(output.end(), node.transition_board.begin(), node.transition_board.end());
		output.push_back(node.transition_board_count);
		append_u32(output, node.legal_menu_handle);
		if (output.size() != kNodeRecordBytes)
			throw std::logic_error("packed public node codec width changed without a schema update");
	}

	std::size_t external_public_node_record_size() noexcept
	{
		return kNodeRecordBytes;
	}

	std::size_t external_public_catalog_record_size() noexcept
	{
		return kNodeRecordBytes + kCatalogMenuBytes;
	}

	bool decode_external_public_node(const std::uint8_t* bytes, std::size_t size,
		MultiwayPackedPublicNode& output) noexcept
	{
		if (bytes == nullptr || size < 8U || read_u32(bytes) != kNodeRecordMagic || read_u32(bytes + 4U) != MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA)
			return false;
		if (size != kNodeRecordBytes)
			return false;
		MultiwayPackedPublicNode node{};
		std::size_t cursor = 8U;
		const auto get_u32 = [&]() { const auto value = read_u32(bytes + cursor); cursor += 4U; return value; };
		const auto get_i32 = [&]() { const auto value = read_i32(bytes + cursor); cursor += 4U; return value; };
		const auto get_u64 = [&]() { const auto value = read_u64(bytes + cursor); cursor += 8U; return value; };
		node.id.value = get_u64();
		node.parent_id.value = get_u64();
		node.canonical_history_id = get_u64();
		for (auto& value : node.betting.stacks)
			value = get_i32();
		for (auto& value : node.betting.contributions)
			value = get_i32();
		for (auto& value : node.betting.street_contributions)
			value = get_i32();
		for (auto& value : node.betting.folded)
			value = bytes[cursor++];
		for (auto& value : node.betting.all_in)
			value = bytes[cursor++];
		for (auto& value : node.betting.may_raise)
			value = bytes[cursor++];
		for (auto& value : node.betting.pending)
			value = bytes[cursor++];
		for (auto& value : node.betting.has_acted)
			value = bytes[cursor++];
		for (auto& value : node.betting.bet_faced_when_acted)
			value = get_i32();
		node.betting.seat_count = bytes[cursor++];
		node.betting.current_player = get_i32();
		node.betting.last_aggressor = get_i32();
		node.betting.current_bet = get_i32();
		node.betting.last_full_raise_size = get_i32();
		node.betting.big_blind = get_i32();
		node.betting.street = static_cast<core::Street>(bytes[cursor++]);
		std::copy_n(bytes + cursor, node.board.size(), node.board.begin());
		cursor += node.board.size();
		node.board_count = bytes[cursor++];
		node.board_runout.remaining_board_cards = bytes[cursor++];
		if (bytes[cursor] > 1U)
			return false;
		node.board_runout.chance_only_runout = bytes[cursor++] != 0U;
		node.incoming_kind = static_cast<MultiwayPublicParentEdgeKind>(bytes[cursor++]);
		node.incoming_action.action = static_cast<MultiwayAction>(bytes[cursor++]);
		node.incoming_action.action_index = get_u32();
		node.incoming_action.target_street_contribution = get_i32();
		node.incoming_action.action_menu_id = get_u64();
		node.incoming_actor = get_i32();
		node.dealt_card = bytes[cursor++];
		std::copy_n(bytes + cursor, node.dealt_cards.size(), node.dealt_cards.begin());
		cursor += node.dealt_cards.size();
		node.dealt_card_count = bytes[cursor++];
		std::copy_n(bytes + cursor, node.transition_board.size(), node.transition_board.begin());
		cursor += node.transition_board.size();
		node.transition_board_count = bytes[cursor++];
		node.legal_menu_handle = get_u32();
		if (cursor != size || node.id.value == 0U || node.betting.seat_count > node.betting.stacks.size() || node.board_count > node.board.size() || node.dealt_card_count > node.dealt_cards.size() || node.transition_board_count > node.transition_board.size() || static_cast<std::uint8_t>(node.betting.street) > static_cast<std::uint8_t>(core::Street::Showdown) || static_cast<std::uint8_t>(node.incoming_kind) > static_cast<std::uint8_t>(MultiwayPublicParentEdgeKind::StreetTransition) || static_cast<std::uint8_t>(node.incoming_action.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn))
			return false;
		for (const auto value : node.betting.folded)
			if (value > 1U)
				return false;
		for (const auto value : node.betting.all_in)
			if (value > 1U)
				return false;
		for (const auto value : node.betting.may_raise)
			if (value > 1U)
				return false;
		for (const auto value : node.betting.pending)
			if (value > 1U)
				return false;
		for (const auto value : node.betting.has_acted)
			if (value > 1U)
				return false;
		output = node;
		return true;
	}

	MultiwayExternalPublicCatalog::MultiwayExternalPublicCatalog(
		MultiwayExternalStorageConfig config, std::size_t maximum_ancestry_depth)
		: store_(std::move(config), external_public_catalog_record_size()), maximum_ancestry_depth_(maximum_ancestry_depth)
	{
		if (maximum_ancestry_depth_ == 0U)
			throw std::invalid_argument("external public catalog requires a positive ancestry depth bound");
	}

	void MultiwayExternalPublicCatalog::classify_batch(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		std::vector<std::uint8_t>& new_state_flags,
		std::vector<MultiwayPackedPublicNode>& existing_nodes) const
	{
		classify_batch_impl(states, new_state_flags, &existing_nodes);
	}

	void MultiwayExternalPublicCatalog::classify_batch(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		std::vector<std::uint8_t>& new_state_flags) const
	{
		classify_batch_impl(states, new_state_flags, nullptr);
	}

	void MultiwayExternalPublicCatalog::classify_batch_impl(
		const std::vector<MultiwayPublicStateDescriptor>& states,
		std::vector<std::uint8_t>& new_state_flags,
		std::vector<MultiwayPackedPublicNode>* existing_nodes) const
	{
		new_state_flags.assign(states.size(), 1U);
		if (existing_nodes != nullptr)
			existing_nodes->assign(states.size(), MultiwayPackedPublicNode{});
		std::vector<std::uint64_t> sorted_keys;
		sorted_keys.reserve(states.size());
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (states[index].id.value == 0U || (index != 0U && states[index - 1U].id.value >= states[index].id.value))
				throw std::invalid_argument("external public-state classification requires sorted unique nonzero ids");
			sorted_keys.push_back(states[index].id.value);
		}
		if (states.empty())
			return;

#pragma pack(push, 8)
		struct ClassificationContext
		{
			const std::vector<MultiwayPublicStateDescriptor>* states = nullptr;
			std::vector<std::uint8_t>* new_state_flags = nullptr;
			std::vector<MultiwayPackedPublicNode>* existing_nodes = nullptr;
			std::size_t next_state = 0U;
		};
#pragma pack(pop)
		ClassificationContext context{ &states, &new_state_flags, existing_nodes, 0U };
		const auto visit = [](void* raw_context, std::uint64_t key,
							   const std::uint8_t* payload, std::size_t payload_size) -> bool {
			auto& current = *static_cast<ClassificationContext*>(raw_context);
			while (current.next_state < current.states->size() && (*current.states)[current.next_state].id.value < key)
				++current.next_state;
			if (current.next_state == current.states->size())
				return false;
			if ((*current.states)[current.next_state].id.value != key)
				return true;

			MultiwayPackedPublicNode existing{};
			const bool found = payload_size == external_public_catalog_record_size() && decode_external_public_node(payload, external_public_node_record_size(), existing) && existing.id.value == key;
			if (found)
			{
				(*current.new_state_flags)[current.next_state] = 0U;
				if (current.existing_nodes != nullptr)
					(*current.existing_nodes)[current.next_state] = existing;
			}
			++current.next_state;
			return current.next_state < current.states->size();
		};
		static_cast<void>(store_.visit_matching_records(sorted_keys, visit, &context));
	}

	void MultiwayExternalPublicCatalog::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwayPackedPublicNode>& nodes)
	{
		if (nodes.empty())
			return;
		const auto record_width = checked_add(external_public_catalog_record_size(), 8U);
		const auto wal_limit = store_.maximum_batch_bytes();
		if (wal_limit < 48U)
			throw std::length_error("external public catalog WAL cannot hold one record");
		const auto max_records = std::min((wal_limit - 48U) / record_width,
			store_.segment_target_bytes() / record_width);
		if (max_records == 0U)
			throw std::length_error("external public catalog segment cannot hold one record");
		const auto chunk_capacity = static_cast<std::size_t>(std::min<std::uint64_t>(
			max_records, std::numeric_limits<std::size_t>::max()));
		std::vector<std::uint64_t> sorted_keys;
		sorted_keys.reserve(nodes.size());
		for (std::size_t index = 0U; index < nodes.size(); ++index)
		{
			if (nodes[index].id.value == 0U || nodes[index].legal_menu_handle != 0U || (index != 0U && nodes[index - 1U].id.value >= nodes[index].id.value))
				throw std::invalid_argument("external menu-less catalog batch requires sorted unique states with menu handle zero");
			sorted_keys.push_back(nodes[index].id.value);
		}
		require_external_records_absent(store_, sorted_keys,
			"external public catalog cannot append an existing state id");
		std::vector<MultiwayExternalRecord> records;
		records.reserve(chunk_capacity);
		std::vector<std::uint8_t> node_bytes;
		std::vector<std::uint8_t> payload;
		for (std::size_t index = 0U; index < nodes.size(); ++index)
		{
			encode_external_public_node(nodes[index], node_bytes);
			payload = node_bytes;
			append_u32(payload, kCatalogMenuMagic);
			append_u32(payload, kCatalogMenuSchema);
			append_u32(payload, 0U);
			payload.resize(external_public_catalog_record_size() - kCatalogMenuChecksumBytes, 0U);
			auto checksum = kHashOffset;
			for (std::size_t byte = node_bytes.size(); byte < payload.size(); ++byte)
				texas::core::fingerprint::append_u8(checksum, payload[byte]);
			append_u64(payload, texas::core::fingerprint::finish(checksum));
			records.push_back({ nodes[index].id.value, payload });
			if (records.size() == chunk_capacity)
			{
				store_.append_batch_continuation(batch_number, merge_fingerprint, records);
				records.clear();
			}
		}
		if (!records.empty())
			store_.append_batch_continuation(batch_number, merge_fingerprint, records);
	}

	void MultiwayExternalPublicCatalog::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwayPackedPublicNode>& nodes,
		const std::vector<MultiwayPublicStateDescriptor>& descriptors)
	{
		if (nodes.size() != descriptors.size())
			throw std::invalid_argument("external public catalog descriptors do not match packed nodes");
		append_discovered_batch_impl(batch_number, merge_fingerprint, descriptors,
			nullptr, &nodes);
	}

	void MultiwayExternalPublicCatalog::append_discovered_batch(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayPublicStateDescriptor>& states,
		const std::vector<std::uint8_t>& new_state_flags)
	{
		append_discovered_batch_impl(batch_number, merge_fingerprint, states,
			&new_state_flags, nullptr);
	}

	void MultiwayExternalPublicCatalog::append_discovered_batch_impl(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayPublicStateDescriptor>& states,
		const std::vector<std::uint8_t>* new_state_flags,
		const std::vector<MultiwayPackedPublicNode>* packed_nodes)
	{
		if ((new_state_flags != nullptr && states.size() != new_state_flags->size()) || (packed_nodes != nullptr && packed_nodes->size() != states.size()))
			throw std::invalid_argument("external public catalog discovery batch dimensions mismatch");
		std::vector<std::uint64_t> sorted_keys;
		sorted_keys.reserve(states.size());
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (states[index].id.value == 0U || (index != 0U && states[index - 1U].id.value >= states[index].id.value))
				throw std::invalid_argument("external public catalog discovery states must be sorted and unique");
			sorted_keys.push_back(states[index].id.value);
		}
		if (new_state_flags != nullptr && std::any_of(new_state_flags->begin(), new_state_flags->end(), [](std::uint8_t flag) { return flag > 1U; }))
			throw std::invalid_argument("external public catalog discovery flags are invalid");
		if (new_state_flags != nullptr)
		{
#pragma pack(push, 8)
			struct ExistingStateContext
			{
				const std::vector<MultiwayPublicStateDescriptor>* states = nullptr;
				const std::vector<std::uint8_t>* new_state_flags = nullptr;
				std::vector<std::uint8_t>* found_existing = nullptr;
				std::size_t next_state = 0U;
			};
#pragma pack(pop)
			std::vector<std::uint8_t> found_existing(states.size(), 0U);
			ExistingStateContext context{ &states, new_state_flags,
				&found_existing, 0U };
			const auto validate_existing = [](void* raw_context, std::uint64_t key,
											   const std::uint8_t*, std::size_t) -> bool {
				auto& current = *static_cast<ExistingStateContext*>(raw_context);
				while (current.next_state < current.states->size() && (*current.states)[current.next_state].id.value < key)
					++current.next_state;
				if (current.next_state == current.states->size() || (*current.states)[current.next_state].id.value != key)
					throw std::logic_error("external catalog lookup returned an unrequested state");
				if ((*current.new_state_flags)[current.next_state] != 0U)
					throw std::invalid_argument("external public catalog cannot append an existing state id");
				(*current.found_existing)[current.next_state] = 1U;
				++current.next_state;
				return true;
			};
			static_cast<void>(store_.visit_matching_records(sorted_keys,
				validate_existing, &context));
			for (std::size_t index = 0U; index < states.size(); ++index)
				if ((*new_state_flags)[index] == 0U && found_existing[index] == 0U)
					throw std::invalid_argument("external public catalog classified a missing state as existing");
		}
		else
			require_external_records_absent(store_, sorted_keys,
				"external public catalog cannot append an existing state id");
		const auto new_count = new_state_flags == nullptr ? states.size() : static_cast<std::size_t>(std::count(new_state_flags->begin(), new_state_flags->end(), 1U));
		if (new_count == 0U)
			return;
		const auto record_width = checked_add(external_public_catalog_record_size(), 8U);
		const auto wal_limit = store_.maximum_batch_bytes();
		if (wal_limit < 48U)
			throw std::length_error("external public catalog WAL cannot hold one record");
		const auto max_records = std::min((wal_limit - 48U) / record_width,
			store_.segment_target_bytes() / record_width);
		if (max_records == 0U)
			throw std::length_error("external public catalog segment cannot hold one record");
		const auto chunk_capacity = static_cast<std::size_t>(std::min<std::uint64_t>(
			max_records, std::numeric_limits<std::size_t>::max()));
		std::uint64_t previous_id = 0U;
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags != nullptr && (*new_state_flags)[index] == 0U)
				continue;
			const auto& descriptor = states[index];
			const auto node = packed_nodes == nullptr
				? MultiwayPublicCatalog::pack_node(descriptor)
				: (*packed_nodes)[index];
			if (node.id.value == 0U || descriptor.id != node.id || node.id.value <= previous_id || descriptor.legal_actions.size() > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
				throw std::invalid_argument("external public catalog batch has invalid identity, order, or menu size");
			previous_id = node.id.value;
			for (std::size_t action = 0U; action < descriptor.legal_actions.size(); ++action)
			{
				const auto& item = descriptor.legal_actions[action];
				if (static_cast<std::uint8_t>(item.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn) || item.action_index != action || item.target_street_contribution < 0 || item.action_menu_id == 0U || (action != 0U && (item.action_menu_id != descriptor.legal_actions.front().action_menu_id || static_cast<std::uint8_t>(item.action) < static_cast<std::uint8_t>(descriptor.legal_actions[action - 1U].action) || (item.action == descriptor.legal_actions[action - 1U].action && item.target_street_contribution <= descriptor.legal_actions[action - 1U].target_street_contribution))))
					throw std::invalid_argument("external public catalog action menu is not canonical");
			}
		}
		std::vector<MultiwayExternalRecord> records;
		records.reserve(chunk_capacity);
		std::vector<std::uint8_t> node_bytes;
		std::vector<std::uint8_t> payload;
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			if (new_state_flags != nullptr && (*new_state_flags)[index] == 0U)
				continue;
			const auto& descriptor = states[index];
			const auto node = packed_nodes == nullptr
				? MultiwayPublicCatalog::pack_node(descriptor)
				: (*packed_nodes)[index];
			auto external_node = node;
			external_node.legal_menu_handle = 0U;
			encode_external_public_node(external_node, node_bytes);
			payload = node_bytes;
			const auto menu_start = payload.size();
			append_u32(payload, kCatalogMenuMagic);
			append_u32(payload, kCatalogMenuSchema);
			append_u32(payload, static_cast<std::uint32_t>(descriptor.legal_actions.size()));
			for (std::size_t action = 0U; action < MULTIWAY_MAX_ABSTRACTED_ACTIONS; ++action)
			{
				if (action < descriptor.legal_actions.size())
				{
					const auto& item = descriptor.legal_actions[action];
					payload.push_back(static_cast<std::uint8_t>(item.action));
					append_u32(payload, item.action_index);
					append_i32(payload, item.target_street_contribution);
					append_u64(payload, item.action_menu_id);
				}
				else
					payload.insert(payload.end(), kCatalogMenuActionBytes, 0U);
			}
			auto checksum = kHashOffset;
			for (std::size_t byte = menu_start; byte < payload.size(); ++byte)
				texas::core::fingerprint::append_u8(checksum, payload[byte]);
			append_u64(payload, texas::core::fingerprint::finish(checksum));
			if (payload.size() != external_public_catalog_record_size())
				throw std::logic_error("external catalog record width changed without a schema update");
			records.push_back({ node.id.value, payload });
			if (records.size() == chunk_capacity)
			{
				store_.append_batch_continuation(batch_number, merge_fingerprint, records);
				records.clear();
			}
		}
		if (!records.empty())
			store_.append_batch_continuation(batch_number, merge_fingerprint, records);
	}

	bool MultiwayExternalPublicCatalog::lookup(MultiwayPublicStateId id,
		MultiwayPackedPublicNode& output) const
	{
		if (id.value == 0U)
			return false;
		std::vector<std::uint8_t> payload;
		if (!store_.lookup(id.value, payload))
			return false;
		return payload.size() == external_public_catalog_record_size() && decode_external_public_node(payload.data(), external_public_node_record_size(), output) && output.id == id;
	}

	bool MultiwayExternalPublicCatalog::lookup_menu(MultiwayPublicStateId id,
		MultiwayActionDescriptor* output, std::size_t output_capacity, std::size_t& action_count) const
	{
		action_count = 0U;
		if (id.value == 0U)
			return false;
		std::vector<std::uint8_t> payload;
		if (!store_.lookup(id.value, payload) || payload.size() != external_public_catalog_record_size())
			return false;
		const auto* menu = payload.data() + external_public_node_record_size();
		if (read_u32(menu) != kCatalogMenuMagic || read_u32(menu + 4U) != kCatalogMenuSchema)
			throw std::invalid_argument("external public catalog menu header is invalid");
		const auto count = read_u32(menu + 8U);
		if (count > MULTIWAY_MAX_ABSTRACTED_ACTIONS || count > output_capacity || (count != 0U && output == nullptr))
			throw std::invalid_argument("external public catalog menu output or count is invalid");
		auto checksum = kHashOffset;
		for (std::size_t byte = 0U; byte < kCatalogMenuBytes - kCatalogMenuChecksumBytes; ++byte)
			texas::core::fingerprint::append_u8(checksum, menu[byte]);
		if (texas::core::fingerprint::finish(checksum) != read_u64(menu + kCatalogMenuBytes - kCatalogMenuChecksumBytes))
			throw std::invalid_argument("external public catalog menu checksum is invalid");
		const auto* action_bytes = menu + kCatalogMenuHeaderBytes;
		std::uint64_t menu_id = 0U;
		for (std::size_t index = 0U; index < MULTIWAY_MAX_ABSTRACTED_ACTIONS; ++index)
		{
			const auto* item = action_bytes + index * kCatalogMenuActionBytes;
			if (index >= count)
			{
				for (std::size_t byte = 0U; byte < kCatalogMenuActionBytes; ++byte)
					if (item[byte] != 0U)
						throw std::invalid_argument("external public catalog menu padding is invalid");
				continue;
			}
			const auto action_kind = item[0];
			const auto action_index = read_u32(item + 1U);
			const auto target = read_i32(item + 5U);
			const auto current_menu_id = read_u64(item + 9U);
			if (action_kind > static_cast<std::uint8_t>(MultiwayAction::AllIn) || action_index != index || target < 0 || current_menu_id == 0U || (index != 0U && current_menu_id != menu_id))
				throw std::invalid_argument("external public catalog action record is invalid");
			menu_id = current_menu_id;
			output[index] = { static_cast<MultiwayAction>(action_kind), action_index, target, current_menu_id };
		}
		action_count = count;
		return true;
	}

	bool MultiwayExternalPublicCatalog::contains(MultiwayPublicStateId id) const
	{
		MultiwayPackedPublicNode node{};
		return lookup(id, node);
	}

	bool MultiwayExternalPublicCatalog::reconstruct_chain(MultiwayPublicStateId id,
		std::vector<MultiwayPackedPublicNode>& parent_first_output) const
	{
		parent_first_output.clear();
		parent_first_output.reserve(maximum_ancestry_depth_);
		MultiwayPublicStateId current = id;
		while (current.value != 0U)
		{
			if (parent_first_output.size() == maximum_ancestry_depth_)
			{
				parent_first_output.clear();
				return false;
			}
			MultiwayPackedPublicNode node{};
			if (!lookup(current, node))
			{
				parent_first_output.clear();
				return false;
			}
			current = node.parent_id;
			parent_first_output.push_back(node);
		}
		std::reverse(parent_first_output.begin(), parent_first_output.end());
		return !parent_first_output.empty();
	}

	bool MultiwayExternalPublicCatalog::reconstruct(MultiwayPublicStateId id,
		MultiwayPublicStateDescriptor& output) const
	{
		std::vector<MultiwayPackedPublicNode> packed_chain;
		if (!reconstruct_chain(id, packed_chain))
			return false;
		const auto& packed = packed_chain.back();
		MultiwayPublicStateDescriptor descriptor{};
		descriptor.id = packed.id;
		descriptor.parent_id = packed.parent_id;
		descriptor.canonical_history_id = packed.canonical_history_id;
		descriptor.board_runout = packed.board_runout;
		descriptor.board.assign(packed.board.begin(), packed.board.begin() + packed.board_count);
		descriptor.incoming_edge.kind = packed.incoming_kind;
		descriptor.incoming_edge.action = packed.incoming_action;
		descriptor.incoming_edge.dealt_card = packed.dealt_card;
		descriptor.incoming_edge.dealt_cards.assign(
			packed.dealt_cards.begin(), packed.dealt_cards.begin() + packed.dealt_card_count);
		descriptor.incoming_edge.transition_board.assign(
			packed.transition_board.begin(), packed.transition_board.begin() + packed.transition_board_count);
		auto& betting = descriptor.betting;
		betting.current_player = packed.betting.current_player;
		betting.last_aggressor = packed.betting.last_aggressor;
		betting.current_bet = packed.betting.current_bet;
		betting.last_full_raise_size = packed.betting.last_full_raise_size;
		betting.big_blind = packed.betting.big_blind;
		betting.street = packed.betting.street;
		const auto seat_count = packed.betting.seat_count;
		betting.stacks.assign(packed.betting.stacks.begin(), packed.betting.stacks.begin() + seat_count);
		betting.contributions.assign(packed.betting.contributions.begin(), packed.betting.contributions.begin() + seat_count);
		betting.street_contributions.assign(packed.betting.street_contributions.begin(), packed.betting.street_contributions.begin() + seat_count);
		betting.bet_faced_when_acted.assign(
			packed.betting.bet_faced_when_acted.begin(), packed.betting.bet_faced_when_acted.begin() + seat_count);
		betting.folded.resize(seat_count);
		betting.all_in.resize(seat_count);
		betting.may_raise.resize(seat_count);
		betting.pending.resize(seat_count);
		betting.has_acted.resize(seat_count);
		for (std::size_t seat = 0U; seat < seat_count; ++seat)
		{
			betting.folded[seat] = packed.betting.folded[seat] != 0U;
			betting.all_in[seat] = packed.betting.all_in[seat] != 0U;
			betting.may_raise[seat] = packed.betting.may_raise[seat] != 0U;
			betting.pending[seat] = packed.betting.pending[seat] != 0U;
			betting.has_acted[seat] = packed.betting.has_acted[seat] != 0U;
		}
		descriptor.legal_actions.resize(MULTIWAY_MAX_ABSTRACTED_ACTIONS);
		std::size_t action_count = 0U;
		if (!lookup_menu(packed.id, descriptor.legal_actions.data(), descriptor.legal_actions.size(), action_count))
			return false;
		descriptor.legal_actions.resize(action_count);
		for (std::size_t index = 1U; index < packed_chain.size(); ++index)
		{
			const auto& ancestor = packed_chain[index];
			if (ancestor.incoming_kind == MultiwayPublicParentEdgeKind::BettingAction)
			{
				auto action = ancestor.incoming_action;
				action.action_index = 0U;
				descriptor.history.push_back({ ancestor.incoming_actor, action });
			}
		}
		output = std::move(descriptor);
		return true;
	}
} // namespace texas::solver::multiway
