#pragma once

/** @file
 * @brief Sparse sampled information-set regret and strategy storage.
 */

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/hunl/flat/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Maximum bucket count accepted by sampled storage. */
	inline constexpr std::uint32_t HUNL_SAMPLED_MAX_BUCKET_COUNT = 1'000'000U;
	/** @brief Maximum action count representable by the sampled HUNL menu. */
	inline constexpr std::uint8_t HUNL_SAMPLED_MAX_ACTION_COUNT =
		static_cast<std::uint8_t>(ACTION_ALL_IN + 1U);

	/** @brief Dimensions and owner for one sampled information-set row. */
	struct HUNLSampledInfosetShape
	{
		InfosetId id{};
		PlayerId player = -1;
		Street street = Street::Preflop;
		std::uint32_t bucket_count = 0;
		std::uint8_t action_count = 0;
	};

	/** @brief Row offsets and discount metadata for allocated sampled storage. */
	struct HUNLSampledInfosetMeta
	{
		std::size_t regret_offset = 0;
		std::size_t strategy_sum_offset = 0;
		InfosetId id{};
		PlayerId player = -1;
		std::uint32_t bucket_count = 0;
		std::uint32_t last_discount_iter = 0;
		std::uint8_t action_count = 0;
		Street street = Street::Preflop;

		/** @brief Returns bucket_count times action_count with saturation on overflow. */
		[[nodiscard]] std::size_t value_count() const noexcept
		{
			const auto actions = static_cast<std::size_t>(action_count);
			if (actions != 0 && static_cast<std::size_t>(bucket_count) > std::numeric_limits<std::size_t>::max() / actions)
			{
				return std::numeric_limits<std::size_t>::max();
			}
			return static_cast<std::size_t>(bucket_count) * actions;
		}
	};

	/** @brief Non-owning read-only view of a sampled regret and strategy-sum row. */
	struct HUNLSampledConstRowView
	{
		const float* regret = nullptr;
		const float* strategy_sum = nullptr;
		std::uint32_t bucket_count = 0;
		std::uint8_t action_count = 0;
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;

		/** @brief Returns the number of action-bucket values in this view. */
		[[nodiscard]] std::size_t value_count() const noexcept
		{
			return static_cast<std::size_t>(bucket_count) * static_cast<std::size_t>(action_count);
		}

		/** @brief Reports whether this view has no backing values. */
		[[nodiscard]] bool empty() const noexcept
		{
			return regret == nullptr || strategy_sum == nullptr || value_count() == 0;
		}
	};

	/** @brief Non-owning mutable view of a sampled regret and strategy-sum row. */
	struct HUNLSampledRowView
	{
		float* regret = nullptr;
		float* strategy_sum = nullptr;
		std::uint32_t bucket_count = 0;
		std::uint8_t action_count = 0;
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;

		/** @brief Returns the number of action-bucket values in this view. */
		[[nodiscard]] std::size_t value_count() const noexcept
		{
			return static_cast<std::size_t>(bucket_count) * static_cast<std::size_t>(action_count);
		}

		/** @brief Reports whether this view has no backing values. */
		[[nodiscard]] bool empty() const noexcept
		{
			return regret == nullptr || strategy_sum == nullptr || value_count() == 0;
		}
	};

	static_assert(
		std::is_trivially_copyable_v<HUNLSampledInfosetMeta>,
		"HUNLSampledInfosetMeta should stay trivially copyable");

	/** @brief Byte and row counts for sampled storage. */
	struct HUNLSampledStorageMemoryEstimate
	{
		std::uint64_t meta_bytes = 0;
		std::uint64_t lookup_bytes = 0;
		std::uint64_t regret_bytes = 0;
		std::uint64_t strategy_sum_bytes = 0;
		std::uint64_t sparse_rows = 0;
		std::uint64_t sparse_values = 0;

		/** @brief Sums byte components with saturation on overflow. */
		[[nodiscard]] std::uint64_t total_bytes() const noexcept
		{
			std::uint64_t total = 0;
			for (const auto value : { meta_bytes, lookup_bytes, regret_bytes, strategy_sum_bytes })
			{
				if (value > std::numeric_limits<std::uint64_t>::max() - total)
				{
					return std::numeric_limits<std::uint64_t>::max();
				}
				total += value;
			}
			return total;
		}
	};

	/** @brief Sparse or dense rows of sampled regrets and strategy sums. */
	class HUNLSampledStorage
	{
	public:
		/** @brief Creates storage with the requested layout and numeric precision. */
		explicit HUNLSampledStorage(
			HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
			HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32);

		// Returned raw-pointer views remain valid until the next operation that
		// grows storage; reacquire the view after adding a new row.
		/** @brief Returns a mutable row view, allocating and initializing the row if needed. */
		HUNLSampledRowView ensure_row(const HUNLSampledInfosetShape& shape);
		/** @brief Reports whether an information-set row exists. */
		[[nodiscard]] bool has_row(InfosetId id) const noexcept;
		/** @brief Returns a read-only row view, empty when the row is absent. */
		[[nodiscard]] HUNLSampledConstRowView view(InfosetId id) const;
		/** @brief Returns a mutable row view, empty when the row is absent. */
		[[nodiscard]] HUNLSampledRowView view_mut(InfosetId id);
		/** @brief Returns metadata for allocated rows. */
		[[nodiscard]] const std::vector<HUNLSampledInfosetMeta>& meta() const noexcept;
		/** @brief Returns mutable metadata for an allocated row, or null when absent. */
		[[nodiscard]] HUNLSampledInfosetMeta* meta_for_mut(InfosetId id) noexcept;
		/** @brief Returns read-only metadata for an allocated row, or null when absent. */
		[[nodiscard]] const HUNLSampledInfosetMeta* meta_for(InfosetId id) const noexcept;
		/** @brief Returns number of allocated rows. */
		[[nodiscard]] std::size_t row_count() const noexcept;
		/** @brief Returns total allocated action-bucket values. */
		[[nodiscard]] std::size_t total_value_count() const noexcept;
		/** @brief Returns bytes currently owned by row storage and metadata. */
		[[nodiscard]] std::uint64_t storage_bytes() const noexcept;
		/** @brief Returns a breakdown of current storage memory. */
		[[nodiscard]] HUNLSampledStorageMemoryEstimate memory_estimate() const noexcept;
		/** @brief Returns the configured row memory layout. */
		[[nodiscard]] HUNLFlatValueLayout layout() const noexcept;
		/** @brief Returns the configured storage precision. */
		[[nodiscard]] HUNLFlatStoragePrecision precision() const noexcept;
		/** @brief Computes an offset for one bucket and action in the selected layout. */
		[[nodiscard]] static std::size_t value_index(
			HUNLFlatValueLayout layout,
			std::uint32_t bucket_count,
			std::uint8_t action_count,
			std::size_t bucket,
			std::size_t action) noexcept;
		/** @brief Computes the regret-matched strategy for one bucket. */
		static void compute_current_strategy(
			HUNLSampledConstRowView row,
			std::size_t bucket,
			float* out) noexcept;
		/** @brief Estimates bytes needed for one row of the supplied shape. */
		[[nodiscard]] static std::uint64_t estimate_row_storage_bytes(
			const HUNLSampledInfosetShape& shape) noexcept;
		/** @brief Sets the maximum storage growth limit; zero disables the limit. */
		void set_memory_limit_bytes(std::uint64_t limit) noexcept;
		/** @brief Clears active rows while retaining allocated vector capacity. */
		void clear_keep_capacity() noexcept;

	private:
		[[nodiscard]] std::size_t row_index(InfosetId id) const;

		HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetActionHand;
		HUNLFlatStoragePrecision precision_ = HUNLFlatStoragePrecision::Float32;
		std::vector<HUNLSampledInfosetMeta> meta_;
		std::unordered_map<InfosetId, std::size_t> row_lookup_;
		std::vector<float> regret_;
		std::vector<float> strategy_sum_;
		std::uint64_t memory_limit_bytes_ = 0;
	};

} // namespace texas::solver::hunl
