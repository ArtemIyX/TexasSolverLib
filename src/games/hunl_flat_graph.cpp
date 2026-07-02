#include "games/hunl_flat_graph.hpp"
#include "games/hunl_flat_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

namespace {

HUNLFlatNodeType classify_flat_node_type(const HUNLTreeNode& node) {
    if (node.depth_limited_leaf) {
        return HUNLFlatNodeType::DepthLimited;
    }
    switch (node.terminal_kind.tag) {
        case TerminalKindTag::Fold:
            return HUNLFlatNodeType::TerminalFold;
        case TerminalKindTag::Showdown:
            return HUNLFlatNodeType::TerminalShowdown;
        case TerminalKindTag::NonTerminal:
            break;
    }

    if (!node.chance_children.empty()) {
        return HUNLFlatNodeType::Chance;
    }
    return HUNLFlatNodeType::Decision;
}

std::size_t street_index(Street street) {
    return static_cast<std::size_t>(street);
}

std::string make_flat_infoset_key(const HUNLTreeNode& node) {
    if (!node.infoset_key.has_value()) {
        return {};
    }

    std::string key = *node.infoset_key;
    key += "|board:";
    key += sorted_card_string(node.board);
    key += "|street:";
    key += street_token(node.street);
    return key;
}

std::vector<std::uint8_t> canonical_board(const std::vector<std::uint8_t>& board) {
    std::vector<std::uint8_t> out = board;
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<HUNLFlatWorkerRange> make_ranges_for_slice(std::uint32_t begin, std::uint32_t count) {
    std::vector<HUNLFlatWorkerRange> ranges;
    if (count == 0) {
        return ranges;
    }

    const std::uint32_t chunk_target = std::max<std::uint32_t>(1U, std::min<std::uint32_t>(count, 4U));
    const std::uint32_t base = count / chunk_target;
    const std::uint32_t remainder = count % chunk_target;
    std::uint32_t offset = begin;
    ranges.reserve(chunk_target);
    for (std::uint32_t i = 0; i < chunk_target; ++i) {
        const std::uint32_t width = base + (i < remainder ? 1U : 0U);
        if (width == 0) {
            continue;
        }
        ranges.push_back(HUNLFlatWorkerRange{offset, offset + width});
        offset += width;
    }
    return ranges;
}

}  // namespace

HUNLFlatSolveGraph HUNLFlatSolveGraph::build(const HUNLTree& tree) {
    if (tree.root >= tree.nodes.size() && !tree.nodes.empty()) {
        throw std::invalid_argument("HUNLFlatSolveGraph tree root out of bounds");
    }
    HUNLFlatSolveGraph graph;
    graph.node_meta.reserve(tree.nodes.size());
    graph.children.reserve(tree.nodes.size() * 2);
    graph.actions.reserve(tree.nodes.size() * 2);
    graph.root = tree.root;
    graph.max_depth = tree.max_depth;
    graph.max_actions = tree.max_actions;
    graph.config = tree.config;

    std::size_t total_chance_outcomes = 0;
    for (const auto& node : tree.nodes) {
        total_chance_outcomes += node.chance_outcomes.size();
    }
    graph.chance_outcomes.reserve(total_chance_outcomes);

    std::unordered_map<std::string, InfosetId> infoset_ids_by_key;
    std::vector<std::vector<std::uint32_t>> infoset_node_lists;
    std::vector<std::uint8_t> infoset_action_counts;
    std::vector<PlayerId> infoset_players;
    std::vector<Street> infoset_streets;
    std::vector<std::vector<std::uint8_t>> infoset_boards;
    std::vector<std::string> infoset_keys;

    for (std::uint32_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        const auto& node = tree.nodes[node_idx];
        if (node.street > Street::Showdown) {
            throw std::logic_error("flat graph node street out of range");
        }
        HUNLFlatNodeMeta flat_node;
        flat_node.type = classify_flat_node_type(node);
        flat_node.player = node.player;
        flat_node.street = node.street;
        flat_node.board = HUNLFlatSolveGraph::pack_board(node.board);
        flat_node.contributions = node.contrib;
        flat_node.terminal_kind = node.terminal_kind;
        flat_node.terminal_utility = node.terminal_utility;

        if (flat_node.type == HUNLFlatNodeType::Chance) {
            if (node.chance_children.size() != node.chance_outcomes.size()) {
                throw std::logic_error("chance node children and outcomes must have matching sizes");
            }

            flat_node.chance_begin = static_cast<std::uint32_t>(graph.chance_outcomes.size());
            flat_node.chance_count = static_cast<std::uint32_t>(node.chance_outcomes.size());
            flat_node.child_begin = static_cast<std::uint32_t>(graph.children.size());
            flat_node.child_count = static_cast<std::uint32_t>(node.chance_children.size());

            for (std::size_t i = 0; i < node.chance_children.size(); ++i) {
                graph.children.push_back(node.chance_children[i]);
                graph.chance_outcomes.push_back(HUNLFlatChanceOutcome{
                    node.chance_outcomes[i].first,
                    node.chance_outcomes[i].second,
                    node.chance_children[i],
                });
            }
        } else if (flat_node.type == HUNLFlatNodeType::Decision) {
            if (node.children.size() != node.legal_actions.size()) {
                throw std::logic_error("decision node children and actions must have matching sizes");
            }
            if (!node.infoset_key.has_value()) {
                throw std::logic_error("decision node must have infoset_key");
            }
            if (node.legal_actions.empty()) {
                throw std::logic_error("decision node must expose at least one legal action");
            }

            flat_node.child_begin = static_cast<std::uint32_t>(graph.children.size());
            flat_node.child_count = static_cast<std::uint32_t>(node.children.size());
            flat_node.action_begin = static_cast<std::uint32_t>(graph.actions.size());
            flat_node.action_count = static_cast<std::uint8_t>(node.legal_actions.size());
            flat_node.has_infoset = true;

            const auto flat_key = make_flat_infoset_key(node);
            const auto key_it = infoset_ids_by_key.find(flat_key);
            if (key_it == infoset_ids_by_key.end()) {
                const InfosetId id{static_cast<std::uint32_t>(infoset_keys.size())};
                infoset_ids_by_key.emplace(flat_key, id);
                infoset_node_lists.push_back({});
                infoset_action_counts.push_back(flat_node.action_count);
                infoset_players.push_back(flat_node.player);
                infoset_streets.push_back(flat_node.street);
                infoset_boards.push_back(canonical_board(node.board));
                infoset_keys.push_back(std::move(flat_key));
                flat_node.infoset_id = id;
            } else {
                flat_node.infoset_id = key_it->second;
                const auto action_count = infoset_action_counts[flat_node.infoset_id.value];
                if (action_count != flat_node.action_count) {
                    throw std::logic_error("infoset nodes must agree on action_count");
                }
                const auto player = infoset_players[flat_node.infoset_id.value];
                if (player != flat_node.player) {
                    throw std::logic_error("infoset nodes must agree on owning player");
                }
                const auto street = infoset_streets[flat_node.infoset_id.value];
                if (street != flat_node.street) {
                    throw std::logic_error("infoset nodes must agree on street");
                }
                const auto& board = infoset_boards[flat_node.infoset_id.value];
                if (board != canonical_board(node.board)) {
                    throw std::logic_error("infoset nodes must agree on board");
                }
            }
            infoset_node_lists[flat_node.infoset_id.value].push_back(node_idx);

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                graph.children.push_back(node.children[i]);
                graph.actions.push_back(node.legal_actions[i]);
            }
        } else if (flat_node.type == HUNLFlatNodeType::DepthLimited) {
            if (!node.children.empty() || !node.chance_children.empty()) {
                throw std::logic_error("depth-limited node must not have children");
            }
        }

        graph.node_meta.push_back(flat_node);

        if (flat_node.type == HUNLFlatNodeType::TerminalFold) {
            graph.terminal_nodes.push_back(node_idx);
            graph.terminal_node_values.push_back(flat_node.terminal_utility[0]);
            graph.fold_terminal_nodes.push_back(node_idx);
            graph.fold_terminal_values.push_back(flat_node.terminal_utility[0]);
        } else if (flat_node.type == HUNLFlatNodeType::TerminalShowdown) {
            graph.terminal_nodes.push_back(node_idx);
            graph.terminal_node_values.push_back(flat_node.terminal_utility[0]);
            graph.showdown_terminal_nodes.push_back(node_idx);
            graph.showdown_terminal_values.push_back(flat_node.terminal_utility[0]);
        } else if (flat_node.type == HUNLFlatNodeType::DepthLimited) {
            graph.terminal_nodes.push_back(node_idx);
            graph.terminal_node_values.push_back(flat_node.terminal_utility[0]);
        }
    }

