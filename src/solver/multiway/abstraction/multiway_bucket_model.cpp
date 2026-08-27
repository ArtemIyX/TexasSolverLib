#include "solver/multiway/abstraction/multiway_bucket_model.hpp"

#include "core/canonical_combo.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {

using core::CanonicalComboId;
using core::Street;
using core::canonical_combos;
using core::is_card_index;
namespace {

std::size_t expected_board_count(Street street) {
    switch (street) {
        case Street::Flop: return 3U;
        case Street::Turn: return 4U;
        case Street::River: return 5U;
        default: break;
    }
    throw std::invalid_argument("multiway bucket table requires a postflop street");
}

bool are_valid_compact_cards(const std::uint8_t* cards, std::size_t count) noexcept {
    if (cards == nullptr && count != 0U) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (!is_card_index(cards[index])) return false;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (cards[index] == cards[prior]) return false;
        }
    }
    return true;
}

std::uint64_t stable_table_identity(
    const MultiwayModelIdentity& identity,
    Street street,
    const std::vector<std::uint8_t>& canonical_board,
    std::uint32_t bucket_count,
    const std::vector<std::uint32_t>& assignments) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    const auto append = [&hash](std::uint64_t value) noexcept {
        texas::core::fingerprint::append_u64(hash, value);
    };
    append(identity.combined_hash);
    append(static_cast<std::uint8_t>(street));
    append(canonical_board.size());
    for (const auto card : canonical_board) append(card);
    append(bucket_count);
    for (const auto assignment : assignments) append(assignment);
    return hash;
}

bool is_live_hole(const std::vector<std::uint8_t>& board, const std::array<std::uint8_t, 2>& hole) {
    return std::find(board.begin(), board.end(), hole[0]) == board.end() &&
           std::find(board.begin(), board.end(), hole[1]) == board.end();
}

}  // namespace

MultiwayBucketTable::MultiwayBucketTable(
    MultiwayModelIdentity identity,
    Street street,
    std::vector<std::uint8_t> canonical_board,
    std::uint32_t bucket_count,
    std::vector<std::uint32_t> assignments)
    : identity_(identity),
      street_(street),
      canonical_board_(std::move(canonical_board)),
      bucket_count_(bucket_count),
      assignments_(std::move(assignments)) {
    identity_.validate();
    std::sort(canonical_board_.begin(), canonical_board_.end());
    if (canonical_board_.size() != expected_board_count(street_) ||
        !are_valid_compact_cards(canonical_board_.data(), canonical_board_.size()) ||
        bucket_count_ == 0U || assignments_.size() != MULTIWAY_HOLE_COMBINATION_COUNT) {
        throw std::invalid_argument("multiway bucket table has invalid metadata");
    }
    for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id) {
        const auto& hole = canonical_combos().cards(id);
        const auto assignment = assignments_[id];
        if (is_live_hole(canonical_board_, hole)) {
            if (assignment >= bucket_count_) {
                throw std::invalid_argument("multiway bucket table omits a live hole-card pair");
            }
        } else if (assignment != MULTIWAY_INVALID_BUCKET) {
            throw std::invalid_argument("multiway bucket table assigns a board-blocked hole-card pair");
        }
    }
    table_identity_ = stable_table_identity(
        identity_, street_, canonical_board_, bucket_count_, assignments_);
}

std::uint32_t MultiwayBucketTable::lookup(const std::array<std::uint8_t, 2>& hole) const {
    if (!is_live_hole(canonical_board_, hole)) {
        throw std::invalid_argument("multiway bucket lookup requires a live distinct hole-card pair");
    }
    const auto bucket = assignments_[hole_index(hole)];
    if (bucket >= bucket_count_) {
        throw std::logic_error("multiway bucket table has no assignment for a live hole-card pair");
    }
    return bucket;
}

std::size_t MultiwayBucketTable::hole_index(const std::array<std::uint8_t, 2>& hole) {
    if (!are_valid_compact_cards(hole.data(), hole.size())) {
        throw std::invalid_argument("multiway bucket hole index requires distinct valid cards");
    }
    return canonical_combos().id(hole);
}

MultiwayBucketRegistry::MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables)
    : tables_(std::move(tables)) {
    if (tables_.empty()) throw std::invalid_argument("multiway bucket registry requires at least one table");
    identity_ = tables_.front().identity();
    identity_.validate();
    for (const auto& table : tables_) {
        if (table.identity() != identity_) {
            throw std::invalid_argument("multiway bucket registry has mixed model identities");
        }
    }
    std::sort(tables_.begin(), tables_.end(), [](const MultiwayBucketTable& left,
                                                  const MultiwayBucketTable& right) {
        if (left.street() != right.street()) return left.street() < right.street();
        return left.canonical_board() < right.canonical_board();
    });
    for (std::size_t index = 1; index < tables_.size(); ++index) {
        if (tables_[index - 1U].street() == tables_[index].street() &&
            tables_[index - 1U].canonical_board() == tables_[index].canonical_board()) {
            throw std::invalid_argument("multiway bucket registry has duplicate board tables");
        }
    }
}

const MultiwayBucketTable& MultiwayBucketRegistry::table(
    Street street,
    const std::vector<std::uint8_t>& canonical_board) const {
    const auto found = std::lower_bound(
        tables_.begin(), tables_.end(), std::pair<Street, const std::vector<std::uint8_t>&>{street, canonical_board},
        [](const MultiwayBucketTable& candidate,
           const std::pair<Street, const std::vector<std::uint8_t>&>& key) {
            if (candidate.street() != key.first) return candidate.street() < key.first;
            return candidate.canonical_board() < key.second;
        });
    if (found != tables_.end() && found->street() == street &&
        found->canonical_board() == canonical_board) {
        return *found;
    }

    throw std::out_of_range("multiway bucket registry has no table for the canonical compact board");
}

std::uint32_t MultiwayBucketRegistry::lookup(
    Street street,
    const std::vector<std::uint8_t>& canonical_board,
    const std::array<std::uint8_t, 2>& hole) const {
    return table(street, canonical_board).lookup(hole);
}

}  // namespace texas::solver::multiway
