#pragma once

#include "core/poker.hpp"

#include "core/canonical_combo.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <filesystem>

namespace texas::solver::multiway
{

	inline constexpr std::uint32_t MULTIWAY_INVALID_BUCKET = 0xffffffffU;
	inline constexpr std::size_t MULTIWAY_HOLE_COMBINATION_COUNT = core::CANONICAL_HOLE_COMBINATION_COUNT;

	class MultiwayBucketRegistry;

	// Immutable bucket assignments for one canonical postflop board. Entries use
	// the fixed unordered-card index; board-blocked hands retain INVALID_BUCKET.
	class MultiwayBucketTable
	{
	public:
		MultiwayBucketTable(
			MultiwayModelIdentity identity,
			core::Street street,
			std::vector<std::uint8_t> canonical_board,
			std::uint32_t bucket_count,
			std::vector<std::uint32_t> assignments);

		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
		[[nodiscard]] core::Street street() const noexcept { return street_; }
		[[nodiscard]] const std::vector<std::uint8_t>& canonical_board() const noexcept
		{
			return canonical_board_;
		}
		[[nodiscard]] std::uint32_t bucket_count() const noexcept { return bucket_count_; }
		// Stable runtime-only identity of this table's model, board, and assignments.
		[[nodiscard]] std::uint64_t table_identity() const noexcept { return table_identity_; }
		[[nodiscard]] const std::vector<std::uint32_t>& assignments() const noexcept
		{
			return assignments_;
		}
		[[nodiscard]] std::uint32_t assignment_at(std::size_t index) const noexcept
		{
			if (mapped_assignments_ == nullptr)
				return assignments_[index];
			const auto* p = mapped_assignments_ + index * 4U;
			return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) | (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
		}
		[[nodiscard]] std::size_t assignment_count() const noexcept
		{
			return mapped_assignments_ != nullptr ? MULTIWAY_HOLE_COMBINATION_COUNT : assignments_.size();
		}

		// Cards are always compact deck indices in [0, 51].
		[[nodiscard]] std::uint32_t lookup(const std::array<std::uint8_t, 2>& hole) const;
		[[nodiscard]] static std::size_t hole_index(const std::array<std::uint8_t, 2>& hole);

	private:
		MultiwayBucketTable() = default;
		MultiwayModelIdentity identity_{};
		core::Street street_ = core::Street::Preflop;
		std::vector<std::uint8_t> canonical_board_;
		std::uint32_t bucket_count_ = 0;
		std::uint64_t table_identity_ = 0U;
		std::vector<std::uint32_t> assignments_;
		const std::uint8_t* mapped_assignments_ = nullptr;
		friend class MultiwayBucketRegistry;
		friend MultiwayBucketRegistry load_multiway_bucket_registry(const std::filesystem::path& path);
	};

	// Sorted immutable board registry. Lookup uses binary search, not a hash map,
	// so traversal can resolve a board/bucket pair without a hot-path allocation.
	class MultiwayBucketRegistry
	{
	public:
		explicit MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables);
		~MultiwayBucketRegistry();
		MultiwayBucketRegistry(MultiwayBucketRegistry&&) noexcept;
		MultiwayBucketRegistry& operator=(MultiwayBucketRegistry&&) noexcept;
		MultiwayBucketRegistry(const MultiwayBucketRegistry&) = delete;
		MultiwayBucketRegistry& operator=(const MultiwayBucketRegistry&) = delete;

		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
		[[nodiscard]] const std::vector<MultiwayBucketTable>& tables() const noexcept
		{
			return tables_;
		}
		// Cards are always compact deck indices in [0, 51].
		[[nodiscard]] const MultiwayBucketTable& table(
			core::Street street,
			const std::vector<std::uint8_t>& canonical_board) const;
		[[nodiscard]] std::uint32_t lookup(
			core::Street street,
			const std::vector<std::uint8_t>& canonical_board,
			const std::array<std::uint8_t, 2>& hole) const;

	private:
		MultiwayModelIdentity identity_{};
		std::vector<MultiwayBucketTable> tables_;
		struct MappedArtifact
		{
			const std::uint8_t* data = nullptr;
			std::size_t size = 0U;
			std::intptr_t file = -1;
			void* mapping = nullptr;
			~MappedArtifact();
		};
		std::unique_ptr<MappedArtifact> mapped_artifact_;

		MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables,
			std::unique_ptr<MappedArtifact> mapped_artifact);
		friend MultiwayBucketRegistry load_multiway_bucket_registry(const std::filesystem::path& path);
	};

} // namespace texas::solver::multiway
