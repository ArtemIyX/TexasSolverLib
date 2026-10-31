#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalPageCache::MultiwayExternalPageCache(MultiwayExternalRecordStore& store, std::uint64_t cache_bytes)
		: store_(&store), payload_bytes_(store.payload_bytes()), lookup_scratch_(payload_bytes_, 0U)
	{
		const auto per_page = static_cast<std::uint64_t>(sizeof(Page)) + 2U * payload_bytes_;
		auto count = per_page == 0U || cache_bytes <= payload_bytes_ ? 0U : (cache_bytes - payload_bytes_) / per_page;
		const auto record_width = payload_bytes_ + 8U;
		const auto wal_pages = (store.maximum_batch_bytes() - 48U) / record_width;
		const auto segment_pages = store.segment_target_bytes() / record_width;
		count = std::min(count, std::min(wal_pages, segment_pages));
		if (count == 0U || count > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external page cache cannot hold one bounded page");
		pages_.resize(static_cast<std::size_t>(count));
		for (auto& page : pages_)
		{
			page.bytes.resize(payload_bytes_);
			page.before_batch.resize(payload_bytes_);
		}
	}

	MultiwayExternalPageCache::Page* MultiwayExternalPageCache::find(std::uint64_t key) noexcept
	{
		for (auto& page : pages_)
			if (page.valid && page.key == key)
				return &page;
		return nullptr;
	}

	MultiwayExternalPageCache::PageView MultiwayExternalPageCache::pin(std::uint64_t key)
	{
		if (auto* cached = find(key))
		{
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external page pin count overflows");
			++cached->pin_count;
			return { key, cached->bytes.data(), cached->bytes.size() };
		}
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
			throw std::length_error("external page cache has no clean unpinned page");
		const auto found = store_->lookup(key, lookup_scratch_);
		if (!found)
			std::fill(lookup_scratch_.begin(), lookup_scratch_.end(), 0U);
		std::copy(lookup_scratch_.begin(), lookup_scratch_.end(), selected->bytes.begin());
		selected->key = key;
		selected->valid = true;
		selected->dirty = false;
		selected->pin_count = 1U;
		return { key, selected->bytes.data(), selected->bytes.size() };
	}

	void MultiwayExternalPageCache::unpin(std::uint64_t key)
	{
		auto* page = find(key);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external page is not pinned");
		--page->pin_count;
	}

	void MultiwayExternalPageCache::mark_dirty(std::uint64_t key)
	{
		if (!batch_open_)
			throw std::logic_error("external page mutation requires an open batch");
		auto* page = find(key);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external page mutation requires a pinned page");
		if (!page->dirty)
		{
			std::copy(page->bytes.begin(), page->bytes.end(), page->before_batch.begin());
			page->dirty = true;
		}
	}

	void MultiwayExternalPageCache::begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint)
	{
		if (batch_open_ || batch_number == 0U)
			throw std::logic_error("external page batch state is invalid");
		for (const auto& page : pages_)
			if (page.dirty || page.pin_count != 0U)
				throw std::logic_error("external page cache is not at a clean batch boundary");
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	void MultiwayExternalPageCache::commit_batch()
	{
		if (!batch_open_)
			throw std::logic_error("external page batch is not open");
		std::vector<Page*> dirty;
		dirty.reserve(pages_.size());
		for (auto& page : pages_)
		{
			if (page.pin_count != 0U)
				throw std::logic_error("external page batch cannot commit pinned pages");
			if (page.dirty)
				dirty.push_back(&page);
		}
		if (dirty.empty())
		{
			batch_open_ = false;
			return;
		}
		std::sort(dirty.begin(), dirty.end(), [](const Page* left, const Page* right) { return left->key < right->key; });
		std::vector<MultiwayExternalRecord> records;
		records.reserve(dirty.size());
		for (const auto* page : dirty)
			records.push_back({ page->key, page->bytes });
		const auto prior_generation = store_->generation();
		try
		{
			store_->append_batch(batch_number_, merge_fingerprint_, records);
			store_->compact();
		}
		catch (...)
		{
			if (store_->generation() > prior_generation)
			{
				for (auto* page : dirty)
					page->dirty = false;
				batch_open_ = false;
			}
			else
				abort_batch();
			throw;
		}
		for (auto* page : dirty)
			page->dirty = false;
		batch_open_ = false;
	}

	void MultiwayExternalPageCache::abort_batch() noexcept
	{
		for (auto& page : pages_)
			if (page.dirty)
			{
				std::copy(page.before_batch.begin(), page.before_batch.end(), page.bytes.begin());
				page.dirty = false;
			}
		batch_open_ = false;
	}

	std::size_t MultiwayExternalPageCache::dirty_page_count() const noexcept
	{
		return static_cast<std::size_t>(std::count_if(pages_.begin(), pages_.end(), [](const Page& page) { return page.dirty; }));
	}

	std::size_t MultiwayExternalPageCache::memory_bytes() const noexcept
	{
		return pages_.capacity() * sizeof(Page) + pages_.size() * payload_bytes_ * 2U + lookup_scratch_.capacity();
	}
} // namespace texas::solver::multiway
