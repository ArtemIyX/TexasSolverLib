#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalCompactStorage::MultiwayExternalCompactStorage(
		MultiwayExternalStorageConfig row_config, MultiwayExternalStorageConfig page_config,
		std::uint32_t page_width, std::uint64_t page_cache_bytes,
		std::uint64_t maximum_page_id)
		: rows_(std::move(row_config), page_width), pages_(std::move(page_config), page_width, page_cache_bytes, maximum_page_id), next_page_id_(rows_.next_page_id()), page_width_(page_width)
	{
		if (page_width_ == 0U)
			throw std::invalid_argument("external compact storage page width is zero");
	}

	void MultiwayExternalCompactStorage::admit_rows(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwaySparseRowShape>& rows)
	{
		rows_.append_batch(batch_number, merge_fingerprint, rows, next_page_id_);
	}

	void MultiwayExternalCompactStorage::begin_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint)
	{
		if (batch_number == 0U)
			throw std::invalid_argument("external compact page batch number is zero");
		page_batch_number_ = std::max(batch_number, pages_.next_batch_number());
		page_batch_fingerprint_ = merge_fingerprint;
		pages_.begin_batch(page_batch_number_, page_batch_fingerprint_);
	}

	void MultiwayExternalCompactStorage::flush_page_batch_and_continue()
	{
		if (page_batch_fingerprint_ == 0U)
		{
			auto hash = kHashOffset;
			texas::core::fingerprint::append_u64(hash, 0x5041474553554242ULL);
			texas::core::fingerprint::append_u64(hash, page_batch_number_);
			const auto fingerprint = texas::core::fingerprint::finish(hash);
			pages_.set_merge_fingerprint(fingerprint == 0U ? 1U : fingerprint);
		}
		pages_.commit_batch();
		if (page_batch_number_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external compact page batch number overflows");
		++page_batch_number_;
		pages_.begin_batch(page_batch_number_, page_batch_fingerprint_);
	}

	void MultiwayExternalCompactStorage::flush_page_batch_if_full()
	{
		if (pages_.batch_full())
			flush_page_batch_and_continue();
	}

	bool MultiwayExternalCompactStorage::lookup_row(MultiwayInfosetId infoset,
		MultiwayExternalRowBinding& output) const
	{
		MultiwayExternalRowMetadata row{};
		if (!rows_.lookup(infoset.public_state, infoset.seat, row))
		{
			output = {};
			return false;
		}
		output.state_id_ = infoset.public_state.value;
		output.owner_ = &rows_;
		output.seat_ = infoset.seat;
		output.row_ = row;
		return true;
	}

	void MultiwayExternalCompactStorage::lookup_rows(
		const std::vector<MultiwaySparseRowShape>& rows,
		std::vector<MultiwayExternalRowMetadata>& output) const
	{
		rows_.lookup_rows(rows, output);
	}

	bool MultiwayExternalCompactStorage::visit_ordered_rows(
		MultiwayExternalRowIndex::OrderedRowVisitor visitor, void* context) const
	{
		return rows_.visit_ordered_rows(visitor, context);
	}

	MultiwayExternalCompactStorage::PinnedBucket::PinnedBucket(PinnedBucket&& other) noexcept
		: owner(other.owner), page(other.page), local_bucket(other.local_bucket), writable(other.writable)
	{
		other.owner = nullptr;
		other.page = {};
		other.local_bucket = 0U;
		other.writable = false;
	}

	MultiwayExternalCompactStorage::PinnedBucket MultiwayExternalCompactStorage::pin_bucket(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket, bool writable) const
	{
		MultiwayExternalRowPage row_page{};
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact row or bucket is unavailable");
		PinnedBucket result;
		result.owner = this;
		result.local_bucket = bucket % page_width_;
		result.writable = writable;
		result.page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		if (writable)
		{
			try
			{
				pages_.mark_dirty(row_page.page_id);
			}
			catch (...)
			{
				pages_.unpin(row_page.page_id);
				throw;
			}
		}
		return result;
	}

	MultiwayExternalCompactStorage::PinnedBucket MultiwayExternalCompactStorage::pin_bucket_profiled(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket, bool writable,
		MultiwayExternalPageProfile* profile) const
	{
		if (profile == nullptr)
			return pin_bucket(binding, bucket, writable);
		MultiwayExternalRowPage row_page{};
		bool located = false;
		if (profile != nullptr)
		{
			OptionalExternalTimer timer(&profile->locate_nanoseconds);
			located = rows_.locate_bucket(binding, bucket, row_page);
		}
		else
			located = rows_.locate_bucket(binding, bucket, row_page);
		if (!located)
			throw std::out_of_range("external compact row or bucket is unavailable");
		PinnedBucket result;
		result.owner = this;
		result.local_bucket = bucket % page_width_;
		result.writable = writable;
		result.page = pages_.pin_profiled(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count, profile);
		if (writable)
		{
			try
			{
				pages_.mark_dirty(row_page.page_id);
			}
			catch (...)
			{
				pages_.unpin(row_page.page_id);
				throw;
			}
		}
		return result;
	}

	void MultiwayExternalCompactStorage::apply_delta(PinnedBucket& bucket,
		std::uint8_t action, double regret, double strategy_sum)
	{
		if (bucket.owner != this || !bucket.writable || bucket.page.page_id == 0U || bucket.local_bucket >= bucket.page.bucket_count || action >= bucket.page.action_count)
			throw std::invalid_argument("external compact pinned bucket is invalid");
		const auto cell = static_cast<std::size_t>(action) * bucket.page.action_stride + bucket.local_bucket;
		MultiwayCompactStorage::apply_delta_to_cells(
			bucket.page.regrets[cell], bucket.page.strategy_masses[cell], regret, strategy_sum);
	}

	void MultiwayExternalCompactStorage::regret_matched_strategy_into(
		const PinnedBucket& bucket, double* output, std::size_t output_size) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || output == nullptr || output_size != bucket.page.action_count || bucket.local_bucket >= bucket.page.bucket_count)
			throw std::invalid_argument("external compact pinned bucket policy output is invalid");
		Probability total = 0.0;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * bucket.page.action_stride + bucket.local_bucket;
			output[action] = std::max(0.0, static_cast<double>(bucket.page.regrets[cell]) / 1024.0);
			total += output[action];
		}
		if (total == 0.0)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] /= total;
	}

	void MultiwayExternalCompactStorage::average_strategy_into(
		const PinnedBucket& bucket, double* output, std::size_t output_size) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || output == nullptr || output_size != bucket.page.action_count || bucket.local_bucket >= bucket.page.bucket_count)
			throw std::invalid_argument("external compact pinned bucket policy output is invalid");
		std::uint64_t total = 0U;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * bucket.page.action_stride + bucket.local_bucket;
			output[action] = static_cast<double>(bucket.page.strategy_masses[cell]) / static_cast<double>(MultiwayCompactStorage::kMassScale);
			total += bucket.page.strategy_masses[cell];
		}
		if (total == 0U)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] /= static_cast<double>(total) / static_cast<double>(MultiwayCompactStorage::kMassScale);
	}

	bool MultiwayExternalCompactStorage::action_below_regret(const PinnedBucket& bucket,
		std::uint8_t action, double threshold) const noexcept
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || !std::isfinite(threshold) || bucket.local_bucket >= bucket.page.bucket_count || action >= bucket.page.action_count)
			return false;
		const auto cell = static_cast<std::size_t>(action) * bucket.page.action_stride + bucket.local_bucket;
		return static_cast<double>(bucket.page.regrets[cell]) / 1024.0 < threshold;
	}

	void MultiwayExternalCompactStorage::unpin_bucket(PinnedBucket& bucket) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U)
			throw std::invalid_argument("external compact pinned bucket is invalid");
		pages_.unpin(bucket.page.page_id);
		bucket.owner = nullptr;
		bucket.page = {};
		bucket.local_bucket = 0U;
		bucket.writable = false;
	}

	void MultiwayExternalCompactStorage::apply_delta(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, std::uint8_t action, double regret, double strategy_sum)
	{
		auto pinned = pin_bucket(binding, bucket, true);
		try
		{
			apply_delta(pinned, action, regret, strategy_sum);
		}
		catch (...)
		{
			unpin_bucket(pinned);
			throw;
		}
		unpin_bucket(pinned);
	}

	void MultiwayExternalCompactStorage::scale_regrets(double factor)
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
			throw std::invalid_argument("external compact regret discount must be finite and in (0, 1]");
		(void)transform_external_regrets(pages_, next_page_id_, [factor](std::int32_t& regret) {
			const auto before = regret;
			regret = static_cast<std::int32_t>(std::clamp<long long>(
				std::llround(static_cast<double>(regret) * factor),
				MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax));
			return regret != before; }, [this] { flush_page_batch_and_continue(); });
	}

	std::size_t MultiwayExternalCompactStorage::scale_and_prune_regrets(
		double factor, double threshold, double regret_floor)
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
			return 0U;
		if (threshold > 0.0)
			throw std::invalid_argument("external compact positive regret thresholds require dense page enumeration");
		const auto floor_value = std::clamp<long long>(std::llround(regret_floor * 1024.0),
			MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax);
		const auto threshold_value = std::clamp<long long>(std::llround(threshold * 1024.0),
			MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax);
		return transform_external_regrets(pages_, next_page_id_, [=](std::int32_t& regret) {
			regret = static_cast<std::int32_t>(std::clamp<long long>(
				std::llround(static_cast<double>(regret) * factor),
				MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax));
			if (regret < threshold_value)
			{
				regret = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
				return true;
			}
			return false; }, [this] { flush_page_batch_and_continue(); });
	}

	std::size_t MultiwayExternalCompactStorage::prune_negative_regrets(
		double threshold, double regret_floor)
	{
		if (threshold > 0.0)
			throw std::invalid_argument("external compact positive regret thresholds require dense page enumeration");
		const auto floor_value = std::clamp<long long>(std::llround(regret_floor * 1024.0),
			MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax);
		const auto threshold_value = std::clamp<long long>(std::llround(threshold * 1024.0),
			MultiwayCompactStorage::kRegretMin, MultiwayCompactStorage::kRegretMax);
		return transform_external_regrets(pages_, next_page_id_, [=](std::int32_t& regret) {
			if (regret < threshold_value)
			{
				regret = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
				return true;
			}
			return false; }, [this] { flush_page_batch_and_continue(); });
	}

	void MultiwayExternalCompactStorage::regret_matched_strategy_into(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket,
		double* output, std::size_t output_size) const
	{
		MultiwayExternalRowPage row_page{};
		if (binding.owner_ != &rows_)
			throw std::invalid_argument("external compact row binding belongs to another storage object");
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact regret strategy row or bucket is unavailable");
		if (output == nullptr || output_size != row_page.action_count)
			throw std::invalid_argument("external compact regret strategy output size mismatch");
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		Probability total = 0.0;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * page.action_stride + bucket % page_width_;
			output[action] = std::max(0.0, static_cast<double>(page.regrets[cell]) / 1024.0);
			total += output[action];
		}
		if (total == 0.0)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] /= total;
		pages_.unpin(row_page.page_id);
	}

	void MultiwayExternalCompactStorage::average_strategy_into(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket,
		double* output, std::size_t output_size) const
	{
		MultiwayExternalRowPage row_page{};
		if (binding.owner_ != &rows_)
			throw std::invalid_argument("external compact row binding belongs to another storage object");
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact average strategy row or bucket is unavailable");
		if (output == nullptr || output_size != row_page.action_count)
			throw std::invalid_argument("external compact average strategy output size mismatch");
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		std::uint64_t total = 0U;
		const auto local_bucket = bucket % page_width_;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * page.action_stride + local_bucket;
			output[action] = static_cast<double>(page.strategy_masses[cell]) / static_cast<double>(MultiwayCompactStorage::kMassScale);
			total += page.strategy_masses[cell];
		}
		if (total == 0U)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] /= static_cast<double>(total) / static_cast<double>(MultiwayCompactStorage::kMassScale);
		pages_.unpin(row_page.page_id);
	}

	bool MultiwayExternalCompactStorage::action_below_regret(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, std::uint8_t action, double threshold) const
	{
		MultiwayExternalRowPage row_page{};
		if (!std::isfinite(threshold) || !rows_.locate_bucket(binding, bucket, row_page) || action >= row_page.action_count)
			return false;
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		const auto cell = static_cast<std::size_t>(action) * page.action_stride + bucket % page_width_;
		const auto below = static_cast<double>(page.regrets[cell]) / 1024.0 < threshold;
		pages_.unpin(row_page.page_id);
		return below;
	}

	void MultiwayExternalCompactStorage::commit_batch()
	{
		pages_.commit_batch();
	}

	void MultiwayExternalCompactStorage::set_merge_fingerprint(std::uint64_t merge_fingerprint)
	{
		pages_.set_merge_fingerprint(merge_fingerprint);
	}

	void MultiwayExternalCompactStorage::abort_batch()
	{
		pages_.abort_batch();
	}

	MultiwayExternalStorageStats MultiwayExternalCompactStorage::stats() const
	{
		auto result = rows_.stats();
		add_storage_stats(result, pages_.stats());
		return result;
	}

	MultiwayExternalDiskUsage MultiwayExternalCompactStorage::disk_usage() const
	{
		const auto rows = rows_.disk_usage();
		const auto pages = pages_.disk_usage();
		return {
			checked_add(rows.live_segment_bytes, pages.live_segment_bytes),
			checked_add(rows.retained_segment_bytes, pages.retained_segment_bytes)
		};
	}

	std::uint64_t MultiwayExternalCompactStorage::content_hash() const
	{
		auto hash = kHashOffset;
		texas::core::fingerprint::append_u64(hash, rows_.content_hash());
		texas::core::fingerprint::append_u64(hash, pages_.content_hash());
		return texas::core::fingerprint::finish(hash);
	}
} // namespace texas::solver::multiway
