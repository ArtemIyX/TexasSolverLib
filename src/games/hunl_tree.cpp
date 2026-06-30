#include "games/hunl_tree.hpp"

#include <functional>

namespace core {

namespace detail {

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

}  // namespace detail

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

HUNLTreeNode HUNLTreeNode::empty(
    PlayerId player,
    const std::array<int, 2>& contrib,
    Street street,
    std::vector<std::uint8_t> board) {
    HUNLTreeNode node;
    node.player = player;
    node.contrib = contrib;
    node.street = street;
    node.board = std::move(board);
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
    if (state.street == Street::Showdown || (state.all_in[0] && state.all_in[1])) {
        return TerminalKind::showdown(state.board.size() >= 5);
    }
    return TerminalKind::non_terminal();
}

HUNLTree HUNLTree::build(std::shared_ptr<const HUNLConfig> cfg) {
    HUNLTree tree;
    tree.config = cfg;
    std::unordered_map<MemoKey, std::uint32_t> memo;
    const auto state = HUNLState::initial(cfg);
    tree.root = tree.build_node(state, memo, 0);
    return tree;
}

std::uint32_t HUNLTree::build_node(
    const HUNLState& state,
    std::unordered_map<MemoKey, std::uint32_t>& memo,
    std::uint32_t depth) {
    const auto key = MemoKey::from_state(state);
    if (const auto it = memo.find(key); it != memo.end()) {
        return it->second;
    }

    const auto my_idx = static_cast<std::uint32_t>(nodes.size());
    nodes.push_back(HUNLTreeNode::empty(state.cur_player, state.contributions, state.street, state.board));
    memo.emplace(key, my_idx);
    max_depth = std::max(max_depth, depth);

    if (state.is_terminal()) {
        auto& node = nodes[my_idx];
        node.player = -2;
        node.terminal_kind = classify_terminal_kind(state);
        const auto utility = state.utility();
        if (utility.size() >= 2) {
            node.terminal_utility = {utility[0], utility[1]};
        }
        return my_idx;
    }

    if (state.cur_player == -1) {
        const auto outcomes = state.chance_outcomes();
        std::vector<std::uint32_t> chance_children;
        chance_children.reserve(outcomes.size());
        for (const auto& outcome : outcomes) {
            const auto child_idx = build_node(state.apply(outcome.action), memo, depth + 1);
            auto& child = nodes[child_idx];
            if (!child.chance_action.has_value()) {
                child.chance_action = static_cast<std::uint8_t>(outcome.action);
                child.chance_prob = outcome.probability;
            }
            chance_children.push_back(child_idx);
        }

        auto& node = nodes[my_idx];
        node.chance_outcomes.reserve(outcomes.size());
        for (const auto& outcome : outcomes) {
            node.chance_outcomes.emplace_back(static_cast<std::uint8_t>(outcome.action), outcome.probability);
        }
        node.chance_children = std::move(chance_children);
        return my_idx;
    }

    const auto actions = state.legal_actions();
    max_actions = std::max(max_actions, static_cast<std::uint8_t>(actions.size()));
    std::vector<std::uint32_t> children;
    children.reserve(actions.size());
    for (const auto action : actions) {
        children.push_back(build_node(state.apply(action), memo, depth + 1));
    }

    auto& node = nodes[my_idx];
    node.num_actions = static_cast<std::uint8_t>(actions.size());
    node.legal_actions = actions;
    node.children = std::move(children);
    node.infoset_key = state.infoset_key(static_cast<std::uint8_t>(state.cur_player));
    return my_idx;
}

}  // namespace core

namespace std {

std::size_t hash<core::MemoKey>::operator()(const core::MemoKey& key) const noexcept {
    std::size_t seed = 0;
    core::detail::hash_combine(seed, key.cur_player);
    core::detail::hash_combine_range(seed, key.contributions);
    core::detail::hash_combine_range(seed, key.stacks);
    core::detail::hash_combine(seed, static_cast<std::uint8_t>(key.street));
    core::detail::hash_combine_range(seed, key.street_history);
    for (const auto& street : key.completed_streets) {
        core::detail::hash_combine_range(seed, street);
    }
    for (const auto& token : key.current_street_tokens) {
        core::detail::hash_combine_range(seed, std::vector<char>(token.begin(), token.end()));
    }
    core::detail::hash_combine_range(seed, key.folded);
    core::detail::hash_combine_range(seed, key.all_in);
    core::detail::hash_combine_range(seed, key.board);
    if (key.hole_cards.has_value()) {
        for (const auto& hand : *key.hole_cards) {
            core::detail::hash_combine_range(seed, hand);
        }
    }
    core::detail::hash_combine(seed, key.pending_board_deals);
    core::detail::hash_combine(seed, key.to_call);
    core::detail::hash_combine(seed, key.street_num_raises);
    core::detail::hash_combine(seed, key.street_aggressor);
    return seed;
}

}  // namespace std


