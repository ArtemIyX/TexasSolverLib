#include "solver/multiway_bucket_model.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace core {
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
        if (cards[index] >= 52U) return false;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (cards[index] == cards[prior]) return false;
        }
    }
    return true;
}

std::array<std::uint8_t, 2> compact_hole(const std::array<std::uint8_t, 2>& hole) {
    std::array<std::uint8_t, 2> result = hole;
    for (auto& card : result) {
        if (card >= 52U && is_valid_card(card)) {
            card = static_cast<std::uint8_t>(card - 8U);
        }
    }
    if (!are_valid_compact_cards(result.data(), result.size())) {
        throw std::invalid_argument("multiway bucket lookup requires valid hole cards");
    }
    return result;
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
    if (canonical_board_.size() != expected_board_count(street_) ||
        !are_valid_compact_cards(canonical_board_.data(), canonical_board_.size()) ||
        bucket_count_ == 0U || assignments_.size() != MULTIWAY_HOLE_COMBINATION_COUNT) {
        throw std::invalid_argument("multiway bucket table has invalid metadata");
    }
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            const auto assignment = assignments_[hole_index(hole)];
            if (is_live_hole(canonical_board_, hole)) {
                if (assignment >= bucket_count_) {
                    throw std::invalid_argument("multiway bucket table omits a live hole-card pair");
                }
            } else if (assignment != MULTIWAY_INVALID_BUCKET) {
                throw std::invalid_argument("multiway bucket table assigns a board-blocked hole-card pair");
            }
        }
    }
}

std::uint32_t MultiwayBucketTable::lookup(const std::array<std::uint8_t, 2>& hole) const {
    const auto compact = compact_hole(hole);
    if (!is_live_hole(canonical_board_, hole)) {
        throw std::invalid_argument("multiway bucket lookup requires a live distinct hole-card pair");
    }
    const auto bucket = assignments_[hole_index(compact)];
    if (bucket >= bucket_count_) {
        throw std::logic_error("multiway bucket table has no assignment for a live hole-card pair");
    }
    return bucket;
}

std::size_t MultiwayBucketTable::hole_index(const std::array<std::uint8_t, 2>& hole) {
    if (!are_valid_compact_cards(hole.data(), hole.size())) {
        throw std::invalid_argument("multiway bucket hole index requires distinct valid cards");
    }
    const auto low = std::min(hole[0], hole[1]);
    const auto high = std::max(hole[0], hole[1]);
    return static_cast<std::size_t>(low) * (103U - low) / 2U + (high - low - 1U);
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

    auto compact_board = canonical_board;
    for (auto& card : compact_board) {
        if (card >= 52U && is_valid_card(card)) card = static_cast<std::uint8_t>(card - 8U);
    }
    const auto compact_found = std::lower_bound(
        tables_.begin(), tables_.end(), std::pair<Street, const std::vector<std::uint8_t>&>{street, compact_board},
        [](const MultiwayBucketTable& candidate,
           const std::pair<Street, const std::vector<std::uint8_t>&>& key) {
            if (candidate.street() != key.first) return candidate.street() < key.first;
            return candidate.canonical_board() < key.second;
        });
    if (compact_found == tables_.end() || compact_found->street() != street ||
        compact_found->canonical_board() != compact_board) {
        throw std::out_of_range("multiway bucket registry has no table for the canonical board");
    }
    return *compact_found;
}

std::uint32_t MultiwayBucketRegistry::lookup(
    Street street,
    const std::vector<std::uint8_t>& canonical_board,
    const std::array<std::uint8_t, 2>& hole) const {
    return table(street, canonical_board).lookup(hole);
}

}  // namespace core
