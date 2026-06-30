#pragma once

#include "core/types.hpp"
#include "games/hunl_tree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace core {

enum class HUNLFlatNodeType : std::uint8_t {
    TerminalFold = 0,
    TerminalShowdown = 1,
    DepthLimited = 2,
    Chance = 3,
    Decision = 4,
};

struct HUNLFlatNode {
    std::uint32_t child_begin = 0;
    std::uint32_t child_count = 0;
    std::uint32_t action_begin = 0;
    std::uint32_t chance_begin = 0;
    std::uint32_t chance_count = 0;
    InfosetId infoset_id{};
    std::array<int, 2> contributions = {0, 0};
    std::array<double, 2> terminal_utility = {0.0, 0.0};
    std::vector<std::uint8_t> board;
    TerminalKind terminal_kind = TerminalKind::non_terminal();
    PlayerId player = -1;
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    Street street = Street::Preflop;
    std::uint8_t action_count = 0;
    bool has_infoset = false;
};

struct HUNLFlatNodeMeta {
    std::uint32_t child_begin = 0;
    std::uint32_t child_count = 0;
    std::uint32_t action_begin = 0;
    std::uint32_t chance_begin = 0;
    std::uint32_t chance_count = 0;
    InfosetId infoset_id{};
    std::array<int, 2> contributions = {0, 0};
    std::array<double, 2> terminal_utility = {0.0, 0.0};
    std::vector<std::uint8_t> board;
    TerminalKind terminal_kind = TerminalKind::non_terminal();
    PlayerId player = -1;
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    Street street = Street::Preflop;
    std::uint8_t action_count = 0;
    bool has_infoset = false;
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
    std::vector<std::uint8_t> board;
    std::string key;
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint8_t action_count = 0;
};

struct HUNLFlatSlice {
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

struct HUNLFlatWorkerRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

static_assert(std::is_trivially_copyable_v<HUNLFlatChanceOutcome>,
              "HUNLFlatChanceOutcome should stay trivially copyable");
static_assert(std::is_trivially_copyable_v<HUNLFlatSlice>, "HUNLFlatSlice should stay trivially copyable");
static_assert(std::is_trivially_copyable_v<HUNLFlatWorkerRange>,
              "HUNLFlatWorkerRange should stay trivially copyable");

struct HUNLFlatSolveGraph {
    std::vector<HUNLFlatNode> nodes;
    std::vector<HUNLFlatNodeMeta> node_meta;
    std::vector<std::uint32_t> children;
    std::vector<ActionId> actions;
    std::vector<HUNLFlatChanceOutcome> chance_outcomes;
    std::vector<HUNLFlatInfoset> infosets;
    std::vector<std::uint32_t> infoset_nodes;
    std::vector<std::uint32_t> forward_order;
    std::vector<std::uint32_t> reverse_order;
    std::vector<std::uint32_t> node_depths;
    std::array<HUNLFlatSlice, 5> street_slices = {};
    std::vector<std::uint32_t> street_order;
    std::vector<HUNLFlatSlice> depth_slices;
    std::vector<std::uint32_t> depth_order;
    std::vector<std::vector<HUNLFlatWorkerRange>> depth_worker_ranges;
    std::vector<std::uint32_t> terminal_nodes;
    std::vector<double> terminal_node_values;
    std::vector<std::uint32_t> fold_terminal_nodes;
    std::vector<double> fold_terminal_values;
    std::vector<std::uint32_t> showdown_terminal_nodes;
    std::vector<double> showdown_terminal_values;
    std::uint32_t root = 0;
    std::uint32_t max_depth = 0;
    std::uint8_t max_actions = 0;
    std::shared_ptr<const HUNLConfig> config;

    static HUNLFlatSolveGraph build(const HUNLTree& tree);
    static HUNLFlatSolveGraph build(std::shared_ptr<const HUNLConfig> config);
};

}  // namespace core
