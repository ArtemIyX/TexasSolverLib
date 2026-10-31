#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/detail/multiway_compact_pruning.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway
{
	std::size_t MultiwayCompactStorage::page_metadata_size() noexcept
	{
		return sizeof(PageMetadata);
	}

	namespace
	{
		// Checkpoint rows are public-state-major, then seat-major. Keep the
		// compact indexes in this order so export does not need a sort or copy.
		bool checkpoint_infoset_less(const MultiwayInfosetId& left, const MultiwayInfosetId& right) noexcept
		{
			return left.public_state.value != right.public_state.value
				? left.public_state.value < right.public_state.value
				: left.seat < right.seat;
		}

		long long saturating_scaled(double value, double scale, long long minimum, long long maximum) noexcept
		{
			const auto scaled = static_cast<long double>(value) * static_cast<long double>(scale);
			if (scaled <= static_cast<long double>(minimum))
				return minimum;
			if (scaled >= static_cast<long double>(maximum))
				return maximum;
			return std::llround(scaled);
		}
	} // namespace

	MultiwayCompactStorage::MultiwayCompactStorage(std::size_t max_rows, std::size_t max_values, std::uint32_t page_width)
		: max_rows_(max_rows), max_values_(max_values), page_width_(page_width)
	{
		if (page_width_ == 0U)
			throw std::invalid_argument("compact storage page width must be positive");
		rows_.reserve(max_rows_);
		// Value arrays grow only when a page is admitted. Reserving the maximum
		// here would defeat the sparse storage contract.
	}

	const MultiwaySparseRowMetadata* MultiwayCompactStorage::metadata(MultiwayInfosetId infoset) const noexcept
	{
		const auto it = std::lower_bound(rows_.begin(), rows_.end(), infoset,
			[](const auto& row, MultiwayInfosetId key) { return checkpoint_infoset_less(row.shape.infoset, key); });
		return it == rows_.end() || !(it->shape.infoset == infoset) ? nullptr : &*it;
	}

	bool MultiwayCompactStorage::has_row(MultiwayInfosetId infoset) const noexcept
	{
		return metadata(infoset) != nullptr;
	}

	const MultiwayCompactStorage::PageMetadata* MultiwayCompactStorage::page(MultiwayInfosetId infoset, std::uint32_t page_index) const noexcept
	{
		const auto it = std::lower_bound(pages_.begin(), pages_.end(), std::pair<MultiwayInfosetId, std::uint32_t>{ infoset, page_index },
			[](const PageMetadata& candidate, const auto& key) {
				return checkpoint_infoset_less(candidate.infoset, key.first) || (candidate.infoset == key.first && candidate.page_index < key.second);
			});
		return it == pages_.end() || it->infoset != infoset || it->page_index != page_index ? nullptr : &*it;
	}

	MultiwayCompactStorage::PageMetadata* MultiwayCompactStorage::page(MultiwayInfosetId infoset, std::uint32_t page_index) noexcept
	{
		const auto it = std::lower_bound(pages_.begin(), pages_.end(), std::pair<MultiwayInfosetId, std::uint32_t>{ infoset, page_index },
			[](const PageMetadata& candidate, const auto& key) {
				return checkpoint_infoset_less(candidate.infoset, key.first) || (candidate.infoset == key.first && candidate.page_index < key.second);
			});
		return it == pages_.end() || it->infoset != infoset || it->page_index != page_index ? nullptr : &*it;
	}

	const MultiwayCompactStorage::PageMetadata* MultiwayCompactStorage::page_for_bucket(const MultiwaySparseRowMetadata& row, std::uint32_t bucket) const noexcept
	{
		return page(row.shape.infoset, bucket / page_width_);
	}

	std::size_t MultiwayCompactStorage::page_cell(const PageMetadata& page, std::uint32_t bucket, std::uint8_t action, std::uint32_t page_width) noexcept
	{
		const auto local_bucket = bucket - page.page_index * page_width;
		return page.regret_offset + static_cast<std::size_t>(action) * page.bucket_count + local_bucket;
	}

	bool MultiwayCompactStorage::has_page(MultiwayInfosetId infoset, std::uint32_t page_index) const noexcept
	{
		return page(infoset, page_index) != nullptr;
	}

	void MultiwayCompactStorage::admit_row(const MultiwaySparseRowShape& shape)
	{
		if (shape.infoset.public_state.value == 0U || shape.infoset.seat < 0 || shape.bucket_count == 0U || shape.action_count == 0U)
			throw std::invalid_argument("compact storage row shape is empty or invalid");
		if (const auto* existing = metadata(shape.infoset))
		{
			if (existing->shape.bucket_count != shape.bucket_count || existing->shape.action_count != shape.action_count)
				throw std::invalid_argument("compact storage row shape conflicts");
			return;
		}
		if (rows_.size() >= max_rows_)
			throw std::length_error("compact storage row capacity exceeded");
		const auto first_page_cells = static_cast<std::size_t>(shape.action_count) * std::min<std::uint32_t>(shape.bucket_count, page_width_);
		if (first_page_cells > max_values_ - regrets_.size())
			throw std::length_error("compact storage page capacity exceeded");
		const MultiwaySparseRowMetadata row{ shape, 0U, 0U };
		const auto it = std::lower_bound(rows_.begin(), rows_.end(), shape.infoset,
			[](const auto& existing, MultiwayInfosetId key) { return checkpoint_infoset_less(existing.shape.infoset, key); });
		rows_.insert(it, row);
		admit_page(shape.infoset, 0U);
	}

	void MultiwayCompactStorage::admit_rows(
		const std::vector<MultiwaySparseRowShape>& shapes,
		const std::vector<std::uint8_t>& new_row_flags)
	{
		if (shapes.size() != new_row_flags.size())
			throw std::invalid_argument("compact storage row batch flags do not match shapes");
		std::size_t new_count = 0U;
		std::size_t new_values = 0U;
		for (std::size_t index = 0U; index < shapes.size(); ++index)
		{
			if (new_row_flags[index] == 0U)
				continue;
			const auto& shape = shapes[index];
			if (shape.infoset.public_state.value == 0U || shape.infoset.seat < 0 || shape.bucket_count == 0U || shape.action_count == 0U)
				throw std::invalid_argument("compact storage row shape is empty or invalid");
			const auto values = static_cast<std::size_t>(shape.action_count) * std::min<std::uint32_t>(shape.bucket_count, page_width_);
			if (new_values > std::numeric_limits<std::size_t>::max() - values)
				throw std::overflow_error("compact storage row batch value count overflows size_t");
			new_values += values;
			++new_count;
		}
		if (rows_.size() > max_rows_ || new_count > max_rows_ - rows_.size())
			throw std::length_error("compact storage row capacity exceeded");
		if (regrets_.size() > max_values_ || new_values > max_values_ - regrets_.size())
			throw std::length_error("compact storage page capacity exceeded");
		if (new_count == 0U)
			return;

		std::vector<MultiwaySparseRowMetadata> pending_rows;
		std::vector<PageMetadata> pending_pages;
		pending_rows.reserve(new_count);
		pending_pages.reserve(new_count);
		std::size_t value_offset = regrets_.size();
		std::size_t strategy_offset = strategy_mass_.size();
		for (std::size_t index = 0U; index < shapes.size(); ++index)
		{
			if (new_row_flags[index] == 0U)
				continue;
			const auto& shape = shapes[index];
			const auto page_buckets = std::min<std::size_t>(page_width_, shape.bucket_count);
			const auto values = static_cast<std::size_t>(shape.action_count) * page_buckets;
			pending_rows.push_back({ shape, 0U, 0U });
			pending_pages.push_back({ shape.infoset, 0U, static_cast<std::uint32_t>(page_buckets), value_offset, strategy_offset });
			value_offset += values;
			strategy_offset += values;
		}
		std::sort(pending_rows.begin(), pending_rows.end(), [](const auto& left, const auto& right) {
			return checkpoint_infoset_less(left.shape.infoset, right.shape.infoset);
		});
		std::sort(pending_pages.begin(), pending_pages.end(), [](const auto& left, const auto& right) {
			return checkpoint_infoset_less(left.infoset, right.infoset) || (left.infoset == right.infoset && left.page_index < right.page_index);
		});
		for (std::size_t index = 1U; index < pending_rows.size(); ++index)
			if (pending_rows[index - 1U].shape.infoset == pending_rows[index].shape.infoset)
				throw std::invalid_argument("compact storage row batch shapes must be unique");

		rows_.reserve(rows_.size() + pending_rows.size());
		pages_.reserve(pages_.size() + pending_pages.size());
		regrets_.reserve(regrets_.size() + new_values);
		strategy_mass_.reserve(strategy_mass_.size() + new_values);
		std::vector<MultiwaySparseRowMetadata> merged_rows;
		merged_rows.reserve(rows_.size() + pending_rows.size());
		std::size_t existing_row = 0U;
		std::size_t pending_row = 0U;
		while (existing_row < rows_.size() || pending_row < pending_rows.size())
		{
			if (existing_row < rows_.size() && pending_row < pending_rows.size() && rows_[existing_row].shape.infoset == pending_rows[pending_row].shape.infoset)
				throw std::invalid_argument("compact storage row batch contains an admitted row");
			if (pending_row == pending_rows.size() || (existing_row < rows_.size() && checkpoint_infoset_less(rows_[existing_row].shape.infoset, pending_rows[pending_row].shape.infoset)))
				merged_rows.push_back(rows_[existing_row++]);
			else
				merged_rows.push_back(pending_rows[pending_row++]);
		}
		std::vector<PageMetadata> merged_pages;
		merged_pages.reserve(pages_.size() + pending_pages.size());
		std::size_t existing_page = 0U;
		std::size_t pending_page = 0U;
		while (existing_page < pages_.size() || pending_page < pending_pages.size())
		{
			if (existing_page < pages_.size() && pending_page < pending_pages.size() && pages_[existing_page].infoset == pending_pages[pending_page].infoset && pages_[existing_page].page_index == pending_pages[pending_page].page_index)
				throw std::invalid_argument("compact storage page batch contains an admitted page");
			if (pending_page == pending_pages.size() || (existing_page < pages_.size() && (checkpoint_infoset_less(pages_[existing_page].infoset, pending_pages[pending_page].infoset) || (pages_[existing_page].infoset == pending_pages[pending_page].infoset && pages_[existing_page].page_index < pending_pages[pending_page].page_index))))
				merged_pages.push_back(pages_[existing_page++]);
			else
				merged_pages.push_back(pending_pages[pending_page++]);
		}

		const auto old_regret_size = regrets_.size();
		const auto old_strategy_size = strategy_mass_.size();
		try
		{
			regrets_.resize(old_regret_size + new_values, 0);
			strategy_mass_.resize(old_strategy_size + new_values, 0);
			rows_.swap(merged_rows);
			pages_.swap(merged_pages);
		}
		catch (...)
		{
			strategy_mass_.resize(old_strategy_size);
			regrets_.resize(old_regret_size);
			throw;
		}
	}

	void MultiwayCompactStorage::admit_page(MultiwayInfosetId infoset, std::uint32_t page_index)
	{
		const auto* row = metadata(infoset);
		if (row == nullptr)
			throw std::invalid_argument("compact storage page requires an admitted row");
		const auto first_bucket = static_cast<std::size_t>(page_index) * page_width_;
		if (first_bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage page index");
		if (page(infoset, page_index) != nullptr)
			return;
		const auto page_buckets = std::min<std::size_t>(page_width_, row->shape.bucket_count - first_bucket);
		const auto values = page_buckets * row->shape.action_count;
		if (values > max_values_ - regrets_.size())
			throw std::length_error("compact storage page capacity exceeded");
		PageMetadata metadata{ infoset, page_index, static_cast<std::uint32_t>(page_buckets), regrets_.size(), strategy_mass_.size() };
		const auto insertion = std::lower_bound(pages_.begin(), pages_.end(), std::pair<MultiwayInfosetId, std::uint32_t>{ infoset, page_index },
			[](const PageMetadata& candidate, const auto& key) {
				return checkpoint_infoset_less(candidate.infoset, key.first) || (candidate.infoset == key.first && candidate.page_index < key.second);
			});
		regrets_.resize(regrets_.size() + values, 0);
		strategy_mass_.resize(strategy_mass_.size() + values, 0);
		pages_.insert(insertion, metadata);
	}

	void MultiwayCompactStorage::apply_delta(MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action, double regret, double strategy_sum)
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count || !std::isfinite(regret) || !std::isfinite(strategy_sum) || strategy_sum < 0.0)
			throw std::invalid_argument("invalid compact storage delta");
		apply_delta(*row, bucket, action, regret, strategy_sum);
	}

	void MultiwayCompactStorage::apply_delta(const MultiwaySparseRowMetadata& row, std::uint32_t bucket, std::uint8_t action, double regret, double strategy_sum)
	{
		if (bucket >= row.shape.bucket_count || action >= row.shape.action_count || !std::isfinite(regret) || !std::isfinite(strategy_sum) || strategy_sum < 0.0)
			throw std::invalid_argument("invalid compact storage delta");
		if (page_for_bucket(row, bucket) == nullptr)
			admit_page(row.shape.infoset, bucket / page_width_);
		const auto* active_page = page_for_bucket(row, bucket);
		const auto index = page_cell(*active_page, bucket, action, page_width_);
		auto& regret_cell = regrets_[index];
		auto& strategy_mass_cell = strategy_mass_[active_page->strategy_sum_offset + index - active_page->regret_offset];
		apply_delta_to_cells(regret_cell, strategy_mass_cell, regret, strategy_sum);
	}

	void MultiwayCompactStorage::apply_delta_to_cells(std::int32_t& regret_cell,
		std::uint64_t& strategy_mass_cell, double regret, double strategy_sum)
	{
		if (!std::isfinite(regret) || !std::isfinite(strategy_sum) || strategy_sum < 0.0)
			throw std::invalid_argument("invalid compact cell delta");
		const auto regret_delta = saturating_scaled(regret, 1024.0,
			std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max());
		const auto old_value = static_cast<long long>(regret_cell);
		const auto summed_regret = regret_delta > 0 && old_value > std::numeric_limits<long long>::max() - regret_delta
			? std::numeric_limits<long long>::max()
			: regret_delta < 0 && old_value < std::numeric_limits<long long>::min() - regret_delta
			? std::numeric_limits<long long>::min()
			: old_value + regret_delta;
		regret_cell = static_cast<std::int32_t>(std::clamp<long long>(summed_regret, kRegretMin, kRegretMax));
		const auto mass_delta = saturating_scaled(strategy_sum, static_cast<double>(kMassScale),
			0, std::numeric_limits<long long>::max());
		const auto positive_delta = static_cast<std::uint64_t>(std::max(0LL, mass_delta));
		strategy_mass_cell = positive_delta > std::numeric_limits<std::uint64_t>::max() - strategy_mass_cell
			? std::numeric_limits<std::uint64_t>::max()
			: strategy_mass_cell + positive_delta;
	}

	void MultiwayCompactStorage::scale_regrets(double factor)
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
			throw std::invalid_argument("compact regret discount must be finite and in (0, 1]");
		for (auto& regret : regrets_)
			regret = static_cast<std::int32_t>(std::clamp<long long>(std::llround(static_cast<double>(regret) * factor), kRegretMin, kRegretMax));
	}

	std::size_t MultiwayCompactStorage::scale_and_prune_regrets(double factor, double threshold, double regret_floor) noexcept
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
			return 0U;
		const auto floor_value = std::clamp<long long>(std::llround(regret_floor * 1024.0), kRegretMin, kRegretMax);
		const auto threshold_value = std::clamp<long long>(std::llround(threshold * 1024.0), kRegretMin, kRegretMax);
		std::size_t count = 0U;
		for (auto& regret : regrets_)
		{
			regret = static_cast<std::int32_t>(std::clamp<long long>(std::llround(static_cast<double>(regret) * factor), kRegretMin, kRegretMax));
			if (regret < threshold_value)
			{
				regret = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
				++count;
			}
		}
		return count;
	}

	std::size_t MultiwayCompactStorage::prune_negative_regrets(double threshold, double regret_floor) noexcept
	{
		const auto floor_value = std::clamp<long long>(std::llround(regret_floor * 1024.0), kRegretMin, kRegretMax);
		const auto threshold_value = std::clamp<long long>(std::llround(threshold * 1024.0), kRegretMin, kRegretMax);
		const auto replacement = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
		return detail::prune_compact_regrets_selected(
			regrets_.data(), regrets_.size(), static_cast<std::int32_t>(threshold_value), replacement);
	}

	std::vector<Probability> MultiwayCompactStorage::regret_matched_strategy(MultiwayInfosetId infoset, std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage lookup");
		std::vector<Probability> result(row->shape.action_count);
		regret_matched_strategy_into(infoset, bucket, result.data(), result.size());
		return result;
	}

	void MultiwayCompactStorage::regret_matched_strategy_into(MultiwayInfosetId infoset, std::uint32_t bucket, Probability* output, std::size_t output_size) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage lookup");
		if (output == nullptr || output_size != row->shape.action_count)
			throw std::invalid_argument("compact policy output size mismatch");
		const auto* active_page = page_for_bucket(*row, bucket);
		Probability total = 0.0;
		for (std::uint8_t action = 0U; action < row->shape.action_count; ++action)
		{
			output[action] = active_page == nullptr ? 0.0 : std::max(0.0, static_cast<double>(regrets_[page_cell(*active_page, bucket, action, page_width_)]) / 1024.0);
			total += output[action];
		}
		if (total == 0.0)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] /= total;
	}

	std::vector<Probability> MultiwayCompactStorage::average_strategy(MultiwayInfosetId infoset, std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage lookup");
		std::vector<Probability> result(row->shape.action_count);
		average_strategy_into(infoset, bucket, result.data(), result.size());
		return result;
	}

	void MultiwayCompactStorage::average_strategy_into(
		MultiwayInfosetId infoset, std::uint32_t bucket,
		Probability* output, std::size_t output_size) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage lookup");
		if (output == nullptr || output_size != row->shape.action_count)
			throw std::invalid_argument("compact average strategy output size mismatch");
		const auto* active_page = page_for_bucket(*row, bucket);
		std::uint64_t total = 0U;
		for (std::uint8_t action = 0U; action < row->shape.action_count; ++action)
		{
			output[action] = 0.0;
			if (active_page != nullptr)
				output[action] = static_cast<double>(strategy_mass_[active_page->strategy_sum_offset + (page_cell(*active_page, bucket, action, page_width_) - active_page->regret_offset)]) / static_cast<double>(kMassScale);
			total += active_page == nullptr ? 0U : strategy_mass_[active_page->strategy_sum_offset + (page_cell(*active_page, bucket, action, page_width_) - active_page->regret_offset)];
		}
		if (total == 0U)
			std::fill(output, output + output_size, 1.0 / output_size);
		else
			for (std::size_t action = 0U; action < output_size; ++action)
			{
				auto& probability = output[action];
				probability /= static_cast<double>(total) / static_cast<double>(kMassScale);
			}
	}

	bool MultiwayCompactStorage::action_below_regret(MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action, double threshold) const noexcept
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count || !std::isfinite(threshold))
			return false;
		const auto* active_page = page_for_bucket(*row, bucket);
		return active_page != nullptr && static_cast<double>(regrets_[page_cell(*active_page, bucket, action, page_width_)]) / 1024.0 < threshold;
	}

	std::vector<double> MultiwayCompactStorage::strategy_sums(MultiwayInfosetId infoset, std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
			throw std::out_of_range("compact storage lookup");
		std::vector<double> result(row->shape.action_count, 0.0);
		const auto* active_page = page_for_bucket(*row, bucket);
		if (active_page == nullptr)
			return result;
		for (std::uint8_t action = 0U; action < row->shape.action_count; ++action)
			result[action] = static_cast<double>(strategy_mass_[active_page->strategy_sum_offset + (page_cell(*active_page, bucket, action, page_width_) - active_page->regret_offset)]) / static_cast<double>(kMassScale);
		return result;
	}

	void MultiwayCompactStorage::export_checkpoint(MultiwaySparseStorageCheckpoint& output) const
	{
		std::vector<std::size_t> row_offsets(rows_.size() + 1U, 0U);
		for (std::size_t row_index = 0U; row_index < rows_.size(); ++row_index)
		{
			const auto& shape = rows_[row_index].shape;
			const auto bucket_count = static_cast<std::size_t>(shape.bucket_count);
			const auto action_count = static_cast<std::size_t>(shape.action_count);
			if (action_count != 0U && bucket_count > std::numeric_limits<std::size_t>::max() / action_count)
				throw std::length_error("compact checkpoint row size overflows");
			const auto row_cells = bucket_count * action_count;
			if (row_cells > std::numeric_limits<std::size_t>::max() - row_offsets[row_index])
				throw std::length_error("compact checkpoint size overflows");
			row_offsets[row_index + 1U] = row_offsets[row_index] + row_cells;
		}

		output.shapes.clear();
		output.active_pages.clear();
		output.shapes.reserve(rows_.size());
		output.active_pages.reserve(pages_.size());
		output.compact_regrets.assign(regrets_.size(), 0);
		output.compact_strategy_masses.assign(strategy_mass_.size(), 0);
		output.regrets.assign(row_offsets.back(), 0.0);
		output.strategy_sums.assign(row_offsets.back(), 0.0);
		for (const auto& active_page : pages_)
			output.active_pages.push_back({ active_page.infoset, active_page.page_index });

		std::size_t page_cursor = 0U;
		std::size_t compact_offset = 0U;
		for (std::size_t row_index = 0U; row_index < rows_.size(); ++row_index)
		{
			const auto& row = rows_[row_index];
			output.shapes.push_back(row.shape);
			while (page_cursor < pages_.size() && checkpoint_infoset_less(pages_[page_cursor].infoset, row.shape.infoset))
				throw std::logic_error("compact storage page order is inconsistent");
			while (page_cursor < pages_.size() && pages_[page_cursor].infoset == row.shape.infoset)
			{
				const auto& active_page = pages_[page_cursor];
				const auto first_bucket = static_cast<std::size_t>(active_page.page_index) * page_width_;
				if (first_bucket >= row.shape.bucket_count || active_page.bucket_count > row.shape.bucket_count - first_bucket)
					throw std::logic_error("compact storage page range is inconsistent");
				for (std::uint8_t action = 0U; action < row.shape.action_count; ++action)
				{
					const auto source_offset = static_cast<std::size_t>(action) * active_page.bucket_count;
					const auto destination_offset = row_offsets[row_index]
						+ static_cast<std::size_t>(action) * row.shape.bucket_count
						+ first_bucket;
					for (std::uint32_t local_bucket = 0U; local_bucket < active_page.bucket_count; ++local_bucket)
					{
						const auto source_index = active_page.regret_offset + source_offset + local_bucket;
						const auto compact_index = compact_offset + source_offset + local_bucket;
						output.compact_regrets[compact_index] = regrets_[source_index];
						output.compact_strategy_masses[compact_index] = strategy_mass_[active_page.strategy_sum_offset + source_offset + local_bucket];
						output.regrets[destination_offset + local_bucket] = static_cast<double>(regrets_[source_index]) / 1024.0;
						output.strategy_sums[destination_offset + local_bucket] = static_cast<double>(strategy_mass_[active_page.strategy_sum_offset + source_offset + local_bucket]) / static_cast<double>(kMassScale);
					}
				}
				compact_offset += static_cast<std::size_t>(active_page.bucket_count) * row.shape.action_count;
				++page_cursor;
			}
		}
		if (page_cursor != pages_.size())
			throw std::logic_error("compact storage contains a page without a row");
	}

	void MultiwayCompactStorage::restore_checkpoint_values(
		const std::vector<std::int32_t>& regrets,
		const std::vector<std::uint64_t>& strategy_masses)
	{
		if (regrets.size() != regrets_.size() || strategy_masses.size() != strategy_mass_.size())
			throw std::invalid_argument("compact checkpoint value count does not match active storage");
		std::size_t compact_offset = 0U;
		for (const auto& active_page : pages_)
		{
			const auto* row = metadata(active_page.infoset);
			if (row == nullptr)
				throw std::invalid_argument("compact checkpoint page has no admitted row");
			const auto page_values = static_cast<std::size_t>(active_page.bucket_count) * row->shape.action_count;
			for (std::uint8_t action = 0U; action < row->shape.action_count; ++action)
			{
				const auto source_offset = static_cast<std::size_t>(action) * active_page.bucket_count;
				for (std::uint32_t local_bucket = 0U; local_bucket < active_page.bucket_count; ++local_bucket)
				{
					const auto compact_index = compact_offset + source_offset + local_bucket;
					regrets_[active_page.regret_offset + source_offset + local_bucket] = regrets[compact_index];
					strategy_mass_[active_page.strategy_sum_offset + source_offset + local_bucket] = strategy_masses[compact_index];
				}
			}
			compact_offset += page_values;
		}
	}

	std::size_t MultiwayCompactStorage::memory_bytes() const noexcept
	{
		const auto snapshot = memory_snapshot();
		return snapshot.total.retained_capacity_bytes > std::numeric_limits<std::size_t>::max()
			? std::numeric_limits<std::size_t>::max()
			: static_cast<std::size_t>(snapshot.total.retained_capacity_bytes);
	}

	MultiwayCompactStorageMemorySnapshot MultiwayCompactStorage::memory_snapshot() const noexcept
	{
		MultiwayCompactStorageMemorySnapshot result;
		result.row_metadata.add_buffer(rows_.size(), rows_.capacity(), sizeof(MultiwaySparseRowMetadata));
		result.page_metadata.add_buffer(pages_.size(), pages_.capacity(), sizeof(PageMetadata));
		result.regret_cells.add_buffer(regrets_.size(), regrets_.capacity(), sizeof(std::int32_t));
		result.strategy_cells.add_buffer(strategy_mass_.size(), strategy_mass_.capacity(), sizeof(std::uint64_t));
		result.total = result.row_metadata;
		result.total.accumulate(result.page_metadata);
		result.total.accumulate(result.regret_cells);
		result.total.accumulate(result.strategy_cells);
		result.total.grouping_only = true;
		return result;
	}

} // namespace texas::solver::multiway
