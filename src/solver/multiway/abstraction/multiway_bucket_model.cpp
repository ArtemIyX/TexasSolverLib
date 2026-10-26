#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"

#include "core/canonical_combo.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{

	using core::canonical_combos;
	using core::CanonicalComboId;
	using core::is_card_index;
	using core::Street;
	namespace
	{

		std::size_t expected_board_count(Street street)
		{
			switch (street)
			{
				case Street::Flop:
					return 3U;
				case Street::Turn:
					return 4U;
				case Street::River:
					return 5U;
				default:
					break;
			}
			throw std::invalid_argument("multiway bucket table requires a postflop street");
		}

		bool are_valid_compact_cards(const std::uint8_t* cards, std::size_t count) noexcept
		{
			if (cards == nullptr && count != 0U)
				return false;
			for (std::size_t index = 0U; index < count; ++index)
			{
				if (!is_card_index(cards[index]))
					return false;
				for (std::size_t prior = 0U; prior < index; ++prior)
				{
					if (cards[index] == cards[prior])
						return false;
				}
			}
			return true;
		}

		std::uint64_t stable_table_identity(
			const MultiwayModelIdentity& identity,
			Street street,
			const std::vector<std::uint8_t>& canonical_board,
			std::uint32_t bucket_count,
			const std::vector<std::uint32_t>& assignments) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			const auto append = [&hash](std::uint64_t value) noexcept {
				texas::core::fingerprint::append_u64(hash, value);
			};
			append(identity.combined_hash);
			append(static_cast<std::uint8_t>(street));
			append(canonical_board.size());
			for (const auto card : canonical_board)
				append(card);
			append(bucket_count);
			for (const auto assignment : assignments)
				append(assignment);
			return hash;
		}

		bool is_live_hole(MultiwayBoardView board, const std::array<std::uint8_t, 2>& hole)
		{
			for (const auto card : board)
				if (card == hole[0] || card == hole[1]) return false;
			return true;
		}

	} // namespace

	std::uint8_t MultiwayBoardView::operator[](std::size_t index) const noexcept
	{
		return packed_view_ ? static_cast<std::uint8_t>((packed_ >> (index * 6U)) & 0x3fU) : bytes_[index];
	}

	std::uint8_t MultiwayBoardView::const_iterator::operator*() const noexcept
	{
		return is_packed_ ? static_cast<std::uint8_t>((packed_ >> (index_ * 6U)) & 0x3fU) : bytes_[index_];
	}

	bool operator==(const MultiwayBoardView::const_iterator& left, const MultiwayBoardView::const_iterator& right) noexcept
	{
		return left.bytes_ == right.bytes_ && left.packed_ == right.packed_ && left.size_ == right.size_ &&
			left.is_packed_ == right.is_packed_ && left.index_ == right.index_;
	}

	bool operator==(MultiwayBoardView left, MultiwayBoardView right) noexcept
	{
		if (left.size_ != right.size_) return false;
		for (std::size_t index = 0U; index < left.size_; ++index)
			if (left[index] != right[index]) return false;
		return true;
	}

	bool operator<(MultiwayBoardView left, MultiwayBoardView right) noexcept
	{
		const auto count = std::min<std::size_t>(left.size_, right.size_);
		for (std::size_t index = 0U; index < count; ++index)
		{
			if (left[index] != right[index]) return left[index] < right[index];
		}
		return left.size_ < right.size_;
	}

	bool operator==(MultiwayBoardView left, const std::vector<std::uint8_t>& right) noexcept
	{
		if (left.size_ != right.size()) return false;
		for (std::size_t index = 0U; index < left.size_; ++index)
			if (left[index] != right[index]) return false;
		return true;
	}

	bool operator<(MultiwayBoardView left, const std::vector<std::uint8_t>& right) noexcept
	{
		const auto count = std::min<std::size_t>(left.size_, right.size());
		for (std::size_t index = 0U; index < count; ++index)
		{
			if (left[index] != right[index]) return left[index] < right[index];
		}
		return left.size_ < right.size();
	}

	MultiwayBucketTable::MultiwayBucketTable(
		MultiwayModelIdentity identity,
		Street street,
		std::vector<std::uint8_t> canonical_board,
		std::uint32_t bucket_count,
		std::vector<std::uint32_t> assignments)
		: owned_(std::make_unique<OwnedData>())
	{
		owned_->identity = identity;
		owned_->street = street;
		owned_->board = std::move(canonical_board);
		owned_->bucket_count = bucket_count;
		owned_->assignments = std::move(assignments);
		identity.validate();
		std::sort(owned_->board.begin(), owned_->board.end());
		if (owned_->board.size() != expected_board_count(street) || !are_valid_compact_cards(owned_->board.data(), owned_->board.size()) || bucket_count == 0U || owned_->assignments.size() != MULTIWAY_HOLE_COMBINATION_COUNT)
		{
			throw std::invalid_argument("multiway bucket table has invalid metadata");
		}
		for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
		{
			const auto& hole = canonical_combos().cards(id);
			const auto assignment = owned_->assignments[id];
			if (is_live_hole(MultiwayBoardView(owned_->board.data(), static_cast<std::uint8_t>(owned_->board.size())), hole))
			{
				if (assignment >= bucket_count)
				{
					throw std::invalid_argument("multiway bucket table omits a live hole-card pair");
				}
			}
			else if (assignment != MULTIWAY_INVALID_BUCKET)
			{
				throw std::invalid_argument("multiway bucket table assigns a board-blocked hole-card pair");
			}
		}
		owned_->table_identity = stable_table_identity(identity, street, owned_->board, bucket_count, owned_->assignments);
	}

	MultiwayBucketTable::~MultiwayBucketTable() = default;
	MultiwayBucketTable::MultiwayBucketTable(const MultiwayBucketTable& other)
		: owned_(other.owned_ == nullptr ? nullptr : std::make_unique<OwnedData>(*other.owned_)),
		  mapped_directory_entry_(other.mapped_directory_entry_),
		  mapped_artifact_data_(other.mapped_artifact_data_), mapped_identity_(other.mapped_identity_),
		  mapped_assignment_width_(other.mapped_assignment_width_), mapped_schema5_(other.mapped_schema5_)
	{}
	MultiwayBucketTable& MultiwayBucketTable::operator=(const MultiwayBucketTable& other)
	{
		if (this == &other) return *this;
		owned_ = other.owned_ == nullptr ? nullptr : std::make_unique<OwnedData>(*other.owned_);
		mapped_directory_entry_ = other.mapped_directory_entry_;
		mapped_artifact_data_ = other.mapped_artifact_data_; mapped_identity_ = other.mapped_identity_;
		mapped_assignment_width_ = other.mapped_assignment_width_; mapped_schema5_ = other.mapped_schema5_;
		return *this;
	}
	MultiwayBucketTable::MultiwayBucketTable(MultiwayBucketTable&&) noexcept = default;
	MultiwayBucketTable& MultiwayBucketTable::operator=(MultiwayBucketTable&&) noexcept = default;

	const MultiwayModelIdentity& MultiwayBucketTable::identity() const noexcept
	{
		return owned_ != nullptr ? owned_->identity : *mapped_identity_;
	}
	core::Street MultiwayBucketTable::street() const noexcept
	{
		return mapped_directory_entry_ != nullptr ? static_cast<Street>(mapped_directory_entry_[0]) : owned_->street;
	}
	MultiwayBoardView MultiwayBucketTable::canonical_board() const noexcept
	{
		if (mapped_schema5_)
			return MultiwayBoardView(read_mapped_u64(mapped_directory_entry_ + 4U), mapped_directory_entry_[1]);
		return MultiwayBoardView(owned_->board.data(), static_cast<std::uint8_t>(owned_->board.size()));
	}
	std::uint32_t MultiwayBucketTable::bucket_count() const noexcept
	{
		if (mapped_schema5_)
			return static_cast<std::uint32_t>(mapped_directory_entry_[12]) | (static_cast<std::uint32_t>(mapped_directory_entry_[13]) << 8U) |
				(static_cast<std::uint32_t>(mapped_directory_entry_[14]) << 16U) | (static_cast<std::uint32_t>(mapped_directory_entry_[15]) << 24U);
		return owned_->bucket_count;
	}
	std::uint64_t MultiwayBucketTable::table_identity() const noexcept
	{
		if (mapped_schema5_) return read_mapped_u64(mapped_directory_entry_ + 24U);
		return owned_->table_identity;
	}
	const std::vector<std::uint32_t>& MultiwayBucketTable::assignments() const noexcept
	{
		static const std::vector<std::uint32_t> empty;
		return owned_ == nullptr ? empty : owned_->assignments;
	}
	std::size_t MultiwayBucketTable::owned_board_capacity() const noexcept { return owned_ == nullptr ? 0U : owned_->board.capacity(); }
	std::size_t MultiwayBucketTable::owned_board_size() const noexcept { return owned_ == nullptr ? 0U : owned_->board.size(); }
	std::size_t MultiwayBucketTable::owned_assignment_capacity() const noexcept { return owned_ == nullptr ? 0U : owned_->assignments.capacity(); }
	std::size_t MultiwayBucketTable::owned_storage_bytes() const noexcept { return owned_ == nullptr ? 0U : sizeof(OwnedData); }

	std::uint32_t MultiwayBucketTable::lookup(const std::array<std::uint8_t, 2>& hole) const
	{
		if (!is_live_hole(canonical_board(), hole))
		{
			throw std::invalid_argument("multiway bucket lookup requires a live distinct hole-card pair");
		}
		const auto bucket = assignment_at(hole_index(hole));
		if (bucket >= bucket_count())
		{
			throw std::logic_error("multiway bucket table has no assignment for a live hole-card pair");
		}
		return bucket;
	}

	std::size_t MultiwayBucketTable::hole_index(const std::array<std::uint8_t, 2>& hole)
	{
		if (!are_valid_compact_cards(hole.data(), hole.size()))
		{
			throw std::invalid_argument("multiway bucket hole index requires distinct valid cards");
		}
		return canonical_combos().id(hole);
	}

	MultiwayBucketRegistry::MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables)
		: tables_(std::move(tables))
	{
		if (tables_.empty())
			throw std::invalid_argument("multiway bucket registry requires at least one table");
		identity_ = tables_.front().identity();
		identity_.validate();
		for (const auto& table : tables_)
		{
			if (table.identity() != identity_)
			{
				throw std::invalid_argument("multiway bucket registry has mixed model identities");
			}
		}
		std::sort(tables_.begin(), tables_.end(), [](const MultiwayBucketTable& left, const MultiwayBucketTable& right) {
			if (left.street() != right.street())
				return left.street() < right.street();
			return left.canonical_board() < right.canonical_board();
		});
		for (std::size_t index = 1; index < tables_.size(); ++index)
		{
			if (tables_[index - 1U].street() == tables_[index].street() && tables_[index - 1U].canonical_board() == tables_[index].canonical_board())
			{
				throw std::invalid_argument("multiway bucket registry has duplicate board tables");
			}
		}
	}

	MultiwayBucketRegistry::MultiwayBucketRegistry(
		std::vector<MultiwayBucketTable> tables,
		std::unique_ptr<MappedArtifact> mapped_artifact, const MultiwayModelIdentity& identity)
		: tables_(std::move(tables)), mapped_artifact_(std::move(mapped_artifact))
	{
		if (tables_.empty())
			throw std::invalid_argument("multiway bucket registry requires at least one table");
		identity_ = identity;
		identity_.validate();
		mapped_identity_storage_ = std::make_unique<MultiwayModelIdentity>(identity_);
		for (const auto& table : tables_)
		{
			if (table.bucket_count() == 0U || table.mapped_directory_entry_ == nullptr)
			{
				throw std::invalid_argument("multiway bucket registry has invalid mapped table");
			}
		}
		for (auto& table : tables_) table.mapped_identity_ = mapped_identity_storage_.get();
		std::sort(tables_.begin(), tables_.end(), [](const auto& left, const auto& right) {
			if (left.street() != right.street())
				return left.street() < right.street();
			return left.canonical_board() < right.canonical_board();
		});
		for (std::size_t index = 1U; index < tables_.size(); ++index)
			if (tables_[index - 1U].street() == tables_[index].street() && tables_[index - 1U].canonical_board() == tables_[index].canonical_board())
				throw std::invalid_argument("multiway bucket registry has duplicate mapped boards");
	}

	MultiwayBucketRegistry::~MultiwayBucketRegistry() = default;
	MultiwayBucketRegistry::MultiwayBucketRegistry(MultiwayBucketRegistry&&) noexcept = default;
	MultiwayBucketRegistry& MultiwayBucketRegistry::operator=(MultiwayBucketRegistry&&) noexcept = default;

	const MultiwayBucketTable& MultiwayBucketRegistry::table(
		Street street,
		const std::vector<std::uint8_t>& canonical_board) const
	{
		if (mapped_directory_ != nullptr)
		{
			std::size_t first = 0U;
			std::size_t last = mapped_directory_count_;
			while (first < last)
			{
				const auto middle = first + (last - first) / 2U;
				const auto* entry = mapped_directory_ + middle * 32U;
				int comparison = entry[0] < static_cast<std::uint8_t>(street) ? -1 : entry[0] > static_cast<std::uint8_t>(street) ? 1 : 0;
				if (comparison == 0)
				{
					const auto packed = MultiwayBucketTable::read_mapped_u64(entry + 4U);
					const auto board_size = entry[1];
					for (std::size_t card = 0U; card < std::min<std::size_t>(board_size, canonical_board.size()); ++card)
					{
						const auto mapped_card = static_cast<std::uint8_t>((packed >> (card * 6U)) & 0x3fU);
						if (mapped_card != canonical_board[card])
						{
							comparison = mapped_card < canonical_board[card] ? -1 : 1;
							break;
						}
					}
					if (comparison == 0 && board_size != canonical_board.size())
						comparison = board_size < canonical_board.size() ? -1 : 1;
				}
				if (comparison < 0) first = middle + 1U;
				else last = middle;
			}
			if (first < mapped_directory_count_)
			{
				const auto* entry = mapped_directory_ + first * 32U;
				if (entry[0] == static_cast<std::uint8_t>(street) && tables_[first].canonical_board() == canonical_board)
					return tables_[first];
			}
			throw std::out_of_range("multiway bucket registry has no table for the canonical compact board");
		}
		const auto found = std::lower_bound(
			tables_.begin(), tables_.end(), std::pair<Street, const std::vector<std::uint8_t>&>{ street, canonical_board },
			[](const MultiwayBucketTable& candidate,
				const std::pair<Street, const std::vector<std::uint8_t>&>& key) {
				if (candidate.street() != key.first)
					return candidate.street() < key.first;
				return candidate.canonical_board() < key.second;
			});
		if (found != tables_.end() && found->street() == street && found->canonical_board() == canonical_board)
		{
			return *found;
		}

		throw std::out_of_range("multiway bucket registry has no table for the canonical compact board");
	}

	std::uint32_t MultiwayBucketRegistry::lookup(
		Street street,
		const std::vector<std::uint8_t>& canonical_board,
		const std::array<std::uint8_t, 2>& hole) const
	{
		return table(street, canonical_board).lookup(hole);
	}

	std::size_t MultiwayBucketRegistry::owned_memory_bytes() const noexcept
	{
		std::uint64_t bytes = multiway_memory_saturating_capacity_bytes(tables_.capacity(), sizeof(MultiwayBucketTable));
		if (mapped_identity_storage_ != nullptr)
			bytes = multiway_memory_saturating_add(bytes, sizeof(MultiwayModelIdentity));
		for (const auto& table : tables_)
		{
			bytes = multiway_memory_saturating_add(bytes, table.owned_storage_bytes());
			bytes = multiway_memory_saturating_add(bytes,
				multiway_memory_saturating_capacity_bytes(table.owned_board_capacity(), sizeof(std::uint8_t)));
			bytes = multiway_memory_saturating_add(bytes,
				multiway_memory_saturating_capacity_bytes(table.owned_assignment_capacity(), sizeof(std::uint32_t)));
		}
		return bytes > std::numeric_limits<std::size_t>::max()
			? std::numeric_limits<std::size_t>::max()
			: static_cast<std::size_t>(bytes);
	}

	std::size_t MultiwayBucketRegistry::mapped_artifact_bytes() const noexcept
	{
		return mapped_artifact_ == nullptr ? 0U : mapped_artifact_->size;
	}

	MultiwayBucketRegistryMemorySnapshot MultiwayBucketRegistry::memory_snapshot() const noexcept
	{
		MultiwayBucketRegistryMemorySnapshot result;
		result.owned_metadata.add_buffer(tables_.size(), tables_.capacity(), sizeof(MultiwayBucketTable));
		if (mapped_identity_storage_ != nullptr)
			result.owned_metadata.add_buffer(1U, 1U, sizeof(MultiwayModelIdentity));
		for (const auto& table : tables_)
		{
			if (table.owned_storage_bytes() != 0U)
				result.owned_metadata.add_buffer(1U, 1U, table.owned_storage_bytes());
			result.owned_metadata.add_buffer(
				table.owned_board_size(), table.owned_board_capacity(), sizeof(std::uint8_t));
			result.owned_metadata.add_buffer(
				table.assignments().size(), table.owned_assignment_capacity(), sizeof(std::uint32_t));
		}
		const auto mapped = mapped_artifact_bytes();
		result.mapping_virtual.live_element_bytes = mapped;
		result.mapping_virtual.retained_capacity_bytes = mapped;
		result.mapping_virtual.grouping_only = true;
		result.total = result.owned_metadata;
		result.total.accumulate(result.mapping_virtual);
		result.total.grouping_only = true;
		return result;
	}

} // namespace texas::solver::multiway
