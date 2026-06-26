#include "core/dcfr_vector_parallel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace core {

std::vector<ParallelChanceRange> derive_parallel_chance_ranges(
    const BettingTree& tree,
    const std::vector<std::size_t>& children) {
    if (children.empty()) {
        throw std::invalid_argument("chance node must have at least one child");
    }
    std::vector<ParallelChanceRange> ranges;
    ranges.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto start = children[i];
        const auto end = (i + 1 < children.size()) ? children[i + 1] : tree.nodes.size();
        if (start >= end) {
            throw std::logic_error("chance child ranges must be strictly increasing");
        }
        ranges.push_back({start, end, start});
    }
    return ranges;
}

bool parallel_chance_enabled() {
    const char* raw = std::getenv("CFR_RAYON_CHANCE");
    if (raw == nullptr) {
        return true;
    }
    std::string value(raw);
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !(value == "0" || value == "false" || value == "off");
}

}  // namespace core
