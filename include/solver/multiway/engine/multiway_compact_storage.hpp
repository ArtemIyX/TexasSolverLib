#pragma once

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
	class MultiwayCompactStorage
	{
	public:
		static constexpr std::int32_t kRegretMin = -2'000'000'000;
		static constexpr std::int32_t kRegretMax = 2'000'000'000;
		static constexpr std::uint64_t kMassScale = 1ULL << 20U;
		static constexpr std::uint32_t kDefaultPageWidth = 16U;

		MultiwayCompactStorage(
			std::size_t max_rows,
			std::size_t max_values,
			std::uint32_t page_width = kDefaultPageWidth);

		void admit_row(const MultiwaySparseRowShape& shape);
		void admit_rows(
			const std::vector<MultiwaySparseRowShape>& shapes,
			const std::vector<std::uint8_t>& new_row_flags);
		void admit_page(MultiwayInfosetId infoset, std::uint32_t page_index);
		[[nodiscard]] bool has_row(MultiwayInfosetId infoset) const noexcept;
		[[nodiscard]] bool has_page(MultiwayInfosetId infoset, std::uint32_t page_index) const noexcept;
		[[nodiscard]] const MultiwaySparseRowMetadata* metadata(MultiwayInfosetId infoset) const noexcept;
		void apply_delta(MultiwayInfosetId infoset, std::uint32_t bucket,
			std::uint8_t action, double regret, double strategy_sum);
		void apply_delta(const MultiwaySparseRowMetadata& row, std::uint32_t bucket,
			std::uint8_t action, double regret, double strategy_sum);
		void scale_regrets(double factor);
		[[nodiscard]] std::size_t scale_and_prune_regrets(
			double factor, double threshold, double regret_floor) noexcept;
		[[nodiscard]] std::size_t prune_negative_regrets(
			double threshold = 0.0, double regret_floor = 0.0) noexcept;
		[[nodiscard]] std::vector<Probability> regret_matched_strategy(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		void regret_matched_strategy_into(
			MultiwayInfosetId infoset, std::uint32_t bucket,
			Probability* output, std::size_t output_size) const;
		[[nodiscard]] std::vector<Probability> average_strategy(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		void average_strategy_into(
			MultiwayInfosetId infoset, std::uint32_t bucket,
			Probability* output, std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
			double threshold) const noexcept;
		[[nodiscard]] std::vector<double> strategy_sums(
			MultiwayInfosetId infoset, std::uint32_t bucket) const;
		[[nodiscard]] const std::vector<MultiwaySparseRowMetadata>& rows() const noexcept { return rows_; }
		void export_checkpoint(MultiwaySparseStorageCheckpoint& output) const;
		void restore_checkpoint_values(
			const std::vector<std::int32_t>& regrets,
			const std::vector<std::uint64_t>& strategy_masses);
		[[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
		[[nodiscard]] std::size_t value_count() const noexcept { return regrets_.size(); }
		[[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
		[[nodiscard]] std::uint32_t page_width() const noexcept { return page_width_; }
		[[nodiscard]] std::size_t active_cell_count() const noexcept { return regrets_.size(); }
		[[nodiscard]] std::size_t configured_cell_capacity() const noexcept { return max_values_; }
		[[nodiscard]] std::size_t memory_bytes() const noexcept;
		[[nodiscard]] MultiwayCompactStorageMemorySnapshot memory_snapshot() const noexcept;

	private:
		struct PageMetadata
		{
			MultiwayInfosetId infoset{};
			std::uint32_t page_index = 0U;
			std::uint32_t bucket_count = 0U;
			std::size_t regret_offset = 0U;
			std::size_t strategy_sum_offset = 0U;
		};

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
