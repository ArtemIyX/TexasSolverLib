#pragma once

#include "core/legacy_namespace_compat.hpp"
#include "core/poker.hpp"

#include "core/canonical_combo.hpp"
#include "solver/multiway_model_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_INVALID_BUCKET = 0xffffffffU;
inline constexpr std::size_t MULTIWAY_HOLE_COMBINATION_COUNT = CANONICAL_HOLE_COMBINATION_COUNT;

// Immutable bucket assignments for one canonical postflop board. Entries use
// the fixed unordered-card index; board-blocked hands retain INVALID_BUCKET.
class MultiwayBucketTable {
public:
    MultiwayBucketTable(
        MultiwayModelIdentity identity,
        Street street,
        std::vector<std::uint8_t> canonical_board,
        std::uint32_t bucket_count,
        std::vector<std::uint32_t> assignments);

    [[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] Street street() const noexcept { return street_; }
    [[nodiscard]] const std::vector<std::uint8_t>& canonical_board() const noexcept {
        return canonical_board_;
    }
    [[nodiscard]] std::uint32_t bucket_count() const noexcept { return bucket_count_; }
    // Stable runtime-only identity of this table's model, board, and assignments.
    [[nodiscard]] std::uint64_t table_identity() const noexcept { return table_identity_; }
    [[nodiscard]] const std::vector<std::uint32_t>& assignments() const noexcept {
        return assignments_;
    }

    // Cards are always compact deck indices in [0, 51].
    [[nodiscard]] std::uint32_t lookup(const std::array<std::uint8_t, 2>& hole) const;
    [[nodiscard]] static std::size_t hole_index(const std::array<std::uint8_t, 2>& hole);

private:
    MultiwayModelIdentity identity_{};
    Street street_ = Street::Preflop;
    std::vector<std::uint8_t> canonical_board_;
    std::uint32_t bucket_count_ = 0;
    std::uint64_t table_identity_ = 0U;
    std::vector<std::uint32_t> assignments_;
};

// Sorted immutable board registry. Lookup uses binary search, not a hash map,
// so traversal can resolve a board/bucket pair without a hot-path allocation.
class MultiwayBucketRegistry {
public:
    explicit MultiwayBucketRegistry(std::vector<MultiwayBucketTable> tables);

    [[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const std::vector<MultiwayBucketTable>& tables() const noexcept {
        return tables_;
    }
    // Cards are always compact deck indices in [0, 51].
    [[nodiscard]] const MultiwayBucketTable& table(
        Street street,
        const std::vector<std::uint8_t>& canonical_board) const;
    [[nodiscard]] std::uint32_t lookup(
        Street street,
        const std::vector<std::uint8_t>& canonical_board,
        const std::array<std::uint8_t, 2>& hole) const;

private:
    MultiwayModelIdentity identity_{};
    std::vector<MultiwayBucketTable> tables_;
};

}  // namespace texas::solver::multiway
