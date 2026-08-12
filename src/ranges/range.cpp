#include "ranges/range.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <new>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace texas::ranges {

namespace {

template <class T>
void write_pod(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T>
bool read_pod(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

}  // namespace

bool RangeVector::empty() const noexcept {
    return weights.empty();
}

std::size_t RangeVector::size() const noexcept {
    return weights.size();
}

Probability RangeVector::sum() const noexcept {
    Probability total = 0.0;
    for (const auto weight : weights) {
        total += weight;
    }
    return total;
}

void RangeVector::normalize() {
    renormalize();
}

void RangeVector::clamp(Probability min_value, Probability max_value) {
    if (!std::isfinite(min_value) ||
        !std::isfinite(max_value) ||
        !(min_value <= max_value)) {
        throw std::invalid_argument(
            "RangeVector::clamp requires finite ordered bounds");
    }
    for (const auto weight : weights) {
        if (!std::isfinite(weight)) {
            throw std::invalid_argument(
                "RangeVector::clamp requires finite weights");
        }
    }
    for (auto& weight : weights) {
        weight = std::clamp(weight, min_value, max_value);
    }
}

void RangeVector::renormalize() {
    Probability total = 0.0;
    for (const auto weight : weights) {
        if (!std::isfinite(weight) || weight < 0.0) {
            throw std::invalid_argument(
                "RangeVector::renormalize requires finite non-negative weights");
        }
        total += weight;
    }
    if (!std::isfinite(total)) {
        throw std::invalid_argument(
            "RangeVector::renormalize requires finite total mass");
    }
    if (!(total > 0.0)) {
        if (!weights.empty()) {
            const auto uniform = 1.0 / static_cast<Probability>(weights.size());
            std::fill(weights.begin(), weights.end(), uniform);
        }
        return;
    }
    for (auto& weight : weights) {
        weight /= total;
    }
}

bool RangeMask::empty() const noexcept {
    return enabled.empty();
}

std::size_t RangeMask::size() const noexcept {
    return enabled.size();
}

bool RangeMask::allows(std::size_t index) const noexcept {
    return index < enabled.size() && enabled[index] != 0;
}

RangeVector masked_copy(const RangeVector& range, const RangeMask& mask) {
    if (range.kind != mask.kind) {
        throw std::invalid_argument("masked_copy requires matching range kinds");
    }
    if (range.size() != mask.size()) {
        throw std::invalid_argument("masked_copy requires matching vector sizes");
    }
    RangeVector out = range;
    apply_mask(out, mask);
    return out;
}

void apply_mask(RangeVector& range, const RangeMask& mask) {
    if (range.kind != mask.kind) {
        throw std::invalid_argument("apply_mask requires matching range kinds");
    }
    if (range.size() != mask.size()) {
        throw std::invalid_argument("apply_mask requires matching vector sizes");
    }
    Probability surviving_mass = 0.0;
    for (std::size_t i = 0; i < range.weights.size(); ++i) {
        const auto weight = range.weights[i];
        if (!std::isfinite(weight) || weight < 0.0) {
            throw std::invalid_argument(
                "apply_mask requires finite non-negative weights");
        }
        if (mask.allows(i)) {
            surviving_mass += weight;
        }
    }
    if (!std::isfinite(surviving_mass)) {
        throw std::invalid_argument("apply_mask requires finite surviving mass");
    }
    if (!range.weights.empty() && !(surviving_mass > 0.0)) {
        throw std::invalid_argument("apply_mask removed all range mass");
    }
    if (range.weights.empty()) {
        return;
    }
    for (std::size_t i = 0; i < range.weights.size(); ++i) {
        range.weights[i] = mask.allows(i)
            ? range.weights[i] / surviving_mass
            : 0.0;
    }
}

RangeMask combine_masks(const RangeMask& lhs, const RangeMask& rhs) {
    if (lhs.kind != rhs.kind) {
        throw std::invalid_argument("combine_masks requires matching mask kinds");
    }
    if (lhs.size() != rhs.size()) {
        throw std::invalid_argument("combine_masks requires matching mask sizes");
    }
    RangeMask out;
    out.kind = lhs.kind;
    out.enabled.resize(lhs.size(), 0);
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        out.enabled[i] = static_cast<std::uint8_t>(lhs.allows(i) && rhs.allows(i));
    }
    return out;
}

void serialize(std::ostream& out, const RangeVector& range) {
    RangeFileHeader header;
    header.kind = range.kind;
    header.value_count = static_cast<std::uint64_t>(range.weights.size());
    out.write(header.magic.data(), static_cast<std::streamsize>(header.magic.size()));
    write_pod(out, header.version);
    write_pod(out, header.kind);
    write_pod(out, header.value_count);
    for (const auto weight : range.weights) {
        write_pod(out, weight);
    }
}

bool deserialize(std::istream& in, RangeVector& range) {
    RangeFileHeader header;
    std::array<char, 8> magic{};
    if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size()))) {
        return false;
    }
    if (magic != header.magic ||
        !read_pod(in, header.version) ||
        !read_pod(in, header.kind) ||
        !read_pod(in, header.value_count)) {
        return false;
    }
    if (header.version != 1 ||
        (header.kind != RangeVector::Kind::Combo &&
         header.kind != RangeVector::Kind::Bucket) ||
        header.value_count > MAX_SERIALIZED_RANGE_VALUES) {
        return false;
    }
    RangeVector decoded;
    decoded.kind = header.kind;
    try {
        decoded.weights.resize(static_cast<std::size_t>(header.value_count));
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    for (auto& weight : decoded.weights) {
        if (!read_pod(in, weight) ||
            !std::isfinite(weight) ||
            weight < 0.0) {
            return false;
        }
    }
    range = std::move(decoded);
    return true;
}