    graph.infosets.reserve(infoset_keys.size());
    graph.infoset_debug_keys = infoset_keys;
    for (std::uint32_t infoset_index = 0; infoset_index < infoset_keys.size(); ++infoset_index) {
        const auto& node_list = infoset_node_lists[infoset_index];
        if (node_list.empty()) {
            throw std::logic_error("flat graph infoset must own at least one node");
        }
        HUNLFlatInfoset infoset;
        infoset.id = InfosetId{infoset_index};
        infoset.node_begin = static_cast<std::uint32_t>(graph.infoset_nodes.size());
        infoset.node_count = static_cast<std::uint32_t>(node_list.size());
        infoset.action_count = infoset_action_counts[infoset_index];
        infoset.player = infoset_players[infoset_index];
        infoset.street = infoset_streets[infoset_index];
        infoset.board = HUNLFlatSolveGraph::pack_board(infoset_boards[infoset_index]);
        infoset.debug_key_index = infoset_index;
        graph.infoset_nodes.insert(graph.infoset_nodes.end(), node_list.begin(), node_list.end());
        graph.infosets.push_back(std::move(infoset));
    }

    std::vector<std::uint32_t> indegree(graph.node_meta.size(), 0);
    std::vector<std::vector<std::uint32_t>> outgoing(graph.node_meta.size());
    for (std::uint32_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        outgoing[node_idx].reserve(meta.child_count);
        for (std::uint32_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph.children[meta.child_begin + i];
            if (child >= graph.node_meta.size()) {
                throw std::logic_error("child index out of bounds in flat graph");
            }
            outgoing[node_idx].push_back(child);
            ++indegree[child];
        }
    }

