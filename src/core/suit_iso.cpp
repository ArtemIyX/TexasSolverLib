#include "core/suit_iso.hpp"

#include <algorithm>
#include <array>
#include <functional>

namespace core {

const ChanceCollapse* SuitIsoCache::get(std::size_t node_idx) const {
    if (node_idx >= nodes.size()) return nullptr;
    return nodes[node_idx] ? &*nodes[node_idx] : nullptr;
}

bool SuitIsoCache::is_active() const {
    return std::any_of(nodes.begin(), nodes.end(), [](const auto& v) { return v.has_value(); });
}

std::uint8_t apply_perm_to_card(const SuitPerm& perm, std::uint8_t card) {
    const auto rank = static_cast<std::uint8_t>(card >> 2U);
    const auto suit = static_cast<std::uint8_t>(card & 3U);
    return static_cast<std::uint8_t>(rank * 4U + perm[suit]);
}

bool fixes_board(const SuitPerm& perm, const std::vector<std::uint8_t>& prefix_board) {
    std::vector<std::uint8_t> relabeled;
    relabeled.reserve(prefix_board.size());
    for (const auto card : prefix_board) relabeled.push_back(apply_perm_to_card(perm, card));
    std::sort(relabeled.begin(), relabeled.end());
    auto original = prefix_board;
    std::sort(original.begin(), original.end());
    return relabeled == original;
}

std::vector<std::size_t> board_stabilizer(const std::vector<std::uint8_t>& prefix_board) {
    static constexpr std::array<SuitPerm, 24> PERMS = {{
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
    }};
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < PERMS.size(); ++i) {
        if (fixes_board(PERMS[i], prefix_board)) out.push_back(i);
    }
    return out;
}

std::vector<IsoClass> group_chance_children(
    const std::vector<std::uint8_t>& prefix_board,
    const std::vector<std::uint8_t>& dealt_cards) {
    (void)prefix_board;
    std::vector<IsoClass> classes;
    for (std::size_t i = 0; i < dealt_cards.size(); ++i) {
        classes.push_back(IsoClass{i, {{i, {0, 1, 2, 3}}}});
    }
    return classes;
}

std::vector<std::array<std::uint8_t, 2>> build_hole_index(
    const std::vector<std::array<std::uint8_t, 2>>& holes) {
    std::vector<std::array<std::uint8_t, 2>> out;
    out.reserve(holes.size());
    for (const auto& hole : holes) {
        auto sorted = hole;
        if (sorted[1] < sorted[0]) std::swap(sorted[0], sorted[1]);
        out.push_back(sorted);
    }
    return out;
}

std::optional<std::vector<std::uint32_t>> hand_index_permutation(
    const std::vector<std::array<std::uint8_t, 2>>& holes,
    const std::vector<std::array<std::uint8_t, 2>>& hole_index,
    const SuitPerm& rel_perm) {
    std::vector<std::uint32_t> sigma;
    sigma.reserve(holes.size());
    for (const auto& h : holes) {
        std::array<std::uint8_t, 2> image = {apply_perm_to_card(rel_perm, h[0]), apply_perm_to_card(rel_perm, h[1])};
        if (image[1] < image[0]) std::swap(image[0], image[1]);
        const auto it = std::find(hole_index.begin(), hole_index.end(), image);
        if (it == hole_index.end()) return std::nullopt;
        sigma.push_back(static_cast<std::uint32_t>(std::distance(hole_index.begin(), it)));
    }
    return sigma;
}

SuitIsoCache build_suit_iso_cache(
    const std::vector<FlatNode>& nodes,
    const std::vector<std::optional<std::uint8_t>>& dealt_cards,
    const std::vector<std::uint8_t>& initial_board,
    const std::array<std::vector<std::array<std::uint8_t, 2>>, 2>& holes,
    const std::array<const std::vector<double>*, 2>& reach) {
    (void)dealt_cards;
    (void)initial_board;
    (void)holes;
    (void)reach;

    SuitIsoCache cache;
    cache.nodes.resize(nodes.size());
    return cache;
}

std::vector<bool> member_skip_mask(const std::vector<FlatNode>& nodes, const SuitIsoCache&) {
    return std::vector<bool>(nodes.size(), false);
}

}  // namespace core
