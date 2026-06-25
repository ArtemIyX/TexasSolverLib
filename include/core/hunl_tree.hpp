#pragma once

#include "core/hunl.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

enum class TerminalKindTag : std::uint8_t {
    NonTerminal = 0,
    Fold = 1,
    Showdown = 2,
};

struct TerminalKind {
    TerminalKindTag tag = TerminalKindTag::NonTerminal;
    std::uint8_t winner = 0;
    int contribution_loss = 0;
    bool board_complete = false;

    static TerminalKind non_terminal();
    static TerminalKind fold(std::uint8_t winner, int contribution_loss);
    static TerminalKind showdown(bool board_complete);
};

struct HUNLTreeNode {
    PlayerId player = -1;
    TerminalKind terminal_kind = TerminalKind::non_terminal();
    std::array<int, 2> contrib = {0, 0};
    Street street = Street::Preflop;
    std::uint8_t num_actions = 0;
    std::vector<ActionId> legal_actions;
    std::vector<std::uint32_t> children;
    std::optional<std::string> infoset_key = std::nullopt;
    std::optional<std::uint8_t> chance_action = std::nullopt;
    double chance_prob = 1.0;
    std::vector<std::pair<std::uint8_t, double>> chance_outcomes;
    std::vector<std::uint32_t> chance_children;

    static HUNLTreeNode empty(PlayerId player, const std::array<int, 2>& contrib, Street street);
};

struct HUNLTree {
    std::vector<HUNLTreeNode> nodes;
    std::uint32_t root = 0;
    std::uint32_t max_depth = 0;
    std::uint8_t max_actions = 0;
    std::shared_ptr<const HUNLConfig> config;

    static HUNLTree build(std::shared_ptr<const HUNLConfig> config);
};

struct MemoKey {
    PlayerId cur_player = -1;
    std::array<int, 2> contributions = {0, 0};
    std::array<int, 2> stacks = {0, 0};
    Street street = Street::Preflop;
    std::vector<ActionId> street_history;
    std::vector<std::vector<std::uint8_t>> completed_streets;
    std::vector<std::string> current_street_tokens;
    std::array<bool, 2> folded = {false, false};
    std::array<bool, 2> all_in = {false, false};
    std::vector<std::uint8_t> board;
    std::optional<std::array<std::array<std::uint8_t, 2>, 2>> hole_cards = std::nullopt;
    std::uint8_t pending_board_deals = 0;
    int to_call = 0;
    std::uint8_t street_num_raises = 0;
    PlayerId street_aggressor = -1;

    static MemoKey from_state(const HUNLState& state);

    bool operator==(const MemoKey& other) const noexcept;
};

TerminalKind classify_terminal_kind(const HUNLState& state);

}  // namespace core

namespace std {

template <>
struct hash<core::MemoKey> {
    std::size_t operator()(const core::MemoKey& key) const noexcept;
};

}  // namespace std
