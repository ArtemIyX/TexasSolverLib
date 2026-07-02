#pragma once

#include "solver/exploit.hpp"
#include "games/hunl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace core {

using SuitPerm = std::array<std::uint8_t, 4>;

struct IsoClass {
    std::size_t representative_child_idx = 0;
    std::vector<std::pair<std::size_t, SuitPerm>> members;
};

struct PublicChanceClass {
    std::size_t representative_outcome_idx = 0;
    std::uint8_t representative_action = 0;
    double probability = 0.0;
    std::uint32_t multiplicity = 0;
};

struct CollapseMember {
    std::size_t child_idx = 0;
    std::array<std::vector<std::uint32_t>, 2> sigma;
    SuitPerm rel_perm = {0, 1, 2, 3};
};

struct CollapseClass {
    std::size_t representative_child_idx = 0;
    std::vector<CollapseMember> members;
};

struct ChanceCollapse {
    std::vector<CollapseClass> classes;
    bool symmetric = false;
};

struct SuitIsoCache {
    std::vector<std::optional<ChanceCollapse>> nodes;

    const ChanceCollapse* get(std::size_t node_idx) const;
    bool is_active() const;
};

std::uint8_t apply_perm_to_card(const SuitPerm& perm, std::uint8_t card);
bool fixes_board(const SuitPerm& perm, const std::vector<std::uint8_t>& prefix_board);
std::vector<std::size_t> board_stabilizer(const std::vector<std::uint8_t>& prefix_board);
std::vector<IsoClass> group_chance_children(
    const std::vector<std::uint8_t>& prefix_board,
    const std::vector<std::uint8_t>& dealt_cards);
std::vector<PublicChanceClass> canonicalize_public_chance_outcomes(
    const std::vector<std::uint8_t>& prefix_board,
    const std::vector<ChanceOutcome>& outcomes);
std::vector<std::array<std::uint8_t, 2>> build_hole_index(
    const std::vector<std::array<std::uint8_t, 2>>& holes);
std::optional<std::vector<std::uint32_t>> hand_index_permutation(
    const std::vector<std::array<std::uint8_t, 2>>& holes,
    const std::vector<std::array<std::uint8_t, 2>>& hole_index,
    const SuitPerm& rel_perm);

SuitIsoCache build_suit_iso_cache(
    const std::vector<FlatNode>& nodes,
    const std::vector<std::optional<std::uint8_t>>& dealt_cards,
    const std::vector<std::uint8_t>& initial_board,
    const std::array<std::vector<std::array<std::uint8_t, 2>>, 2>& holes,
    const std::array<const std::vector<double>*, 2>& reach);

std::vector<bool> member_skip_mask(const std::vector<FlatNode>& nodes, const SuitIsoCache& cache);

}  // namespace core


