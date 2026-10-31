#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	std::size_t MultiwayExternalCompactPages::page_payload_size() const noexcept
	{
		return kPageRecordHeaderBytes + MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::size_t>(page_width_) * 12U + kPageRecordChecksumBytes;
	}

	MultiwayExternalCompactPages::MultiwayExternalCompactPages(
		MultiwayExternalStorageConfig config, std::uint32_t page_width,
		std::uint64_t cache_bytes, std::uint64_t maximum_page_id)
		: page_width_(page_width)
	{
		if (page_width_ == 0U || cache_bytes > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external compact pages require bounded positive dimensions");
		const auto payload = kPageRecordHeaderBytes + MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::uint64_t>(page_width_) * 12U + kPageRecordChecksumBytes;
		if (payload > std::numeric_limits<std::size_t>::max() || payload > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("external compact page payload width overflows");
		const auto store_cache_bytes = payload + 1024U;
		if (maximum_page_id >= std::numeric_limits<std::size_t>::max())
			throw std::length_error("external compact page lookup index exceeds size_t");
		const auto lookup_slot_count = maximum_page_id == 0U ? 0U : maximum_page_id + 1U;
		if (lookup_slot_count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
			throw std::length_error("external compact page lookup index byte count overflows size_t");
		const auto lookup_index_bytes = lookup_slot_count * sizeof(std::uint32_t);
		if (lookup_index_bytes > cache_bytes ||
			cache_bytes - lookup_index_bytes <= store_cache_bytes + 2U * payload)
			throw std::invalid_argument("external compact page cache cannot hold one working page");
		if (lookup_slot_count != 0U)
			page_lookup_slots_.resize(static_cast<std::size_t>(lookup_slot_count), 0U);
		config.cache_bytes = store_cache_bytes;
		store_cache_budget_bytes_ = store_cache_bytes;
		store_ = std::make_unique<MultiwayExternalRecordStore>(config, static_cast<std::size_t>(payload));
		lookup_scratch_.resize(static_cast<std::size_t>(payload));
		encode_scratch_.reserve(static_cast<std::size_t>(payload));
		const auto cells = MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::size_t>(page_width_);
		constexpr auto bytes_per_cell = 2U * sizeof(std::int32_t) + 2U * sizeof(std::uint64_t);
		if (cells > (std::numeric_limits<std::size_t>::max() - sizeof(Page)) / bytes_per_cell)
			throw std::length_error("external compact page cache cell arena overflows size_t");
		const auto page_memory = sizeof(Page) + cells * bytes_per_cell;
		// Publication temporarily holds a pointer, record metadata and encoded copy
		// for every dirty page. Reserve that worst case within the same budget.
		const auto publication_memory = sizeof(Page*) + sizeof(MultiwayExternalRecord) + payload;
		if (publication_memory > std::numeric_limits<std::size_t>::max() - page_memory)
			throw std::length_error("external compact page publication budget overflows size_t");
		const auto scratch_bytes = lookup_scratch_.capacity() + encode_scratch_.capacity();
		if (lookup_index_bytes > cache_bytes ||
			cache_bytes - lookup_index_bytes <= store_cache_bytes + scratch_bytes)
			throw std::invalid_argument("external compact page cache cannot hold fixed scratch");
		const auto page_budget = cache_bytes - lookup_index_bytes - store_cache_bytes - scratch_bytes;
		auto capacity = page_budget / (page_memory + publication_memory);
		const auto record_width = payload + 8U;
		capacity = std::min<std::uint64_t>(capacity, (config.maximum_wal_bytes - 48U) / record_width);
		capacity = std::min<std::uint64_t>(capacity, config.segment_target_bytes / record_width);
		if (capacity == 0U || capacity > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external compact page cache cannot hold a committable page");
		if (capacity > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("external compact page cache slots exceed index width");
		pages_.resize(static_cast<std::size_t>(capacity));
		for (auto& page : pages_)
		{
			page.regrets.resize(cells);
			page.strategy_masses.resize(cells);
			page.before_regrets.resize(cells);
			page.before_strategy_masses.resize(cells);
		}
		if (memory_bytes() > cache_bytes)
			throw std::length_error("external compact page allocation exceeds its configured cache bound");
	}

	MultiwayExternalCompactPages::Page* MultiwayExternalCompactPages::find(std::uint64_t page_id) noexcept
	{
		if (!page_lookup_slots_.empty())
		{
			if (page_id >= page_lookup_slots_.size())
				return nullptr;
			const auto encoded_slot = page_lookup_slots_[static_cast<std::size_t>(page_id)];
			if (encoded_slot == 0U)
				return nullptr;
			const auto slot = static_cast<std::size_t>(encoded_slot - 1U);
			if (slot >= pages_.size())
				return nullptr;
			auto& page = pages_[slot];
			return page.valid && page.page_id == page_id ? &page : nullptr;
		}
		for (auto& page : pages_)
			if (page.valid && page.page_id == page_id)
				return &page;
		return nullptr;
	}

	void MultiwayExternalCompactPages::validate_page_id(std::uint64_t page_id) const
	{
		if (!page_lookup_slots_.empty() && page_id >= page_lookup_slots_.size())
			throw std::length_error("external compact page id exceeds lookup index bound");
	}

	void MultiwayExternalCompactPages::cache_page(Page& page, std::size_t slot)
	{
		if (page_lookup_slots_.empty())
			return;
		if (page.page_id == 0U || page.page_id >= page_lookup_slots_.size() ||
			slot >= pages_.size() || slot >= std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("external compact page id or cache slot exceeds lookup index bounds");
		page_lookup_slots_[static_cast<std::size_t>(page.page_id)] =
			static_cast<std::uint32_t>(slot + 1U);
	}

	void MultiwayExternalCompactPages::forget_cached_page(Page& page, std::size_t slot) noexcept
	{
		if (page_lookup_slots_.empty() || !page.valid || page.page_id >= page_lookup_slots_.size() ||
			slot >= std::numeric_limits<std::uint32_t>::max())
			return;
		auto& encoded_slot = page_lookup_slots_[static_cast<std::size_t>(page.page_id)];
		if (encoded_slot == static_cast<std::uint32_t>(slot + 1U))
			encoded_slot = 0U;
	}

	void MultiwayExternalCompactPages::decode_page(const std::uint8_t* bytes, Page& page) const
	{
		if (read_u32(bytes) != kPageRecordMagic || read_u32(bytes + 4U) != kPageRecordSchema)
			throw std::invalid_argument("external compact page schema is invalid");
		auto hash = kHashOffset;
		for (std::size_t index = 0U; index + kPageRecordChecksumBytes < page_payload_size(); ++index)
			texas::core::fingerprint::append_u8(hash, bytes[index]);
		if (texas::core::fingerprint::finish(hash) != read_u64(bytes + page_payload_size() - kPageRecordChecksumBytes))
			throw std::invalid_argument("external compact page checksum is invalid");
		std::size_t cursor = 8U;
		page.page_id = read_u64(bytes + cursor);
		cursor += 8U;
		page.state_id = read_u64(bytes + cursor);
		cursor += 8U;
		page.seat = read_i32(bytes + cursor);
		cursor += 4U;
		page.page_index = read_u32(bytes + cursor);
		cursor += 4U;
		page.bucket_count = read_u32(bytes + cursor);
		cursor += 4U;
		page.action_count = bytes[cursor++];
		if (bytes[cursor] != 0U || bytes[cursor + 1U] != 0U || bytes[cursor + 2U] != 0U)
			throw std::invalid_argument("external compact page reserved bytes are invalid");
		cursor += 3U;
		for (auto& value : page.regrets)
		{
			value = read_i32(bytes + cursor);
			cursor += 4U;
		}
		for (auto& value : page.strategy_masses)
		{
			value = read_u64(bytes + cursor);
			cursor += 8U;
		}
		if (cursor + kPageRecordChecksumBytes != page_payload_size() || page.page_id == 0U || page.state_id == 0U || page.seat < 0 || page.seat >= 6 || page.bucket_count == 0U || page.bucket_count > page_width_ || page.action_count == 0U || page.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("external compact page identity or shape is invalid");
	}

	void MultiwayExternalCompactPages::encode_page(const Page& page,
		std::vector<std::uint8_t>& bytes) const
	{
		bytes.clear();
		bytes.reserve(page_payload_size());
		append_u32(bytes, kPageRecordMagic);
		append_u32(bytes, kPageRecordSchema);
		append_u64(bytes, page.page_id);
		append_u64(bytes, page.state_id);
		append_i32(bytes, page.seat);
		append_u32(bytes, page.page_index);
		append_u32(bytes, page.bucket_count);
		bytes.push_back(page.action_count);
		bytes.insert(bytes.end(), 3U, 0U);
		for (const auto value : page.regrets)
			append_i32(bytes, value);
		for (const auto value : page.strategy_masses)
			append_u64(bytes, value);
		auto hash = kHashOffset;
		for (const auto byte : bytes)
			texas::core::fingerprint::append_u8(hash, byte);
		append_u64(bytes, texas::core::fingerprint::finish(hash));
		if (bytes.size() != page_payload_size())
			throw std::logic_error("external compact page codec width changed without a schema update");
	}

	void MultiwayExternalCompactPages::begin_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (batch_open_ || batch_number == 0U)
			throw std::logic_error("external compact page batch state is invalid");
		for (const auto& page : pages_)
			if (page.dirty || page.pin_count != 0U)
				throw std::logic_error("external compact page cache is not at a clean batch boundary");
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	std::uint64_t MultiwayExternalCompactPages::next_batch_number() const
	{
		const auto last = store_->last_batch_number();
		if (last == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external compact page batch number overflows");
		return last + 1U;
	}

	MultiwayExternalCompactPages::PageView MultiwayExternalCompactPages::pin_existing(
		std::uint64_t page_id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (page_id == 0U)
			throw std::invalid_argument("external compact page id is invalid");
		validate_page_id(page_id);
		if (auto* cached = find(page_id))
		{
			++page_cache_hits_;
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external compact page pin count overflows");
			++cached->pin_count;
			return { page_id, cached->regrets.data(), cached->strategy_masses.data(),
				cached->bucket_count, cached->action_count, page_width_ };
		}
		++page_cache_misses_;
		if (!store_->lookup(page_id, lookup_scratch_))
			return {};
		Page* selected = nullptr;
		for (std::size_t offset = 0U; offset < pages_.size(); ++offset)
		{
			const auto index = (next_victim_ + offset) % pages_.size();
			auto& candidate = pages_[index];
			if (!candidate.valid || (candidate.pin_count == 0U && !candidate.dirty))
			{
				selected = &candidate;
				next_victim_ = (index + 1U) % pages_.size();
				break;
			}
		}
		if (selected == nullptr)
			throw std::length_error("external compact page cache has no clean unpinned page");
		if (selected->valid)
			++page_cache_evictions_;
		const auto selected_slot = static_cast<std::size_t>(selected - pages_.data());
		forget_cached_page(*selected, selected_slot);
		selected->valid = false;
		decode_page(lookup_scratch_.data(), *selected);
		if (selected->page_id != page_id)
			throw std::invalid_argument("external compact page key disagrees with its record");
		selected->valid = true;
		cache_page(*selected, selected_slot);
		selected->dirty = false;
		selected->pin_count = 1U;
		return { page_id, selected->regrets.data(), selected->strategy_masses.data(),
			selected->bucket_count, selected->action_count, page_width_ };
	}

	MultiwayExternalCompactPages::PageView MultiwayExternalCompactPages::pin(
		std::uint64_t page_id, MultiwayPublicStateId state, std::int32_t seat,
		std::uint32_t page_index, std::uint32_t bucket_count, std::uint8_t action_count)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (page_id == 0U || state.value == 0U || seat < 0 || seat >= 6 || bucket_count == 0U || bucket_count > page_width_ || action_count == 0U || action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("external compact page lookup identity or shape is invalid");
		validate_page_id(page_id);
		if (auto* cached = find(page_id))
		{
			++page_cache_hits_;
			if (cached->state_id != state.value || cached->seat != seat || cached->page_index != page_index || cached->bucket_count != bucket_count || cached->action_count != action_count)
				throw std::invalid_argument("external compact page identity disagrees with row metadata");
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external compact page pin count overflows");
			++cached->pin_count;
			return { page_id, cached->regrets.data(), cached->strategy_masses.data(), bucket_count,
				action_count, page_width_ };
		}
		++page_cache_misses_;
		Page* selected = nullptr;
		for (std::size_t offset = 0U; offset < pages_.size(); ++offset)
		{
			const auto index = (next_victim_ + offset) % pages_.size();
			auto& candidate = pages_[index];
			if (!candidate.valid || (candidate.pin_count == 0U && !candidate.dirty))
			{
				selected = &candidate;
				next_victim_ = (index + 1U) % pages_.size();
				break;
			}
		}
		if (selected == nullptr)
			throw std::length_error("external compact page cache has no clean unpinned page");
		if (selected->valid)
			++page_cache_evictions_;
		const auto selected_slot = static_cast<std::size_t>(selected - pages_.data());
		forget_cached_page(*selected, selected_slot);
		selected->valid = false;
		const auto found = store_->lookup(page_id, lookup_scratch_);
		if (found)
		{
			decode_page(lookup_scratch_.data(), *selected);
			if (selected->page_id != page_id || selected->state_id != state.value || selected->seat != seat || selected->page_index != page_index || selected->bucket_count != bucket_count || selected->action_count != action_count)
				throw std::invalid_argument("external compact page identity disagrees with requested row");
		}
		else
		{
			std::fill(selected->regrets.begin(), selected->regrets.end(), 0);
			std::fill(selected->strategy_masses.begin(), selected->strategy_masses.end(), 0U);
			selected->page_id = page_id;
			selected->state_id = state.value;
			selected->seat = seat;
			selected->page_index = page_index;
			selected->bucket_count = bucket_count;
			selected->action_count = action_count;
		}
		selected->valid = true;
		cache_page(*selected, selected_slot);
		selected->dirty = false;
		selected->pin_count = 1U;
		return { page_id, selected->regrets.data(), selected->strategy_masses.data(), bucket_count,
			action_count, page_width_ };
	}

	MultiwayExternalCompactPages::PageView MultiwayExternalCompactPages::pin_profiled(
		std::uint64_t page_id, MultiwayPublicStateId state, std::int32_t seat,
		std::uint32_t page_index, std::uint32_t bucket_count, std::uint8_t action_count,
		MultiwayExternalPageProfile* profile)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (page_id == 0U || state.value == 0U || seat < 0 || seat >= 6 || bucket_count == 0U || bucket_count > page_width_ || action_count == 0U || action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("external compact page lookup identity or shape is invalid");
		validate_page_id(page_id);
		Page* cached = nullptr;
		if (profile != nullptr)
		{
			OptionalExternalTimer timer(&profile->cache_find_nanoseconds);
			cached = find(page_id);
		}
		else
			cached = find(page_id);
		if (cached != nullptr)
		{
			++page_cache_hits_;
			if (cached->state_id != state.value || cached->seat != seat || cached->page_index != page_index || cached->bucket_count != bucket_count || cached->action_count != action_count)
				throw std::invalid_argument("external compact page identity disagrees with row metadata");
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external compact page pin count overflows");
			++cached->pin_count;
			return { page_id, cached->regrets.data(), cached->strategy_masses.data(), bucket_count,
				action_count, page_width_ };
		}
		++page_cache_misses_;
		Page* selected = nullptr;
		const auto select_victim = [&]() {
			for (std::size_t offset = 0U; offset < pages_.size(); ++offset)
			{
				const auto index = (next_victim_ + offset) % pages_.size();
				auto& candidate = pages_[index];
				if (!candidate.valid || (candidate.pin_count == 0U && !candidate.dirty))
				{
					selected = &candidate;
					next_victim_ = (index + 1U) % pages_.size();
					break;
				}
			}
		};
		if (profile != nullptr)
		{
			OptionalExternalTimer timer(&profile->victim_nanoseconds);
			select_victim();
		}
		else
			select_victim();
		if (selected == nullptr)
			throw std::length_error("external compact page cache has no clean unpinned page");
		if (selected->valid)
			++page_cache_evictions_;
		const auto selected_slot = static_cast<std::size_t>(selected - pages_.data());
		forget_cached_page(*selected, selected_slot);
		selected->valid = false;
		bool found = false;
		if (profile != nullptr)
		{
			OptionalExternalTimer timer(&profile->store_lookup_nanoseconds);
			found = store_->lookup_profiled(page_id, lookup_scratch_, profile->record_lookup);
		}
		else
			found = store_->lookup(page_id, lookup_scratch_);
		if (found)
		{
			const auto decode = [&]() {
				decode_page(lookup_scratch_.data(), *selected);
				if (selected->page_id != page_id || selected->state_id != state.value || selected->seat != seat || selected->page_index != page_index || selected->bucket_count != bucket_count || selected->action_count != action_count)
					throw std::invalid_argument("external compact page identity disagrees with requested row");
			};
			if (profile != nullptr)
			{
				OptionalExternalTimer timer(&profile->decode_nanoseconds);
				decode();
				++profile->decoded_pages;
			}
			else
				decode();
		}
		else
		{
			const auto zero = [&]() {
				std::fill(selected->regrets.begin(), selected->regrets.end(), 0);
				std::fill(selected->strategy_masses.begin(), selected->strategy_masses.end(), 0U);
				selected->page_id = page_id;
				selected->state_id = state.value;
				selected->seat = seat;
				selected->page_index = page_index;
				selected->bucket_count = bucket_count;
				selected->action_count = action_count;
			};
			if (profile != nullptr)
			{
				OptionalExternalTimer timer(&profile->zero_nanoseconds);
				zero();
				++profile->zero_pages;
			}
			else
				zero();
		}
		selected->valid = true;
		cache_page(*selected, selected_slot);
		selected->dirty = false;
		selected->pin_count = 1U;
		return { page_id, selected->regrets.data(), selected->strategy_masses.data(), bucket_count,
			action_count, page_width_ };
	}

	void MultiwayExternalCompactPages::unpin(std::uint64_t page_id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto* page = find(page_id);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external compact page is not pinned");
		--page->pin_count;
	}

	void MultiwayExternalCompactPages::mark_dirty(std::uint64_t page_id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_)
			throw std::logic_error("external compact page mutation requires an open batch");
		auto* page = find(page_id);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external compact page mutation requires a pinned page");
		if (!page->dirty)
		{
			std::copy(page->regrets.begin(), page->regrets.end(), page->before_regrets.begin());
			std::copy(page->strategy_masses.begin(), page->strategy_masses.end(), page->before_strategy_masses.begin());
			page->dirty = true;
			++dirty_count_;
		}
	}

	bool MultiwayExternalCompactPages::batch_full() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return dirty_count_ == pages_.size();
	}

	void MultiwayExternalCompactPages::commit_batch()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_)
			throw std::logic_error("external compact page batch is not open");
		auto dirty = collect_dirty_pages();
		if (dirty.empty())
		{
			batch_open_ = false;
			return;
		}
		const auto prior_generation = store_->generation();
		try
		{
			std::vector<MultiwayExternalRecord> records;
			records.reserve(dirty.size());
			encode_dirty_pages(dirty, records);
			store_->append_batch(batch_number_, merge_fingerprint_, records);
		}
		catch (...)
		{
			if (store_->generation() > prior_generation)
				clear_dirty_pages(dirty);
			else
				rollback_dirty_pages(dirty);
			batch_open_ = false;
			dirty_count_ = 0U;
			throw;
		}
		clear_dirty_pages(dirty);
		dirty_count_ = 0U;
		batch_open_ = false;
	}

	std::vector<MultiwayExternalCompactPages::Page*>
	MultiwayExternalCompactPages::collect_dirty_pages()
	{
		std::vector<Page*> dirty;
		dirty.reserve(pages_.size());
		for (auto& page : pages_)
		{
			if (page.pin_count != 0U)
				throw std::logic_error("external compact page batch cannot commit pinned pages");
			if (page.dirty)
				dirty.push_back(&page);
		}
		std::sort(dirty.begin(), dirty.end(), [](const Page* left, const Page* right) {
			return left->page_id < right->page_id;
		});
		return dirty;
	}

	void MultiwayExternalCompactPages::encode_dirty_pages(
		const std::vector<Page*>& dirty, std::vector<MultiwayExternalRecord>& records)
	{
		for (const auto* page : dirty)
		{
			encode_page(*page, encode_scratch_);
			records.push_back({ page->page_id, encode_scratch_ });
		}
	}

	void MultiwayExternalCompactPages::rollback_dirty_pages(
		const std::vector<Page*>& dirty) noexcept
	{
		for (auto* page : dirty)
		{
			std::copy(page->before_regrets.begin(), page->before_regrets.end(), page->regrets.begin());
			std::copy(page->before_strategy_masses.begin(), page->before_strategy_masses.end(), page->strategy_masses.begin());
			page->dirty = false;
		}
	}

	void MultiwayExternalCompactPages::clear_dirty_pages(
		const std::vector<Page*>& dirty) noexcept
	{
		for (auto* page : dirty)
			page->dirty = false;
	}

	void MultiwayExternalCompactPages::set_merge_fingerprint(std::uint64_t merge_fingerprint)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_ || merge_fingerprint == 0U)
			throw std::invalid_argument("external compact page merge fingerprint is invalid");
		merge_fingerprint_ = merge_fingerprint;
	}

	void MultiwayExternalCompactPages::abort_batch()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_)
			return;
		for (auto& page : pages_)
		{
			if (page.dirty)
			{
				std::copy(page.before_regrets.begin(), page.before_regrets.end(), page.regrets.begin());
				std::copy(page.before_strategy_masses.begin(), page.before_strategy_masses.end(), page.strategy_masses.begin());
				page.dirty = false;
			}
		}
		dirty_count_ = 0U;
		batch_open_ = false;
	}

	std::size_t MultiwayExternalCompactPages::memory_bytes() const noexcept
	{
		std::size_t result = static_cast<std::size_t>(store_cache_budget_bytes_) +
			lookup_scratch_.capacity() + encode_scratch_.capacity() +
			page_lookup_slots_.capacity() * sizeof(std::uint32_t) + pages_.capacity() * sizeof(Page);
		for (const auto& page : pages_)
			result += page.regrets.capacity() * sizeof(std::int32_t) + page.strategy_masses.capacity() * sizeof(std::uint64_t) + page.before_regrets.capacity() * sizeof(std::int32_t) + page.before_strategy_masses.capacity() * sizeof(std::uint64_t);
		return result + pages_.size() * (sizeof(Page*) + sizeof(MultiwayExternalRecord) + page_payload_size());
	}

	MultiwayExternalStorageStats MultiwayExternalCompactPages::stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto result = store_->stats();
		result.page_cache_hits = page_cache_hits_;
		result.page_cache_misses = page_cache_misses_;
		result.page_cache_evictions = page_cache_evictions_;
		return result;
	}
} // namespace texas::solver::multiway
