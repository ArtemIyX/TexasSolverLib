#include "ranges/range.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace core {

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
    if (!(min_value <= max_value)) {
        throw std::invalid_argument("RangeVector::clamp requires min_value <= max_value");
    }
    for (auto& weight : weights) {
        weight = std::clamp(weight, min_value, max_value);
    }
}

void RangeVector::renormalize() {
    const auto total = sum();
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
    for (std::size_t i = 0; i < range.weights.size(); ++i) {
        if (!mask.allows(i)) {
            range.weights[i] = 0.0;
        }
    }
    range.renormalize();
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
    if (header.version != 1) {
        return false;
    }
    range.kind = header.kind;
    range.weights.resize(static_cast<std::size_t>(header.value_count));
    for (std::size_t i = 0; i < range.weights.size(); ++i) {
        if (!read_pod(in, range.weights[i])) {
            return false;
        }
    }
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
    if (header.version != 1) {
        return false;
    }
    mask.kind = header.kind;
    mask.enabled.resize(static_cast<std::size_t>(header.value_count));
    for (std::size_t i = 0; i < mask.enabled.size(); ++i) {
        if (!read_pod(in, mask.enabled[i])) {
            return false;
        }
    }
    return true;
}

bool save_range_file(const std::filesystem::path& path, const RangeVector& range) {
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
    return deserialize(in, range);
}

}  // namespace core
