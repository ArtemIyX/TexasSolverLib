#pragma once

/** @file
 * @brief Immutable multiway bucket-table and board-registry lookup interfaces.
 */

#include "core/poker.hpp"

#include "core/canonical_combo.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <filesystem>
#include <iterator>

namespace texas::solver::multiway
{

	/** @brief Sentinel returned when a private hand is blocked or not assigned. */
	inline constexpr std::uint32_t MULTIWAY_INVALID_BUCKET = 0xffffffffU;
	/** @brief Number of unordered two-card combinations in a 52-card deck. */
	inline constexpr std::size_t MULTIWAY_HOLE_COMBINATION_COUNT = core::CANONICAL_HOLE_COMBINATION_COUNT;

	class MultiwayBucketRegistry;
	struct MultiwayProcessMemorySnapshotCollection;
	struct MultiwayBucketRegistryMemorySnapshot;

	/** @brief Non-owning board view supporting packed and byte-array storage. */
	class MultiwayBoardView
	{
	public:
		class const_iterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::uint8_t;
			using difference_type = std::ptrdiff_t;
			using pointer = void;
			using reference = std::uint8_t;
			std::uint8_t operator*() const noexcept;
			const_iterator& operator++() noexcept
			{
				++index_;
				return *this;
			}
			const_iterator operator++(int) noexcept
			{
				auto copy = *this;
				++*this;
				return copy;
			}
			friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept;
			friend bool operator!=(const const_iterator& left, const const_iterator& right) noexcept { return !(left == right); }

		private:
			const_iterator(const std::uint8_t* bytes, std::uint64_t packed, std::uint8_t size, bool is_packed, std::uint8_t index) noexcept
				: bytes_(bytes), packed_(packed), size_(size), is_packed_(is_packed), index_(index) {}
			const std::uint8_t* bytes_ = nullptr;
			std::uint64_t packed_ = 0U;
			std::uint8_t size_ = 0U;
			bool is_packed_ = false;
			std::uint8_t index_ = 0U;
			friend class MultiwayBoardView;
		};