    std::deque<std::uint32_t> ready;
    for (std::uint32_t node_idx = 0; node_idx < indegree.size(); ++node_idx) {
        if (indegree[node_idx] == 0) {
            ready.push_back(node_idx);
        }
    }

    graph.forward_order.reserve(graph.node_meta.size());
    graph.node_depths.assign(graph.node_meta.size(), 0);
    while (!ready.empty()) {
        const auto node_idx = ready.front();
        ready.pop_front();
        graph.forward_order.push_back(node_idx);
        const auto parent_depth = graph.node_depths[node_idx];
        for (const auto child : outgoing[node_idx]) {
            graph.node_depths[child] = std::max(graph.node_depths[child], parent_depth + 1U);
            if (--indegree[child] == 0) {
                ready.push_back(child);
            }
        }
    }

    if (graph.forward_order.size() != graph.node_meta.size()) {
        throw std::logic_error("flat graph must be acyclic");
    }

    graph.reverse_order = graph.forward_order;
    std::reverse(graph.reverse_order.begin(), graph.reverse_order.end());

    std::uint32_t max_topo_depth = 0;
    for (const auto depth : graph.node_depths) {
        max_topo_depth = std::max(max_topo_depth, depth);
    }

    std::vector<std::vector<std::uint32_t>> depth_buckets(static_cast<std::size_t>(max_topo_depth) + 1U);
    std::array<std::vector<std::uint32_t>, 5> street_buckets;
    for (const auto node_idx : graph.forward_order) {
        depth_buckets[graph.node_depths[node_idx]].push_back(node_idx);
        street_buckets[street_index(graph.node_meta[node_idx].street)].push_back(node_idx);
    }

    graph.depth_slices.reserve(depth_buckets.size());
    for (const auto& bucket : depth_buckets) {
        const auto begin = static_cast<std::uint32_t>(graph.depth_order.size());
        graph.depth_order.insert(graph.depth_order.end(), bucket.begin(), bucket.end());
        graph.depth_slices.push_back(HUNLFlatSlice{begin, static_cast<std::uint32_t>(bucket.size())});
        graph.depth_worker_ranges.push_back(make_ranges_for_slice(begin, static_cast<std::uint32_t>(bucket.size())));
    }

    for (std::size_t i = 0; i < street_buckets.size(); ++i) {
        const auto begin = static_cast<std::uint32_t>(graph.street_order.size());
        graph.street_order.insert(graph.street_order.end(), street_buckets[i].begin(), street_buckets[i].end());
        graph.street_slices[i] = HUNLFlatSlice{begin, static_cast<std::uint32_t>(street_buckets[i].size())};
    }

    return graph;
}

HUNLFlatSolveGraph HUNLFlatSolveGraph::build(std::shared_ptr<const HUNLConfig> config) {
    return HUNLFlatBuilder::build(std::move(config));
}

std::size_t HUNLFlatSolveGraph::node_count() const noexcept {
    return node_meta.size();
}

std::vector<std::uint8_t> HUNLFlatSolveGraph::node_board(std::uint32_t node_idx) const {
    if (node_idx >= node_meta.size()) {
        throw std::out_of_range("flat graph node_board node_idx out of range");
    }
    return unpack_board(node_meta[node_idx].board);
}

std::vector<std::uint8_t> HUNLFlatSolveGraph::infoset_board(InfosetId infoset_id) const {
    if (infoset_id.value >= infosets.size()) {
        throw std::out_of_range("flat graph infoset_board infoset id out of range");
    }
    return unpack_board(infosets[infoset_id.value].board);
}

std::string_view HUNLFlatSolveGraph::infoset_key(InfosetId infoset_id) const noexcept {
    if (infoset_id.value >= infosets.size()) {
        return {};
    }
    return infoset_key(infosets[infoset_id.value]);
}

std::string_view HUNLFlatSolveGraph::infoset_key(const HUNLFlatInfoset& infoset) const noexcept {
    if (infoset.debug_key_index >= infoset_debug_keys.size()) {
        return {};
    }
    return infoset_debug_keys[infoset.debug_key_index];
}

HUNLFlatPackedBoard HUNLFlatSolveGraph::pack_board(const std::vector<std::uint8_t>& board) {
    if (board.size() > 5) {
        throw std::invalid_argument("flat graph pack_board supports up to 5 cards");
    }
    HUNLFlatPackedBoard packed;
    packed.count = static_cast<std::uint8_t>(board.size());
    for (std::size_t i = 0; i < board.size(); ++i) {
        packed.cards[i] = board[i];
    }
    return packed;
}

std::vector<std::uint8_t> HUNLFlatSolveGraph::unpack_board(const HUNLFlatPackedBoard& board) {
    return std::vector<std::uint8_t>(board.cards.begin(), board.cards.begin() + board.count);
}

}  // namespace core
