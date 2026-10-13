#include "solver/multiway/engine/multiway_compact_storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway
{

	namespace
	{
		long long saturating_scaled(double value, double scale, long long minimum, long long maximum) noexcept
		{
			const auto scaled = static_cast<long double>(value) * static_cast<long double>(scale);
			if (scaled <= static_cast<long double>(minimum))
				return minimum;
			if (scaled >= static_cast<long double>(maximum))
				return maximum;
			return std::llround(scaled);
		}
	}

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
			[](const auto& row, MultiwayInfosetId key) { return row.shape.infoset < key; });
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
				return candidate.infoset < key.first || (candidate.infoset == key.first && candidate.page_index < key.second);
			});
		return it == pages_.end() || it->infoset != infoset || it->page_index != page_index ? nullptr : &*it;
	}

	MultiwayCompactStorage::PageMetadata* MultiwayCompactStorage::page(MultiwayInfosetId infoset, std::uint32_t page_index) noexcept
	{
		const auto it = std::lower_bound(pages_.begin(), pages_.end(), std::pair<MultiwayInfosetId, std::uint32_t>{ infoset, page_index },
			[](const PageMetadata& candidate, const auto& key) {
				return candidate.infoset < key.first || (candidate.infoset == key.first && candidate.page_index < key.second);
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
			[](const auto& existing, MultiwayInfosetId key) { return existing.shape.infoset < key; });
		rows_.insert(it, row);
		admit_page(shape.infoset, 0U);
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
				return candidate.infoset < key.first || (candidate.infoset == key.first && candidate.page_index < key.second);
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
		const auto regret_delta = saturating_scaled(regret, 1024.0, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max());
		const auto old_value = static_cast<long long>(regrets_[index]);
		const auto summed_regret = regret_delta > 0 && old_value > std::numeric_limits<long long>::max() - regret_delta
			? std::numeric_limits<long long>::max()
			: regret_delta < 0 && old_value < std::numeric_limits<long long>::min() - regret_delta
			? std::numeric_limits<long long>::min()
			: old_value + regret_delta;
		regrets_[index] = static_cast<std::int32_t>(std::clamp<long long>(summed_regret, kRegretMin, kRegretMax));
		const auto mass_delta = saturating_scaled(strategy_sum, static_cast<double>(kMassScale), 0, std::numeric_limits<long long>::max());
		auto& stored_mass = strategy_mass_[active_page->strategy_sum_offset + index - active_page->regret_offset];
		const auto positive_delta = static_cast<std::uint64_t>(std::max(0LL, mass_delta));
		stored_mass = positive_delta > std::numeric_limits<std::uint64_t>::max() - stored_mass ? std::numeric_limits<std::uint64_t>::max() : stored_mass + positive_delta;
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
		std::size_t count = 0U;
		for (auto& regret : regrets_)
		{
			if (regret < threshold_value)
			{
				regret = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
				++count;
			}
		}
		return count;
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
		const auto* active_page = page_for_bucket(*row, bucket);
		std::uint64_t total = 0U;
		for (std::uint8_t action = 0U; action < row->shape.action_count; ++action)
		{
			if (active_page != nullptr)
				result[action] = static_cast<double>(strategy_mass_[active_page->strategy_sum_offset + (page_cell(*active_page, bucket, action, page_width_) - active_page->regret_offset)]) / static_cast<double>(kMassScale);
			total += active_page == nullptr ? 0U : strategy_mass_[active_page->strategy_sum_offset + (page_cell(*active_page, bucket, action, page_width_) - active_page->regret_offset)];
		}
		if (total == 0U)
			std::fill(result.begin(), result.end(), 1.0 / result.size());
		else
			for (auto& probability : result)
				probability /= static_cast<double>(total) / static_cast<double>(kMassScale);
		return result;
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
		output.shapes.clear();
		output.regrets.clear();
		output.strategy_sums.clear();
		output.shapes.reserve(rows_.size());
		for (const auto& row : rows_)
		{
			output.shapes.push_back(row.shape);
			const auto total = static_cast<std::size_t>(row.shape.bucket_count) * row.shape.action_count;
			output.regrets.reserve(output.regrets.size() + total);
			output.strategy_sums.reserve(output.strategy_sums.size() + total);
			for (std::uint8_t action = 0U; action < row.shape.action_count; ++action)
			{
				for (std::uint32_t bucket = 0U; bucket < row.shape.bucket_count; ++bucket)
				{
					const auto* active_page = page_for_bucket(row, bucket);
					if (active_page == nullptr)
					{
						output.regrets.push_back(0.0);
						output.strategy_sums.push_back(0.0);
						continue;
					}
					const auto index = page_cell(*active_page, bucket, action, page_width_);
					output.regrets.push_back(static_cast<double>(regrets_[index]) / 1024.0);
					output.strategy_sums.push_back(static_cast<double>(strategy_mass_[active_page->strategy_sum_offset + index - active_page->regret_offset]) / static_cast<double>(kMassScale));
				}
			}
		}
	}

	std::size_t MultiwayCompactStorage::memory_bytes() const noexcept
	{
		return rows_.capacity() * sizeof(MultiwaySparseRowMetadata) + pages_.capacity() * sizeof(PageMetadata) + regrets_.capacity() * sizeof(std::int32_t) + strategy_mass_.capacity() * sizeof(std::uint64_t);
	}

} // namespace texas::solver::multiway
