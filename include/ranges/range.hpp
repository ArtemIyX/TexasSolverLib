#pragma once

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <iosfwd>
#include <string>
#include <vector>

namespace core {

/**
 * @brief Dense probability vector over private-hand combos or abstraction buckets.
 *
 * This is the canonical range container for solver inputs, cached priors, and
 * exported equilibrium ranges.
 */
struct RangeVector {
    enum class Kind : std::uint8_t {
        Combo = 0,
        Bucket = 1,
    };

    Kind kind = Kind::Combo;
    std::vector<Probability> weights;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] Probability sum() const noexcept;

    void normalize();
    void clamp(Probability min_value, Probability max_value);
    void renormalize();
};

/**
 * @brief Boolean legality mask for dead cards, blockers, or abstraction filters.
 */
struct RangeMask {
    RangeVector::Kind kind = RangeVector::Kind::Combo;
    std::vector<std::uint8_t> enabled;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool allows(std::size_t index) const noexcept;
};

RangeVector masked_copy(const RangeVector& range, const RangeMask& mask);
void apply_mask(RangeVector& range, const RangeMask& mask);
RangeMask combine_masks(const RangeMask& lhs, const RangeMask& rhs);

/**
 * @brief Binary serialization header for range cache files.
 */
struct RangeFileHeader {
    std::array<char, 8> magic{{'T', 'S', 'R', 'N', 'G', '0', '0', '1'}};
    std::uint8_t version = 1;
    RangeVector::Kind kind = RangeVector::Kind::Combo;
    std::uint64_t value_count = 0;
};

void serialize(std::ostream& out, const RangeVector& range);
bool deserialize(std::istream& in, RangeVector& range);
void serialize(std::ostream& out, const RangeMask& mask);
bool deserialize(std::istream& in, RangeMask& mask);

bool save_range_file(const std::filesystem::path& path, const RangeVector& range);
bool load_range_file(const std::filesystem::path& path, RangeVector& range);

}  // namespace core
