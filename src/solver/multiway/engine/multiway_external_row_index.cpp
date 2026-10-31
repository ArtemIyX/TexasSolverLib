#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalRowIndex::MultiwayExternalRowIndex(
		MultiwayExternalStorageConfig config, std::uint32_t page_width)
		: store_(std::move(config), 8U + 6U * 24U), page_width_(page_width)
	{
		if (page_width_ == 0U)
			throw std::invalid_argument("external row index requires a positive page width");
		std::vector<std::uint8_t> allocator;
		if (store_.lookup(0U, allocator))
		{
			if (allocator.size() != 8U + 6U * 24U || read_u32(allocator.data()) != 0x3152574dU || read_u32(allocator.data() + 4U) != 1U || read_u64(allocator.data() + 8U) == 0U)
				throw std::invalid_argument("external row page-id allocator record is invalid");
			next_page_id_ = read_u64(allocator.data() + 8U);
		}
		else if (store_.record_count() != 0U)
			throw std::invalid_argument("external row index is missing its page-id allocator record");
	}

	void MultiwayExternalRowIndex::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwaySparseRowShape>& rows,
		std::uint64_t& next_page_id)
	{
		if (rows.empty())
			return;
		constexpr std::uint64_t row_payload_bytes = 8U + 6U * 24U;
		const auto wal_limit = store_.maximum_batch_bytes();
		const auto row_record_width = 8U + row_payload_bytes;
		const auto segment_record_capacity = store_.segment_target_bytes() / row_record_width;
		if (wal_limit < 48U + 2U * row_record_width || rows.size() > (wal_limit - 48U) / row_record_width - 1U || segment_record_capacity == 0U || rows.size() > segment_record_capacity - 1U)
			throw std::length_error("external row admission exceeds its bounded WAL or segment");
		constexpr std::uint32_t row_magic = 0x3152574dU;
		constexpr std::uint32_t row_schema = 1U;
		std::vector<std::size_t> order(rows.size());
		for (std::size_t index = 0U; index < order.size(); ++index)
			order[index] = index;
		std::sort(order.begin(), order.end(), [&rows](std::size_t left, std::size_t right) {
			const auto& a = rows[left].infoset;
			const auto& b = rows[right].infoset;
			return a.public_state.value != b.public_state.value
				? a.public_state.value < b.public_state.value
				: a.seat < b.seat;
		});
		std::vector<MultiwayExternalRecord> records;
		records.reserve(rows.size() + 1U);
		if (next_page_id != next_page_id_)
			throw std::invalid_argument("external row page-id allocator does not match the durable generation");
		auto candidate_next_page_id = next_page_id;
		std::vector<std::uint64_t> sorted_state_ids;
		sorted_state_ids.reserve(rows.size());
		std::size_t cursor = 0U;
		while (cursor < order.size())
		{
			const auto& first_row = rows[order[cursor]];
			const auto state_id = first_row.infoset.public_state.value;
			if (state_id == 0U)
				throw std::invalid_argument("external row state id is invalid");
			std::vector<std::uint8_t> payload(8U + 6U * 24U, 0U);
			for (unsigned shift = 0U; shift < 32U; shift += 8U)
			{
				payload[shift / 8U] = static_cast<std::uint8_t>(row_magic >> shift);
				payload[4U + shift / 8U] = static_cast<std::uint8_t>(row_schema >> shift);
			}
			sorted_state_ids.push_back(state_id);
			records.push_back({ state_id, std::move(payload) });
			while (cursor < order.size() && rows[order[cursor]].infoset.public_state.value == state_id)
				++cursor;
		}
#pragma pack(push, 8)
		struct ExistingRowContext
		{
			std::vector<MultiwayExternalRecord>* records = nullptr;
			std::size_t next_record = 0U;
		};
#pragma pack(pop)
		ExistingRowContext existing_rows{ &records, 0U };
		const auto copy_existing_row = [](void* raw_context, std::uint64_t state_id,
										   const std::uint8_t* payload, std::size_t payload_size) -> bool {
			auto& current = *static_cast<ExistingRowContext*>(raw_context);
			while (current.next_record < current.records->size() && (*current.records)[current.next_record].key < state_id)
				++current.next_record;
			if (current.next_record == current.records->size() || (*current.records)[current.next_record].key != state_id)
				throw std::logic_error("external row lookup returned an unrequested state");
			constexpr std::size_t row_payload_size = 8U + 6U * 24U;
			constexpr std::uint32_t expected_magic = 0x3152574dU;
			constexpr std::uint32_t expected_schema = 1U;
			if (payload_size != row_payload_size || read_u32(payload) != expected_magic || read_u32(payload + 4U) != expected_schema)
				throw std::invalid_argument("external row metadata record schema is invalid");
			auto& output = (*current.records)[current.next_record].payload;
			std::copy_n(payload, row_payload_size, output.data());
			++current.next_record;
			return true;
		};
		static_cast<void>(store_.visit_matching_records(sorted_state_ids,
			copy_existing_row, &existing_rows));

		cursor = 0U;
		std::size_t record_index = 0U;
		while (cursor < order.size())
		{
			const auto state_id = rows[order[cursor]].infoset.public_state.value;
			if (record_index >= records.size() || records[record_index].key != state_id)
				throw std::logic_error("external row output records are not aligned with sorted rows");
			auto& payload = records[record_index].payload;
			if (read_u32(payload.data()) != row_magic || read_u32(payload.data() + 4U) != row_schema)
				throw std::invalid_argument("external row metadata record schema is invalid");
			std::int32_t prior_seat = -1;
			while (cursor < order.size() && rows[order[cursor]].infoset.public_state.value == state_id)
			{
				const auto& row = rows[order[cursor]];
				const auto seat = row.infoset.seat;
				if (seat < 0 || seat >= 6 || seat <= prior_seat || row.bucket_count == 0U || row.action_count == 0U || row.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
					throw std::invalid_argument("external row metadata batch has invalid seat, order, or shape");
				prior_seat = seat;
				const auto slot = 8U + static_cast<std::size_t>(seat) * 24U;
				if (payload[slot] != 0U)
					throw std::invalid_argument("external row index cannot append an existing infoset");
				const auto page_count64 = (static_cast<std::uint64_t>(row.bucket_count) + page_width_ - 1U) / page_width_;
				if (page_count64 > std::numeric_limits<std::uint32_t>::max() || candidate_next_page_id == 0U || page_count64 > std::numeric_limits<std::uint64_t>::max() - candidate_next_page_id)
					throw std::overflow_error("external row page identity overflows");
				payload[slot] = 1U;
				payload[slot + 1U] = row.action_count;
				for (unsigned shift = 0U; shift < 32U; shift += 8U)
					payload[slot + 4U + shift / 8U] = static_cast<std::uint8_t>(row.bucket_count >> shift);
				for (unsigned shift = 0U; shift < 64U; shift += 8U)
					payload[slot + 8U + shift / 8U] = static_cast<std::uint8_t>(candidate_next_page_id >> shift);
				const auto page_count = static_cast<std::uint32_t>(page_count64);
				for (unsigned shift = 0U; shift < 32U; shift += 8U)
					payload[slot + 16U + shift / 8U] = static_cast<std::uint8_t>(page_count >> shift);
				candidate_next_page_id = checked_add(candidate_next_page_id, page_count64);
				++cursor;
			}
			++record_index;
		}
		std::vector<std::uint8_t> allocator_payload(8U + 6U * 24U, 0U);
		for (unsigned shift = 0U; shift < 32U; shift += 8U)
		{
			allocator_payload[shift / 8U] = static_cast<std::uint8_t>(row_magic >> shift);
			allocator_payload[4U + shift / 8U] = static_cast<std::uint8_t>(row_schema >> shift);
		}
		for (unsigned shift = 0U; shift < 64U; shift += 8U)
			allocator_payload[8U + shift / 8U] = static_cast<std::uint8_t>(candidate_next_page_id >> shift);
		records.insert(records.begin(), { 0U, std::move(allocator_payload) });
		store_.append_batch(batch_number, merge_fingerprint, records);
		next_page_id = candidate_next_page_id;
		next_page_id_ = candidate_next_page_id;
	}

	bool MultiwayExternalRowIndex::lookup(MultiwayPublicStateId state,
		std::int32_t seat, MultiwayExternalRowMetadata& output) const
	{
		if (state.value == 0U || seat < 0 || seat >= 6)
			return false;
		std::array<std::uint8_t, 8U + 6U * 24U> payload{};
		if (!store_.lookup(state.value, payload.data(), payload.size()))
			return false;
		if (payload.size() != 8U + 6U * 24U || read_u32(payload.data()) != 0x3152574dU || read_u32(payload.data() + 4U) != 1U)
			throw std::invalid_argument("external row metadata record schema is invalid");
		const auto slot = 8U + static_cast<std::size_t>(seat) * 24U;
		if (payload[slot] == 0U)
			return false;
		if (payload[slot] != 1U || payload[slot + 2U] != 0U || payload[slot + 3U] != 0U || read_u32(payload.data() + slot + 20U) != 0U)
			throw std::invalid_argument("external row metadata flags or reserved fields are invalid");
		output.action_count = payload[slot + 1U];
		output.bucket_count = read_u32(payload.data() + slot + 4U);
		output.first_page_id = read_u64(payload.data() + slot + 8U);
		output.page_count = read_u32(payload.data() + slot + 16U);
		if (output.action_count == 0U || output.bucket_count == 0U || output.first_page_id == 0U || output.page_count == 0U)
			throw std::invalid_argument("external row metadata contains an invalid shape");
		const auto expected_pages = (static_cast<std::uint64_t>(output.bucket_count) + page_width_ - 1U) / page_width_;
		if (output.page_count != expected_pages || output.page_count > std::numeric_limits<std::uint64_t>::max() - output.first_page_id)
			throw std::invalid_argument("external row metadata page range is inconsistent");
		return true;
	}

	void MultiwayExternalRowIndex::lookup_rows(
		const std::vector<MultiwaySparseRowShape>& rows,
		std::vector<MultiwayExternalRowMetadata>& output) const
	{
#pragma pack(push, 8)
		struct Query
		{
			std::uint64_t state_id = 0U;
			std::size_t row_index = 0U;
			std::int32_t seat = -1;
		};
#pragma pack(pop)
		std::vector<Query> queries;
		queries.reserve(rows.size());
		output.assign(rows.size(), MultiwayExternalRowMetadata{});
		for (std::size_t index = 0U; index < rows.size(); ++index)
		{
			const auto& infoset = rows[index].infoset;
			if (infoset.public_state.value != 0U && infoset.seat >= 0 && infoset.seat < 6)
				queries.push_back({ infoset.public_state.value, index, infoset.seat });
		}
		std::sort(queries.begin(), queries.end(), [](const Query& left, const Query& right) {
			return left.state_id != right.state_id
				? left.state_id < right.state_id
				: left.seat != right.seat ? left.seat < right.seat
										  : left.row_index < right.row_index;
		});
		std::vector<std::uint64_t> sorted_state_ids;
		sorted_state_ids.reserve(queries.size());
		for (const auto& query : queries)
			if (sorted_state_ids.empty() || sorted_state_ids.back() != query.state_id)
				sorted_state_ids.push_back(query.state_id);
		if (sorted_state_ids.empty())
			return;

#pragma pack(push, 8)
		struct LookupContext
		{
			const std::vector<Query>* queries = nullptr;
			std::vector<MultiwayExternalRowMetadata>* output = nullptr;
			std::size_t next_query = 0U;
			std::uint32_t page_width = 0U;
		};
#pragma pack(pop)
		LookupContext context{ &queries, &output, 0U, page_width_ };
		const auto visit = [](void* raw_context, std::uint64_t state_id,
							   const std::uint8_t* payload, std::size_t payload_size) -> bool {
			auto& current = *static_cast<LookupContext*>(raw_context);
			constexpr std::size_t row_payload_size = 8U + 6U * 24U;
			if (payload == nullptr || payload_size != row_payload_size || read_u32(payload) != 0x3152574dU || read_u32(payload + 4U) != 1U)
				throw std::invalid_argument("external row metadata record schema is invalid");
			while (current.next_query < current.queries->size() && (*current.queries)[current.next_query].state_id < state_id)
				++current.next_query;
			while (current.next_query < current.queries->size() && (*current.queries)[current.next_query].state_id == state_id)
			{
				const auto& query = (*current.queries)[current.next_query++];
				const auto slot = 8U + static_cast<std::size_t>(query.seat) * 24U;
				if (payload[slot] == 0U)
					continue;
				if (payload[slot] != 1U || payload[slot + 2U] != 0U || payload[slot + 3U] != 0U || read_u32(payload + slot + 20U) != 0U)
					throw std::invalid_argument("external row metadata flags or reserved fields are invalid");
				auto& row = (*current.output)[query.row_index];
				row.action_count = payload[slot + 1U];
				row.bucket_count = read_u32(payload + slot + 4U);
				row.first_page_id = read_u64(payload + slot + 8U);
				row.page_count = read_u32(payload + slot + 16U);
				if (row.action_count == 0U || row.bucket_count == 0U || row.first_page_id == 0U || row.page_count == 0U)
					throw std::invalid_argument("external row metadata contains an invalid shape");
				const auto expected_pages =
					(static_cast<std::uint64_t>(row.bucket_count) + current.page_width - 1U) / current.page_width;
				if (row.page_count != expected_pages || row.page_count > std::numeric_limits<std::uint64_t>::max() - row.first_page_id)
					throw std::invalid_argument("external row metadata page range is inconsistent");
			}
			return true;
		};
		static_cast<void>(store_.visit_matching_records(sorted_state_ids, visit, &context));
	}

	bool MultiwayExternalRowIndex::visit_ordered_rows(
		OrderedRowVisitor visitor, void* context) const
	{
		if (visitor == nullptr)
			throw std::invalid_argument("external ordered-row visitor is missing");
#pragma pack(push, 8)
		struct VisitorContext
		{
			OrderedRowVisitor visitor;
			void* context;
			std::uint32_t page_width;
			const MultiwayExternalRowIndex* owner;
		};
#pragma pack(pop)
		VisitorContext visitor_context{ visitor, context, page_width_, this };
		const auto visit_record = [](void* opaque, std::uint64_t state_id,
									  const std::uint8_t* payload, std::size_t payload_size) {
			auto& current = *static_cast<VisitorContext*>(opaque);
			constexpr std::size_t row_payload_size = 8U + 6U * 24U;
			constexpr std::uint32_t row_magic = 0x3152574dU;
			constexpr std::uint32_t row_schema = 1U;
			if (payload == nullptr || payload_size != row_payload_size || read_u32(payload) != row_magic || read_u32(payload + 4U) != row_schema)
				throw std::invalid_argument("external ordered row record schema is invalid");
			if (state_id == 0U)
			{
				if (read_u64(payload + 8U) == 0U)
					throw std::invalid_argument("external ordered row page allocator is invalid");
				for (std::size_t index = 16U; index < row_payload_size; ++index)
					if (payload[index] != 0U)
						throw std::invalid_argument("external ordered row allocator reserved bytes are nonzero");
				return true;
			}
			bool found_row = false;
			for (std::int32_t seat = 0; seat < 6; ++seat)
			{
				const auto slot = 8U + static_cast<std::size_t>(seat) * 24U;
				if (payload[slot] == 0U)
				{
					for (std::size_t index = slot + 1U; index < slot + 24U; ++index)
						if (payload[index] != 0U)
							throw std::invalid_argument("external ordered row absent-seat metadata is nonzero");
					continue;
				}
				if (payload[slot] != 1U || payload[slot + 2U] != 0U || payload[slot + 3U] != 0U || read_u32(payload + slot + 20U) != 0U)
					throw std::invalid_argument("external ordered row seat flags are invalid");
				MultiwayExternalRowMetadata row{};
				row.action_count = payload[slot + 1U];
				row.bucket_count = read_u32(payload + slot + 4U);
				row.first_page_id = read_u64(payload + slot + 8U);
				row.page_count = read_u32(payload + slot + 16U);
				const auto expected_pages = (static_cast<std::uint64_t>(row.bucket_count) + current.page_width - 1U) / current.page_width;
				if (row.action_count == 0U || row.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS || row.bucket_count == 0U || row.first_page_id == 0U || row.page_count == 0U || row.page_count != expected_pages || row.page_count > std::numeric_limits<std::uint64_t>::max() - row.first_page_id)
					throw std::invalid_argument("external ordered row shape is invalid");
				found_row = true;
				MultiwayExternalRowBinding binding;
				binding.owner_ = current.owner;
				binding.state_id_ = state_id;
				binding.seat_ = seat;
				binding.row_ = row;
				if (!current.visitor(current.context, binding))
					return false;
			}
			if (!found_row)
				throw std::invalid_argument("external ordered row record has no seat rows");
			return true;
		};
		return store_.visit_ordered_records(visit_record, &visitor_context);
	}

	bool MultiwayExternalRowIndex::locate_bucket(MultiwayPublicStateId state,
		std::int32_t seat, std::uint32_t bucket, MultiwayExternalRowPage& output) const
	{
		MultiwayExternalRowMetadata metadata{};
		if (!lookup(state, seat, metadata))
			return false;
		MultiwayExternalRowBinding binding;
		binding.owner_ = this;
		binding.state_id_ = state.value;
		binding.seat_ = seat;
		binding.row_ = metadata;
		return locate_bucket(binding, bucket, output);
	}

	bool MultiwayExternalRowIndex::locate_bucket(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, MultiwayExternalRowPage& output) const
	{
		const auto& row = binding.row_;
		if (binding.owner_ != this || binding.state_id_ == 0U || binding.seat_ < 0 || binding.seat_ >= 6 || row.bucket_count == 0U || row.action_count == 0U || row.first_page_id == 0U || row.page_count == 0U || bucket >= row.bucket_count)
			return false;
		const auto page_index = bucket / page_width_;
		if (page_index >= row.page_count || page_index > std::numeric_limits<std::uint64_t>::max() - row.first_page_id)
			throw std::invalid_argument("external row metadata does not cover its bucket range");
		const auto first_bucket = static_cast<std::uint64_t>(page_index) * page_width_;
		const auto page_bucket_count = std::min<std::uint64_t>(page_width_,
			static_cast<std::uint64_t>(row.bucket_count) - first_bucket);
		output = { row.first_page_id + page_index, page_index,
			static_cast<std::uint32_t>(page_bucket_count), row.action_count };
		return true;
	}

	MultiwayPublicStateId MultiwayExternalRowBinding::public_state() const noexcept
	{
		return { state_id_ };
	}
} // namespace texas::solver::multiway
