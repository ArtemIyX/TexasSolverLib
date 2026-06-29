#pragma once

#include "core/types.hpp"
#include "games/hunl_tree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace core {

enum class HUNLFlatNodeType : std::uint8_t {
    TerminalFold = 0,
    TerminalShowdown = 1,
    Chance = 2,
    Decision = 3,
};

struct HUNLFlatNode {
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::array<int, 2> contributions = {0, 0};
    std::uint32_t child_begin = 0;
    std::uint32_t child_count = 0;
    std::uint32_t action_begin = 0;
    std::uint32_t chance_begin = 0;
    std::uint32_t chance_count = 0;
    std::uint8_t action_count = 0;
    InfosetId infoset_id{};
    bool has_infoset = false;
    TerminalKind terminal_kind = TerminalKind::non_terminal();
    std::optional<std::string> infoset_key = std::nullopt;
};

struct HUNLFlatNodeMeta {
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::array<int, 2> contributions = {0, 0};
    std::uint32_t child_begin = 0;
    std::uint32_t child_count = 0;
    std::uint32_t action_begin = 0;
    std::uint32_t chance_begin = 0;
    std::uint32_t chance_count = 0;
    std::uint8_t action_count = 0;
    InfosetId infoset_id{};
    bool has_infoset = false;
    TerminalKind terminal_kind = TerminalKind::non_terminal();
};

struct HUNLFlatChanceOutcome {
    std::uint8_t action = 0;
    double probability = 0.0;
    std::uint32_t child = 0;
};

struct HUNLFlatInfoset {
    InfosetId id{};
    std::uint32_t node_begin = 0;
    std::uint32_t node_count = 0;
    std::uint8_t action_count = 0;
    std::string key;
};

struct HUNLFlatSolveGraph {
    std::vector<HUNLFlatNode> nodes;
    std::vector<HUNLFlatNodeMeta> node_meta;
    std::vector<std::uint32_t> children;
    std::vector<ActionId> actions;
    std::vector<HUNLFlatChanceOutcome> chance_outcomes;
    std::vector<HUNLFlatInfoset> infosets;
    std::vector<std::uint32_t> infoset_nodes;
    std::uint32_t root = 0;
    std::uint32_t max_depth = 0;
    std::uint8_t max_actions = 0;
    std::shared_ptr<const HUNLConfig> config;

    static HUNLFlatSolveGraph build(const HUNLTree& tree);
    static HUNLFlatSolveGraph build(std::shared_ptr<const HUNLConfig> config);
};

}  // namespace core