		/** @brief Returns the number of board cards. */
		[[nodiscard]] std::size_t size() const noexcept { return size_; }
		/** @brief Reports whether the board contains no cards. */
		[[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
		/** @brief Returns the card at index without bounds checking. */
		[[nodiscard]] std::uint8_t operator[](std::size_t index) const noexcept;
		/** @brief Returns an iterator to the first card. */
		[[nodiscard]] const_iterator begin() const noexcept { return { bytes_, packed_, size_, packed_view_, 0U }; }
		/** @brief Returns an iterator past the final card. */
		[[nodiscard]] const_iterator end() const noexcept { return { bytes_, packed_, size_, packed_view_, size_ }; }
		friend bool operator==(MultiwayBoardView left, MultiwayBoardView right) noexcept;
		friend bool operator<(MultiwayBoardView left, MultiwayBoardView right) noexcept;
		friend bool operator==(MultiwayBoardView left, const std::vector<std::uint8_t>& right) noexcept;
		friend bool operator==(const std::vector<std::uint8_t>& left, MultiwayBoardView right) noexcept { return right == left; }
		friend bool operator<(MultiwayBoardView left, const std::vector<std::uint8_t>& right) noexcept;

	private:
		const std::uint8_t* bytes_ = nullptr;
		std::uint64_t packed_ = 0U;
		std::uint8_t size_ = 0U;
		bool packed_view_ = false;
		MultiwayBoardView(const std::uint8_t* bytes, std::uint8_t size) noexcept : bytes_(bytes), size_(size) {}
		MultiwayBoardView(std::uint64_t packed, std::uint8_t size) noexcept : packed_(packed), size_(size), packed_view_(true) {}
		friend class MultiwayBucketTable;
	};

	// Immutable bucket assignments for one canonical postflop board. Entries use
	// the fixed unordered-card index; board-blocked hands retain INVALID_BUCKET.
	/** @brief Immutable assignments from canonical private hands to buckets on one board. */
	class MultiwayBucketTable
	{
	public:
		/** @brief Creates an owned table from identity, board and assignments. */
		MultiwayBucketTable(
			MultiwayModelIdentity identity,
			core::Street street,
			std::vector<std::uint8_t> canonical_board,
			std::uint32_t bucket_count,
			std::vector<std::uint32_t> assignments);
		~MultiwayBucketTable();
		MultiwayBucketTable(const MultiwayBucketTable& other);
		MultiwayBucketTable& operator=(const MultiwayBucketTable& other);
		MultiwayBucketTable(MultiwayBucketTable&&) noexcept;
		MultiwayBucketTable& operator=(MultiwayBucketTable&&) noexcept;

		/** @brief Returns the model identity that produced this table. */
		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept;
		/** @brief Returns the table street. */
		[[nodiscard]] core::Street street() const noexcept;
		/** @brief Returns the canonical board as a non-owning view. */
		[[nodiscard]] MultiwayBoardView canonical_board() const noexcept;
		/** @brief Returns the number of valid buckets. */
		[[nodiscard]] std::uint32_t bucket_count() const noexcept;
		// Stable runtime-only identity of this table's model, board, and assignments.
		[[nodiscard]] std::uint64_t table_identity() const noexcept;
		/** @brief Returns owned assignment storage; mapped tables expose an empty vector. */
		[[nodiscard]] const std::vector<std::uint32_t>& assignments() const noexcept;
		/** @brief Returns one assignment by canonical hole-combination index. */
		[[nodiscard]] std::uint32_t assignment_at(std::size_t index) const noexcept
		{
			if (mapped_directory_entry_ == nullptr)
				return owned_->assignments[index];
			const auto* p = !mapped_schema5_
				? mapped_directory_entry_ + 2U + mapped_directory_entry_[1] + 4U + index * 4U
				: mapped_artifact_data_ + read_mapped_u64(mapped_directory_entry_ + 16U) + index * mapped_assignment_width_;
			std::uint32_t value = 0U;
			const auto width = !mapped_schema5_ ? 4U : mapped_assignment_width_;
			for (std::uint8_t byte = 0U; byte < width; ++byte)
				value |= static_cast<std::uint32_t>(p[byte]) << (byte * 8U);
			if (width < 4U && value == ((1U << (width * 8U)) - 1U))
			{
				if (bucket_count() != (1U << (width * 8U)))
					return MULTIWAY_INVALID_BUCKET;
				const auto& hole = core::canonical_combos().cards(static_cast<core::CanonicalComboId>(index));
				for (const auto card : canonical_board())
					if (hole[0] == card || hole[1] == card)
						return MULTIWAY_INVALID_BUCKET;
			}
			return value;
		}
		/** @brief Returns the number of hole combinations covered. */
		[[nodiscard]] std::size_t assignment_count() const noexcept
		{
			return mapped_directory_entry_ != nullptr ? MULTIWAY_HOLE_COMBINATION_COUNT : owned_->assignments.size();
		}
		/** @brief Reports owned board-vector capacity. */
		[[nodiscard]] std::size_t owned_board_capacity() const noexcept;
		/** @brief Reports active owned board-vector size. */
		[[nodiscard]] std::size_t owned_board_size() const noexcept;
		/** @brief Reports owned assignment-vector capacity. */
		[[nodiscard]] std::size_t owned_assignment_capacity() const noexcept;
		/** @brief Reports bytes of owned table storage. */
		[[nodiscard]] std::size_t owned_storage_bytes() const noexcept;

		// Cards are always compact deck indices in [0, 51].
		/** @brief Looks up a bucket for a compact-card hole pair. */
		[[nodiscard]] std::uint32_t lookup(const std::array<std::uint8_t, 2>& hole) const;
		/** @brief Returns the canonical index for an unordered compact-card pair. */
		[[nodiscard]] static std::size_t hole_index(const std::array<std::uint8_t, 2>& hole);

	private:
		struct OwnedData
		{
			MultiwayModelIdentity identity{};
			core::Street street = core::Street::Preflop;
			std::vector<std::uint8_t> board;
			std::uint32_t bucket_count = 0U;
			std::uint64_t table_identity = 0U;
			std::vector<std::uint32_t> assignments;
		};
		MultiwayBucketTable() = default;
		std::unique_ptr<OwnedData> owned_;
		const std::uint8_t* mapped_directory_entry_ = nullptr;
		const std::uint8_t* mapped_artifact_data_ = nullptr;
		const MultiwayModelIdentity* mapped_identity_ = nullptr;
		std::uint8_t mapped_assignment_width_ = 4U;
		bool mapped_schema5_ = false;
		static std::uint64_t read_mapped_u64(const std::uint8_t* p) noexcept
		{
			std::uint64_t value = 0U;
			for (std::uint8_t byte = 0U; byte < 8U; ++byte)
				value |= static_cast<std::uint64_t>(p[byte]) << (byte * 8U);
			return value;
		}
		friend class MultiwayBucketRegistry;
		friend MultiwayBucketRegistry load_multiway_bucket_registry(
			const std::filesystem::path& path,
			MultiwayProcessMemorySnapshotCollection* process_memory_snapshots);
	};

	// Sorted immutable board registry. Lookup uses binary search, not a hash map,
	// so traversal can resolve a board/bucket pair without a hot-path allocation.
	/** @brief Sorted board tables with binary-search lookup and optional mapped backing. */
	class MultiwayBucketRegistry
	{
	public:
		/** @brief Builds a registry from owned tables and validates sorting/identity. */
		explicit MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables);
		~MultiwayBucketRegistry();
		MultiwayBucketRegistry(MultiwayBucketRegistry&&) noexcept;
		MultiwayBucketRegistry& operator=(MultiwayBucketRegistry&&) noexcept;
		MultiwayBucketRegistry(const MultiwayBucketRegistry&) = delete;
		MultiwayBucketRegistry& operator=(const MultiwayBucketRegistry&) = delete;

		/** @brief Returns the model identity shared by registry tables. */
		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
		[[nodiscard]] const std::vector<MultiwayBucketTable>& tables() const noexcept
		{
			return tables_;
		}
		// Cards are always compact deck indices in [0, 51].
		/** @brief Returns a table for street and canonical board. */
		[[nodiscard]] const MultiwayBucketTable& table(
			core::Street street,
			const std::vector<std::uint8_t>& canonical_board) const;
		/** @brief Looks up one private hand's bucket on a canonical board. */
		[[nodiscard]] std::uint32_t lookup(
			core::Street street,
			const std::vector<std::uint8_t>& canonical_board,
			const std::array<std::uint8_t, 2>& hole) const;
		// Cold-boundary accounting only. Mapped bytes are virtual artifact bytes;
		// owned bytes cover registry/table metadata and copied payloads.
		/** @brief Returns bytes owned by registry metadata and copied tables. */
		[[nodiscard]] std::size_t owned_memory_bytes() const noexcept;
		/** @brief Returns mapped artifact bytes, or zero for owned-only registries. */
		[[nodiscard]] std::size_t mapped_artifact_bytes() const noexcept;
		/** @brief Returns owned and mapped memory accounting. */
		[[nodiscard]] MultiwayBucketRegistryMemorySnapshot memory_snapshot() const noexcept;
		/** @brief Reports whether registry storage is backed by a mapped artifact. */
		[[nodiscard]] bool is_mapped() const noexcept { return mapped_artifact_ != nullptr; }

	private:
		MultiwayModelIdentity identity_{};
		std::vector<MultiwayBucketTable> tables_;
		const std::uint8_t* mapped_directory_ = nullptr;
		std::uint32_t mapped_directory_count_ = 0U;
		std::unique_ptr<MultiwayModelIdentity> mapped_identity_storage_;
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
			std::unique_ptr<MappedArtifact> mapped_artifact, const MultiwayModelIdentity& identity);
		friend MultiwayBucketRegistry load_multiway_bucket_registry(
			const std::filesystem::path& path,
			MultiwayProcessMemorySnapshotCollection* process_memory_snapshots);
	};

	static_assert(sizeof(MultiwayBucketTable) <= 40U, "mapped bucket table views must remain compact");

} // namespace texas::solver::multiway