void serialize(std::ostream& out, const RangeMask& mask) {
    RangeFileHeader header;
    header.kind = mask.kind;
    header.value_count = static_cast<std::uint64_t>(mask.enabled.size());
    out.write(header.magic.data(), static_cast<std::streamsize>(header.magic.size()));
    write_pod(out, header.version);
    write_pod(out, header.kind);
    write_pod(out, header.value_count);
    for (const auto enabled : mask.enabled) {
        write_pod(out, enabled);
    }
}

bool deserialize(std::istream& in, RangeMask& mask) {
    RangeFileHeader header;
    std::array<char, 8> magic{};
    if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size()))) {
        return false;
    }
    if (magic != header.magic ||
        !read_pod(in, header.version) ||
        !read_pod(in, header.kind) ||
        !read_pod(in, header.value_count)) {
        return false;
    }
    if (header.version != 1 ||
        (header.kind != RangeVector::Kind::Combo &&
         header.kind != RangeVector::Kind::Bucket) ||
        header.value_count > MAX_SERIALIZED_RANGE_VALUES) {
        return false;
    }
    RangeMask decoded;
    decoded.kind = header.kind;
    try {
        decoded.enabled.resize(static_cast<std::size_t>(header.value_count));
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    for (auto& enabled : decoded.enabled) {
        if (!read_pod(in, enabled) || enabled > 1U) {
            return false;
        }
    }
    mask = std::move(decoded);
    return true;
}

bool save_range_file(const std::filesystem::path& path, const RangeVector& range) {
    if ((range.kind != RangeVector::Kind::Combo &&
         range.kind != RangeVector::Kind::Bucket) ||
        range.weights.size() > MAX_SERIALIZED_RANGE_VALUES) {
        return false;
    }
    for (const auto weight : range.weights) {
        if (!std::isfinite(weight) || weight < 0.0) {
            return false;
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    serialize(out, range);
    return static_cast<bool>(out);
}

bool load_range_file(const std::filesystem::path& path, RangeVector& range) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    RangeVector decoded;
    if (!deserialize(in, decoded) ||
        in.peek() != std::char_traits<char>::eof()) {
        return false;
    }
    range = std::move(decoded);
    return true;
}

}  // namespace texas::ranges
