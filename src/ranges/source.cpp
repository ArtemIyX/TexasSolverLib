#include "ranges/source.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace texas::ranges {

namespace {

RangeMask make_full_mask(std::size_t value_count, RangeVector::Kind kind) {
    RangeMask mask;
    mask.kind = kind;
    mask.enabled.assign(value_count, 1U);
    return mask;
}

RangeVector make_uniform_vector(std::size_t value_count, RangeVector::Kind kind) {
    RangeVector range;
    range.kind = kind;
    range.weights.assign(value_count, value_count == 0 ? 0.0 : 1.0 / static_cast<Probability>(value_count));
    return range;
}

}  // namespace

UniformRangeSource::UniformRangeSource(std::size_t value_count, RangeVector::Kind kind)
    : value_count_(value_count), kind_(kind) {}

CanonicalRange UniformRangeSource::load() const {
    return make_uniform_canonical_range(value_count_, kind_);
}

FileRangeSource::FileRangeSource(
    RangeSourceKind kind,
    std::filesystem::path path,
    RangeVector::Kind value_kind)
    : kind_(kind), path_(std::move(path)), value_kind_(value_kind) {}

CanonicalRange FileRangeSource::load() const {
    CanonicalRange out;
    out.source_kind = kind_;
    out.context.source_path = path_;
    out.range.kind = value_kind_;
    if (!load_range_file(path_, out.range)) {
        throw std::runtime_error("failed to load range file: " + path_.string());
    }
    out.mask = make_full_mask(out.range.size(), out.range.kind);
    apply_mask(out.range, out.mask);
    return out;
}

ChartRangeSource::ChartRangeSource(
    std::vector<Entry> entries,
    std::size_t value_count,
    RangeVector::Kind kind)
    : entries_(std::move(entries)), value_count_(value_count), kind_(kind) {}

CanonicalRange ChartRangeSource::load() const {
    throw std::logic_error(
        "ChartRangeSource requires an explicit label-to-combo or "
        "label-to-bucket mapping; use indexed canonical values or a range file");
}

CanonicalRange make_uniform_canonical_range(
    std::size_t value_count,
    RangeVector::Kind kind) {
    CanonicalRange out;
    out.range = make_uniform_vector(value_count, kind);
    out.mask = make_full_mask(value_count, kind);
    out.source_kind = RangeSourceKind::UniformPrior;
    out.range.normalize();
    return out;
}

CanonicalRange load_canonical_range_from_file(
    const std::filesystem::path& path,
    RangeSourceKind kind,
    RangeVector::Kind value_kind) {
    FileRangeSource source(kind, path, value_kind);
    return source.load();
}

CanonicalRange make_canonical_range_from_values(
    RangeSourceKind kind,
    RangeVector range,
    RangeMask mask) {
    if (mask.empty()) {
        mask = make_full_mask(range.size(), range.kind);
    }
    apply_mask(range, mask);
    CanonicalRange out;
    out.range = std::move(range);
    out.mask = std::move(mask);
    out.source_kind = kind;
    return out;
}

}  // namespace texas::ranges
