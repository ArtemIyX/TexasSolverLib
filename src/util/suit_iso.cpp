#include "util/suit_iso.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <unordered_map>

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
    const auto stabilizer_idx = board_stabilizer(prefix_board);
    static constexpr std::array<SuitPerm, 24> PERMS = {{
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
    }};
    std::vector<SuitPerm> stabilizer;
    stabilizer.reserve(stabilizer_idx.size());
    for (auto idx : stabilizer_idx) stabilizer.push_back(PERMS[idx]);

    std::unordered_map<std::uint8_t, std::size_t> card_to_child;
    for (std::size_t i = 0; i < dealt_cards.size(); ++i) card_to_child.emplace(dealt_cards[i], i);

    std::vector<bool> assigned(dealt_cards.size(), false);
    std::vector<IsoClass> classes;
    for (std::size_t rep_idx = 0; rep_idx < dealt_cards.size(); ++rep_idx) {
        if (assigned[rep_idx]) continue;
        const auto rep_card = dealt_cards[rep_idx];
        std::vector<std::pair<std::size_t, SuitPerm>> members;
        for (const auto& perm : stabilizer) {
            const auto image = apply_perm_to_card(perm, rep_card);
            const auto it = card_to_child.find(image);
            if (it != card_to_child.end() && !assigned[it->second]) {
                assigned[it->second] = true;
                members.push_back({it->second, perm});
            }
        }
        std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& m : members) {
            if (m.first == rep_idx) {
                m.second = {0, 1, 2, 3};
            }
        }
        classes.push_back(IsoClass{rep_idx, std::move(members)});
    }
    return classes;
}

