#pragma once

#include "games/hunl.hpp"
#include "ranges/range.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

/**
 * @brief Solver-agnostic description of where a range came from.
 */
enum class RangeSourceKind : std::uint8_t {
    UniformPrior = 0,
    PreflopChart = 1,
    SolverExport = 2,
    CachedFile = 3,
};

/**
 * @brief Optional context describing how a range should be interpreted.
 */
struct RangeSourceContext {
    std::optional<std::filesystem::path> source_path;
    std::optional<Street> street;
    std::optional<std::string> label;
};

/**
 * @brief Canonical range payload returned by every source.
 */
struct CanonicalRange {
    RangeVector range;
    RangeMask mask;
    RangeSourceKind source_kind = RangeSourceKind::UniformPrior;
    RangeSourceContext context;
};

/**
 * @brief Abstract loader for building canonical ranges.
 */
class IRangeSource {
public:
    virtual ~IRangeSource() = default;
    virtual CanonicalRange load() const = 0;
};

/**
 * @brief Uniform prior range source.
 */
class UniformRangeSource final : public IRangeSource {
public:
    UniformRangeSource(std::size_t value_count, RangeVector::Kind kind = RangeVector::Kind::Combo);
    CanonicalRange load() const override;

private:
    std::size_t value_count_ = 0;
    RangeVector::Kind kind_ = RangeVector::Kind::Combo;
};

/**
 * @brief Generic file-backed range source.
 *
 * This is solver-independent and can be used for:
 * - preflop charts
 * - solver exports
 * - cached files
 */
class FileRangeSource final : public IRangeSource {
public:
    FileRangeSource(
        RangeSourceKind kind,
        std::filesystem::path path,
        RangeVector::Kind value_kind = RangeVector::Kind::Combo);

    CanonicalRange load() const override;

private:
    RangeSourceKind kind_ = RangeSourceKind::CachedFile;
    std::filesystem::path path_;
    RangeVector::Kind value_kind_ = RangeVector::Kind::Combo;
};

/**
 * @brief In-memory chart source.
 */
class ChartRangeSource final : public IRangeSource {
public:
    using Entry = std::pair<std::string, Probability>;

    ChartRangeSource(
        std::vector<Entry> entries,
        std::size_t value_count,
        RangeVector::Kind kind = RangeVector::Kind::Combo);

    CanonicalRange load() const override;

private:
    std::vector<Entry> entries_;
    std::size_t value_count_ = 0;
    RangeVector::Kind kind_ = RangeVector::Kind::Combo;
};

CanonicalRange make_uniform_canonical_range(
    std::size_t value_count,
    RangeVector::Kind kind = RangeVector::Kind::Combo);
CanonicalRange load_canonical_range_from_file(
    const std::filesystem::path& path,
    RangeSourceKind kind,
    RangeVector::Kind value_kind = RangeVector::Kind::Combo);
CanonicalRange make_canonical_range_from_values(
    RangeSourceKind kind,
    RangeVector range,
    RangeMask mask = {});

}  // namespace core
