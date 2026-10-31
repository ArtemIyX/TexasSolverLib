#pragma once

/** @file
 * Bounded fixed-point regret and strategy storage for multiway solver rows.
 */

#include "solver/multiway/engine/multiway_solver.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace texas::solver::multiway
{
	struct MultiwayCompactStorageMemorySnapshot;

	// Bounded production accumulator. Regrets use signed fixed-point int32 cells;
	// strategy mass uses unsigned fixed-point uint64 cells. Rows remain lazy and
	// action-major. MultiwaySparseRowStorage is the Float64 reference backend.
	/** Lazy paged fixed-point accumulator used by compact storage backends. */
	class MultiwayCompactStorage
	{
	public:
		/** Non-owning page view used by checkpoint serialization. */
#pragma pack(push, 8)
		struct CheckpointPageView
		{
			MultiwayInfosetId infoset{};
			std::uint32_t page_index = 0U;
			std::uint32_t bucket_count = 0U;
			const std::int32_t* regrets = nullptr;
			const std::uint64_t* strategy_masses = nullptr;
		};
#pragma pack(pop)

		static constexpr std::int32_t kRegretMin = -2'000'000'000;
		static constexpr std::int32_t kRegretMax = 2'000'000'000;
		static constexpr std::uint64_t kMassScale = 1ULL << 20U;
		static constexpr std::uint32_t kDefaultPageWidth = 16U;

		/** Creates storage with row/value limits and a fixed bucket page width. */
		MultiwayCompactStorage(
			std::size_t max_rows,
			std::size_t max_values,
			std::uint32_t page_width = kDefaultPageWidth);

		/** Admits a row shape; accumulator pages remain lazy. */
		void admit_row(const MultiwaySparseRowShape& shape);
		/** Admits a batch of row shapes using parallel new-row flags. */
		void admit_rows(
			const std::vector<MultiwaySparseRowShape>& shapes,
			const std::vector<std::uint8_t>& new_row_flags);
		/** Admits one page for an already-known infoset row. */
		void admit_page(MultiwayInfosetId infoset, std::uint32_t page_index);
		[[nodiscard]] bool has_row(MultiwayInfosetId infoset) const noexcept;
		[[nodiscard]] bool has_page(MultiwayInfosetId infoset, std::uint32_t page_index) const noexcept;
		[[nodiscard]] const MultiwaySparseRowMetadata* metadata(MultiwayInfosetId infoset) const noexcept;
		/** Applies a regret and strategy delta by infoset and bucket. */
		void apply_delta(MultiwayInfosetId infoset, std::uint32_t bucket,
			std::uint8_t action, double regret, double strategy_sum);
		/** Applies a delta using pre-resolved row metadata. */
		void apply_delta(const MultiwaySparseRowMetadata& row, std::uint32_t bucket,
			std::uint8_t action, double regret, double strategy_sum);
		// Shared scalar kernel for both in-memory and external compact pages.
		static void apply_delta_to_cells(std::int32_t& regret_cell,
			std::uint64_t& strategy_mass_cell, double regret, double strategy_sum);
		/** Scales all accumulated regrets. */
		void scale_regrets(double factor);
		[[nodiscard]] std::size_t scale_and_prune_regrets(
			double factor, double threshold, double regret_floor) noexcept;
		[[nodiscard]] std::size_t prune_negative_regrets(
			double threshold = 0.0, double regret_floor = 0.0) noexcept;
		/** Builds a regret-matched distribution for one bucket. */
		[[nodiscard]] std::vector<Probability> regret_matched_strategy(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		/** Writes a regret-matched distribution into caller-owned storage. */
		void regret_matched_strategy_into(
			MultiwayInfosetId infoset, std::uint32_t bucket,
			Probability* output, std::size_t output_size) const;
		/** Builds the accumulated average strategy for one bucket. */
		[[nodiscard]] std::vector<Probability> average_strategy(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		/** Writes the average strategy into caller-owned storage. */
		void average_strategy_into(
			MultiwayInfosetId infoset, std::uint32_t bucket,
			Probability* output, std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
			double threshold) const noexcept;
		[[nodiscard]] std::vector<double> strategy_sums(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		[[nodiscard]] const std::vector<MultiwaySparseRowMetadata>& rows() const noexcept { return rows_; }
		[[nodiscard]] CheckpointPageView checkpoint_page(std::size_t index) const noexcept
		{
			if (index >= pages_.size())
				return {};
			const auto& page = pages_[index];
			return { page.infoset, page.page_index, page.bucket_count,
				regrets_.data() + page.regret_offset,
				strategy_mass_.data() + page.strategy_sum_offset };
		}
		/** Copies compact row/page values into the checkpoint representation. */
		void export_checkpoint(MultiwaySparseStorageCheckpoint& output) const;
		/** Restores compact values after row and active-page metadata are admitted. */
		void restore_checkpoint_values(
			const std::vector<std::int32_t>& regrets,
			const std::vector<std::uint64_t>& strategy_masses);
		[[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
		[[nodiscard]] std::size_t value_count() const noexcept { return regrets_.size(); }
		[[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
		[[nodiscard]] std::uint32_t page_width() const noexcept { return page_width_; }
		[[nodiscard]] std::size_t active_cell_count() const noexcept { return regrets_.size(); }
		[[nodiscard]] std::size_t configured_cell_capacity() const noexcept { return max_values_; }
		[[nodiscard]] static std::size_t page_metadata_size() noexcept;
		[[nodiscard]] std::size_t memory_bytes() const noexcept;
		/** Returns categorized memory accounting for compact storage. */
		[[nodiscard]] MultiwayCompactStorageMemorySnapshot memory_snapshot() const noexcept;

	private:
#pragma pack(push, 8)
		struct PageMetadata
		{
			MultiwayInfosetId infoset{};
			std::uint32_t page_index = 0U;
			std::uint32_t bucket_count = 0U;
			std::size_t regret_offset = 0U;
			std::size_t strategy_sum_offset = 0U;
		};
#pragma pack(pop)

		[[nodiscard]] const PageMetadata* page(
			MultiwayInfosetId infoset, std::uint32_t page_index) const noexcept;
		[[nodiscard]] PageMetadata* page(
			MultiwayInfosetId infoset, std::uint32_t page_index) noexcept;
		[[nodiscard]] const PageMetadata* page_for_bucket(
			const MultiwaySparseRowMetadata& row, std::uint32_t bucket) const noexcept;
		[[nodiscard]] static std::size_t page_cell(
			const PageMetadata& page, std::uint32_t bucket, std::uint8_t action,
			std::uint32_t page_width) noexcept;

		std::size_t max_rows_ = 0;
		std::size_t max_values_ = 0;
		std::uint32_t page_width_ = kDefaultPageWidth;
		std::vector<MultiwaySparseRowMetadata> rows_;
		std::vector<PageMetadata> pages_;
		std::vector<std::int32_t> regrets_;
		std::vector<std::uint64_t> strategy_mass_;
	};

} // namespace texas::solver::multiway