std::vector<PublicChanceClass> canonicalize_public_chance_outcomes(
    const std::vector<std::uint8_t>& prefix_board,
    const std::vector<ChanceOutcome>& outcomes) {
    std::vector<std::uint8_t> dealt_cards;
    dealt_cards.reserve(outcomes.size());
    for (const auto& outcome : outcomes) {
        dealt_cards.push_back(static_cast<std::uint8_t>(outcome.action));
    }

    const auto iso_classes = group_chance_children(prefix_board, dealt_cards);
    std::vector<PublicChanceClass> classes;
    classes.reserve(iso_classes.size());
    for (const auto& iso_class : iso_classes) {
        PublicChanceClass collapsed;
        collapsed.representative_outcome_idx = iso_class.representative_child_idx;
        collapsed.representative_action =
            static_cast<std::uint8_t>(outcomes[iso_class.representative_child_idx].action);
        collapsed.multiplicity = static_cast<std::uint32_t>(iso_class.members.size());
        for (const auto& [member_idx, rel_perm] : iso_class.members) {
            (void)rel_perm;
            collapsed.probability += outcomes[member_idx].probability;
        }
        classes.push_back(collapsed);
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
    const auto hole_index = std::array{
        build_hole_index(holes[0]),
        build_hole_index(holes[1]),
    };

    static constexpr std::array<SuitPerm, 24> PERMS = {{
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
    }};

    auto reach_is_symmetric = [](const std::vector<double>& r, const std::vector<std::uint32_t>& sigma) {
        if (r.size() != sigma.size()) return false;
        for (std::size_t i = 0; i < r.size(); ++i) {
            if (r[i] != r[sigma[i]]) return false;
        }
        return true;
    };

    auto hand_perm = [&](std::size_t p, const SuitPerm& perm) {
        return hand_index_permutation(holes[p], hole_index[p], perm);
    };

    const auto root_stab_idx = board_stabilizer(initial_board);
    std::vector<SuitPerm> root_stab;
    for (auto idx : root_stab_idx) root_stab.push_back(PERMS[idx]);
    for (std::size_t p = 0; p < 2; ++p) {
        for (const auto& perm : root_stab) {
            const auto sigma = hand_perm(p, perm);
            if (!sigma || !reach_is_symmetric(*reach[p], *sigma)) {
                SuitIsoCache cache;
                cache.nodes.assign(nodes.size(), std::nullopt);
                return cache;
            }
        }
    }

    std::vector<std::optional<std::vector<std::uint8_t>>> prefix_at(nodes.size());
    std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> stack;
    stack.push_back({0, initial_board});
    while (!stack.empty()) {
        auto [idx, board] = std::move(stack.back());
        stack.pop_back();
        const auto& node = nodes[idx];
        if (node.tag == FlatNodeTag::Chance) {
            prefix_at[idx] = board;
            for (auto child : node.children) {
                auto child_board = board;
                if (child < dealt_cards.size() && dealt_cards[child].has_value()) {
                    child_board.push_back(*dealt_cards[child]);
                }
                stack.push_back({child, std::move(child_board)});
            }
        } else if (node.tag == FlatNodeTag::Decision) {
            for (auto child : node.children) {
                stack.push_back({child, board});
            }
        }
    }

    std::vector<std::optional<ChanceCollapse>> out(nodes.size());
    for (std::size_t idx = 0; idx < nodes.size(); ++idx) {
        const auto& node = nodes[idx];
        if (node.tag != FlatNodeTag::Chance || node.children.size() < 2) continue;
        if (!prefix_at[idx]) continue;

        std::vector<std::uint8_t> child_dealt;
        child_dealt.reserve(node.children.size());
        bool complete = true;
        for (auto child : node.children) {
            if (child >= dealt_cards.size() || !dealt_cards[child].has_value()) {
                complete = false;
                break;
            }
            child_dealt.push_back(*dealt_cards[child]);
        }
        if (!complete) continue;

        const auto iso_classes = group_chance_children(*prefix_at[idx], child_dealt);
        bool symmetric = true;
        std::vector<CollapseClass> classes;
        classes.reserve(iso_classes.size());
        for (const auto& iso : iso_classes) {
            std::vector<CollapseMember> members;
            members.reserve(iso.members.size());
            for (const auto& [member_local_idx, rel_perm] : iso.members) {
                const auto sigma0 = hand_perm(0, rel_perm);
                const auto sigma1 = hand_perm(1, rel_perm);
                const bool closed_and_symmetric =
                    sigma0.has_value() && sigma1.has_value() &&
                    reach_is_symmetric(*reach[0], *sigma0) &&
                    reach_is_symmetric(*reach[1], *sigma1);
                if (!closed_and_symmetric) symmetric = false;
                std::vector<std::uint32_t> id0(holes[0].size());
                std::vector<std::uint32_t> id1(holes[1].size());
                for (std::uint32_t i = 0; i < id0.size(); ++i) id0[i] = i;
                for (std::uint32_t i = 0; i < id1.size(); ++i) id1[i] = i;
                members.push_back(CollapseMember{
                    node.children[member_local_idx],
                    {sigma0.value_or(id0), sigma1.value_or(id1)},
                    rel_perm,
                });
            }
            std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
                return a.child_idx < b.child_idx;
            });
            classes.push_back(CollapseClass{node.children[iso.representative_child_idx], std::move(members)});
        }
        out[idx] = ChanceCollapse{std::move(classes), symmetric};
    }

    SuitIsoCache cache;
    cache.nodes = std::move(out);
    return cache;
}

std::vector<bool> member_skip_mask(const std::vector<FlatNode>& nodes, const SuitIsoCache& cache) {
    std::vector<bool> skip(nodes.size(), false);
    if (!cache.is_active()) return skip;

    std::vector<std::pair<std::size_t, bool>> stack;
    stack.push_back({0, true});
    while (!stack.empty()) {
        const auto [idx, kept] = stack.back();
        stack.pop_back();
        skip[idx] = !kept;
        const auto& node = nodes[idx];
        if (node.tag == FlatNodeTag::Chance) {
            std::set<std::size_t> reps;
            if (const auto* collapse = cache.get(idx); collapse && collapse->symmetric) {
                for (const auto& cl : collapse->classes) reps.insert(cl.representative_child_idx);
            } else {
                for (auto child : node.children) reps.insert(child);
            }
            for (auto child : node.children) {
                stack.push_back({child, kept && reps.find(child) != reps.end()});
            }
        } else if (node.tag == FlatNodeTag::Decision) {
            for (auto child : node.children) stack.push_back({child, kept});
        }
    }
    return skip;
}

}  // namespace core


