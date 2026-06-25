#include "core/hunl_tree.hpp"

#include <functional>

namespace core {

namespace {

template <class T>
void hash_combine(std::size_t& seed, const T& value) {
    seed ^= std::hash<T>{}(value) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
}

template <class T, std::size_t N>
void hash_combine_range(std::size_t& seed, const std::array<T, N>& values) {
    for (const auto& value : values) {
        hash_combine(seed, value);
    }
}

template <class T>
void hash_combine_range(std::size_t& seed, const std::vector<T>& values) {
    for (const auto& value : values) {
        hash_combine(seed, value);
    }
}

}

TerminalKind TerminalKind::non_terminal() {
    return {};
}

TerminalKind TerminalKind::fold(std::uint8_t winner, int contribution_loss) {
    TerminalKind kind;
    kind.tag = TerminalKindTag::Fold;
    kind.winner = winner;
    kind.contribution_loss = contribution_loss;
    return kind;
}

TerminalKind TerminalKind::showdown(bool board_complete) {
    TerminalKind kind;
    kind.tag = TerminalKindTag::Showdown;
    kind.board_complete = board_complete;
    return kind;
}

HUNLTreeNode HUNLTreeNode::empty(PlayerId player, const std::array<int, 2>& contrib, Street street) {
    HUNLTreeNode node;
    node.player = player;
    node.contrib = contrib;
    node.street = street;
    return node;
}

MemoKey MemoKey::from_state(const HUNLState& state) {
    MemoKey key;
    key.cur_player = state.cur_player;
    key.contributions = state.contributions;
    key.stacks = state.stacks;
    key.street = state.street;
    key.street_history = state.street_history;
    key.completed_streets.reserve(state.betting_tokens.size());
    for (const auto& street_tokens : state.betting_tokens) {
        std::vector<std::uint8_t> bytes;
        for (const auto& token : street_tokens) {
            bytes.insert(bytes.end(), token.begin(), token.end());
        }
        key.completed_streets.push_back(std::move(bytes));
    }
    key.current_street_tokens = state.current_street_tokens;
    key.folded = state.folded;
    key.all_in = state.all_in;
    key.board = state.board;
    key.hole_cards = state.hole_cards;
    key.pending_board_deals = state.pending_board_deals;
    key.to_call = state.to_call;
    key.street_num_raises = state.street_num_raises;
    key.street_aggressor = state.street_aggressor;
    return key;
}

bool MemoKey::operator==(const MemoKey& other) const noexcept {
    return cur_player == other.cur_player &&
           contributions == other.contributions &&
           stacks == other.stacks &&
           street == other.street &&
           street_history == other.street_history &&
           completed_streets == other.completed_streets &&
           current_street_tokens == other.current_street_tokens &&
           folded == other.folded &&
           all_in == other.all_in &&
           board == other.board &&
           hole_cards == other.hole_cards &&
           pending_board_deals == other.pending_board_deals &&
           to_call == other.to_call &&
           street_num_raises == other.street_num_raises &&
           street_aggressor == other.street_aggressor;
}

TerminalKind classify_terminal_kind(const HUNLState& state) {
    if (state.folded[0]) {
        return TerminalKind::fold(1, state.contributions[0]);
    }
    if (state.folded[1]) {
        return TerminalKind::fold(0, state.contributions[1]);
    }
    if (state.all_in[0] && state.all_in[1]) {
        return TerminalKind::showdown(state.board.size() >= 5);
    }
    return TerminalKind::non_terminal();
}

HUNLTree HUNLTree::build(std::shared_ptr<const HUNLConfig> cfg) {
    HUNLTree tree;
    tree.config = cfg;

    const auto state = HUNLState::initial(cfg);
    auto root = HUNLTreeNode::empty(state.cur_player, state.contributions, state.street);
    root.terminal_kind = classify_terminal_kind(state);
    root.chance_outcomes = {};
    root.chance_children = {};
    root.legal_actions = {};
    root.children = {};
    root.num_actions = 0;

    tree.nodes.push_back(std::move(root));
    tree.root = 0;
    tree.max_depth = 0;
    tree.max_actions = 0;
    return tree;
}

}  // namespace core

namespace std {

std::size_t hash<core::MemoKey>::operator()(const core::MemoKey& key) const noexcept {
    std::size_t seed = 0;
    hash_combine(seed, key.cur_player);
    hash_combine_range(seed, key.contributions);
    hash_combine_range(seed, key.stacks);
    hash_combine(seed, static_cast<std::uint8_t>(key.street));
    hash_combine_range(seed, key.street_history);
    for (const auto& street : key.completed_streets) {
        hash_combine_range(seed, street);
    }
    for (const auto& token : key.current_street_tokens) {
        hash_combine_range(seed, std::vector<char>(token.begin(), token.end()));
    }
    hash_combine_range(seed, key.folded);
    hash_combine_range(seed, key.all_in);
    hash_combine_range(seed, key.board);
    if (key.hole_cards.has_value()) {
        for (const auto& hand : *key.hole_cards) {
            hash_combine_range(seed, hand);
        }
    }
    hash_combine(seed, key.pending_board_deals);
    hash_combine(seed, key.to_call);
    hash_combine(seed, key.street_num_raises);
    hash_combine(seed, key.street_aggressor);
    return seed;
}

}  // namespace std
