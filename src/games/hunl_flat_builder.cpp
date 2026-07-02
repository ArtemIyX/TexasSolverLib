#include "games/hunl_flat_builder.hpp"
#include "games/hunl_tree.hpp"
#include "util/suit_iso.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

}  // namespace detail

namespace {

std::size_t street_index(Street street) {
    return static_cast<std::size_t>(street);
}

std::string make_flat_infoset_key(const HUNLState& state) {
    std::string key = state.infoset_key(static_cast<std::uint8_t>(state.cur_player));
    key += "|board:";
    key += sorted_card_string(state.board);
    key += "|street:";
    key += street_token(state.street);
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

void finalize_graph(HUNLFlatSolveGraph& graph) {
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
}

struct HUNLFlatBuildFrame {
    HUNLState state;
    std::uint32_t node_idx = 0;
    std::uint32_t depth = 0;
    std::size_t next_child = 0;
    std::vector<ActionId> actions;
    std::vector<ChanceOutcome> chance_outcomes;
    std::vector<PublicChanceClass> public_chance_classes;
    std::vector<std::uint32_t> child_indices;
    bool initialized = false;
    bool is_terminal = false;
    bool is_depth_limited = false;
    bool is_chance = false;
    bool collapsed_public_chance = false;
};

}  // namespace

HUNLFlatBuilderMemoKey HUNLFlatBuilderMemoKey::from_state(const HUNLState& state) {
    HUNLFlatBuilderMemoKey key;
    key.cur_player = state.cur_player;
    key.contributions = state.contributions;
    key.stacks = state.stacks;
    key.folded = state.folded;
    key.all_in = state.all_in;
    key.street = state.street;
    key.board_count = static_cast<std::uint8_t>(state.board.size());
    key.has_hole_cards = state.hole_cards.has_value();
    key.pending_board_deals = state.pending_board_deals;
    key.street_num_raises = state.street_num_raises;
    key.to_call = state.to_call;
    key.street_aggressor = state.street_aggressor;

    if (state.street_history.size() > key.street_history.size()) {
        throw std::invalid_argument("HUNLFlatBuilderMemoKey::from_state street_history is too large");
    }
    key.street_history_count = static_cast<std::uint8_t>(state.street_history.size());
    for (std::size_t i = 0; i < state.street_history.size(); ++i) {
        key.street_history[i] = state.street_history[i];
    }

    if (state.board.size() > key.board.size()) {
        throw std::invalid_argument("HUNLFlatBuilderMemoKey::from_state board is too large");
    }
    for (std::size_t i = 0; i < state.board.size(); ++i) {
        key.board[i] = state.board[i];
    }

    if (state.hole_cards.has_value()) {
        key.hole_cards = *state.hole_cards;
    }

    std::size_t offset = 0;
    for (std::size_t street_idx = 0;
         street_idx < state.betting_history_codes.size() && street_idx < key.street_lengths.size();
         ++street_idx) {
        const auto& street_codes = state.betting_history_codes[street_idx];
        if (offset + street_codes.size() > key.history_codes.size()) {
            throw std::invalid_argument("HUNLFlatBuilderMemoKey::from_state exceeded history capacity");
        }
        key.street_lengths[street_idx] = static_cast<std::uint8_t>(street_codes.size());
        for (const auto code : street_codes) {
            key.history_codes[offset++] = code;
        }
    }

    if (!state.current_street_history_codes.empty()) {
        const auto current_street_idx =
            std::min<std::size_t>(state.betting_history_codes.size(), key.street_lengths.size() - 1U);
        if (offset + state.current_street_history_codes.size() > key.history_codes.size()) {
            throw std::invalid_argument("HUNLFlatBuilderMemoKey::from_state exceeded history capacity");
        }
        key.street_lengths[current_street_idx] =
            static_cast<std::uint8_t>(state.current_street_history_codes.size());
        for (const auto code : state.current_street_history_codes) {
            key.history_codes[offset++] = code;
        }
    }
    key.history_count = static_cast<std::uint8_t>(offset);

    return key;
}

bool HUNLFlatBuilderMemoKey::operator==(const HUNLFlatBuilderMemoKey& other) const noexcept {
    return cur_player == other.cur_player &&
           contributions == other.contributions &&
           stacks == other.stacks &&
           street_history == other.street_history &&
           folded == other.folded &&
           all_in == other.all_in &&
           board == other.board &&
           hole_cards == other.hole_cards &&
           street_lengths == other.street_lengths &&
           history_codes == other.history_codes &&
           street == other.street &&
           board_count == other.board_count &&
           history_count == other.history_count &&
           street_history_count == other.street_history_count &&
           pending_board_deals == other.pending_board_deals &&
           street_num_raises == other.street_num_raises &&
           has_hole_cards == other.has_hole_cards &&
           to_call == other.to_call &&
           street_aggressor == other.street_aggressor;
}

HUNLFlatSolveMode resolve_flat_builder_solve_mode(const HUNLConfig& config) {
    if (config.flat_solve_mode != HUNLFlatSolveMode::Auto) {
        return config.flat_solve_mode;
    }
    return config.abstraction_path.has_value()
        ? HUNLFlatSolveMode::Bucketed
        : HUNLFlatSolveMode::ExplicitHand;
}

bool can_collapse_public_chance(
    const HUNLState& state,
    HUNLFlatSolveMode solve_mode) {
    if (solve_mode == HUNLFlatSolveMode::ExplicitHand) {
        return false;
    }
    if (state.cur_player != -1 || state.pending_board_deals != 1) {
        return false;
    }
    return state.street == Street::Turn || state.street == Street::River;
}

HUNLFlatSolveGraph HUNLFlatBuilder::build(std::shared_ptr<const HUNLConfig> config) {
    HUNLFlatSolveGraph graph;
    graph.config = config;

    std::unordered_map<MemoKey, std::uint32_t> memo;
    std::unordered_map<std::string, InfosetId> infoset_ids_by_key;
    std::vector<std::vector<std::uint32_t>> infoset_node_lists;
    std::vector<std::uint8_t> infoset_action_counts;
    std::vector<PlayerId> infoset_players;
    std::vector<Street> infoset_streets;
    std::vector<std::vector<std::uint8_t>> infoset_boards;
    std::vector<std::string> infoset_keys;

    const auto initial_state = HUNLState::initial(config);
    const auto depth_limit_plies = config ? config->depth_limit_plies : 0U;
    const auto solve_mode = config ? resolve_flat_builder_solve_mode(*config) : HUNLFlatSolveMode::ExplicitHand;

    auto emplace_new_node = [&](const HUNLState& state, std::uint32_t depth) {
        const auto node_idx = static_cast<std::uint32_t>(graph.node_meta.size());
        graph.node_meta.push_back(HUNLFlatNodeMeta{});
        auto& meta = graph.node_meta.back();
        meta.player = state.cur_player;
        meta.street = state.street;
        meta.board = HUNLFlatSolveGraph::pack_board(state.board);
        meta.contributions = state.contributions;
        graph.max_depth = std::max(graph.max_depth, depth);
        return node_idx;
    };

    auto find_or_create_node =
        [&](const HUNLState& state, std::uint32_t depth) -> std::pair<std::uint32_t, bool> {
        const auto key = MemoKey::from_state(state);
        if (const auto it = memo.find(key); it != memo.end()) {
            return {it->second, false};
        }

        const auto node_idx = emplace_new_node(state, depth);
        memo.emplace(key, node_idx);
        return {node_idx, true};
    };

    const auto [root_idx, root_is_new] = find_or_create_node(initial_state, 0);
    if (!root_is_new) {
        throw std::logic_error("flat builder root unexpectedly memoized before creation");
    }
    graph.root = root_idx;

    std::vector<HUNLFlatBuildFrame> stack;
    stack.push_back(HUNLFlatBuildFrame{initial_state, root_idx, 0});

    while (!stack.empty()) {
        auto& frame = stack.back();

        if (!frame.initialized) {
            frame.initialized = true;
            if (frame.state.is_terminal()) {
                frame.is_terminal = true;
                auto& meta = graph.node_meta[frame.node_idx];
                meta.player = -2;
                meta.terminal_kind = classify_terminal_kind(frame.state);
                const auto utility = frame.state.utility();
                if (utility.size() >= 2) {
                    meta.terminal_utility = {utility[0], utility[1]};
                }

                if (meta.terminal_kind.tag == TerminalKindTag::Fold) {
                    meta.type = HUNLFlatNodeType::TerminalFold;
                    graph.terminal_nodes.push_back(frame.node_idx);
                    graph.terminal_node_values.push_back(meta.terminal_utility[0]);
                    graph.fold_terminal_nodes.push_back(frame.node_idx);
                    graph.fold_terminal_values.push_back(meta.terminal_utility[0]);
                } else if (meta.terminal_kind.tag == TerminalKindTag::Showdown) {
                    meta.type = HUNLFlatNodeType::TerminalShowdown;
                    graph.terminal_nodes.push_back(frame.node_idx);
                    graph.terminal_node_values.push_back(meta.terminal_utility[0]);
                    graph.showdown_terminal_nodes.push_back(frame.node_idx);
                    graph.showdown_terminal_values.push_back(meta.terminal_utility[0]);
                } else {
                    throw std::logic_error("terminal state classified as non-terminal");
                }
                stack.pop_back();
                continue;
            }

            if (depth_limit_plies != 0U && frame.depth >= depth_limit_plies) {
                frame.is_depth_limited = true;
                auto& meta = graph.node_meta[frame.node_idx];
                meta.type = HUNLFlatNodeType::DepthLimited;
                graph.terminal_nodes.push_back(frame.node_idx);
                graph.terminal_node_values.push_back(meta.terminal_utility[0]);
                stack.pop_back();
                continue;
            }

            if (frame.state.cur_player == -1) {
                frame.is_chance = true;
                auto& meta = graph.node_meta[frame.node_idx];
                meta.type = HUNLFlatNodeType::Chance;
                frame.chance_outcomes = frame.state.chance_outcomes();
                if (can_collapse_public_chance(frame.state, solve_mode)) {
                    frame.public_chance_classes =
                        canonicalize_public_chance_outcomes(frame.state.board, frame.chance_outcomes);
                    frame.collapsed_public_chance = frame.public_chance_classes.size() < frame.chance_outcomes.size();
                }
                const auto branch_count = frame.collapsed_public_chance
                    ? frame.public_chance_classes.size()
                    : frame.chance_outcomes.size();
                frame.child_indices.reserve(branch_count);
                meta.child_begin = static_cast<std::uint32_t>(graph.children.size());
                meta.child_count = static_cast<std::uint32_t>(branch_count);
                meta.chance_begin = static_cast<std::uint32_t>(graph.chance_outcomes.size());
                meta.chance_count = static_cast<std::uint32_t>(branch_count);
                graph.children.resize(graph.children.size() + branch_count);
                graph.chance_outcomes.resize(graph.chance_outcomes.size() + branch_count);
                continue;
            }

            auto& meta = graph.node_meta[frame.node_idx];
            meta.type = HUNLFlatNodeType::Decision;
            meta.has_infoset = true;
            frame.actions = frame.state.legal_actions();
            if (frame.actions.empty()) {
                throw std::logic_error("decision node must expose at least one legal action");
            }

            graph.max_actions = std::max(graph.max_actions, static_cast<std::uint8_t>(frame.actions.size()));
            meta.action_count = static_cast<std::uint8_t>(frame.actions.size());
            frame.child_indices.reserve(frame.actions.size());
            meta.child_begin = static_cast<std::uint32_t>(graph.children.size());
            meta.child_count = static_cast<std::uint32_t>(frame.actions.size());
            meta.action_begin = static_cast<std::uint32_t>(graph.actions.size());
            graph.children.resize(graph.children.size() + frame.actions.size());
            graph.actions.resize(graph.actions.size() + frame.actions.size());

            const auto flat_key = make_flat_infoset_key(frame.state);
            const auto key_it = infoset_ids_by_key.find(flat_key);
            if (key_it == infoset_ids_by_key.end()) {
                const InfosetId id{static_cast<std::uint32_t>(infoset_keys.size())};
                infoset_ids_by_key.emplace(flat_key, id);
                infoset_node_lists.push_back({});
                infoset_action_counts.push_back(meta.action_count);
                infoset_players.push_back(meta.player);
                infoset_streets.push_back(meta.street);
                infoset_boards.push_back(canonical_board(frame.state.board));
                infoset_keys.push_back(flat_key);
                meta.infoset_id = id;
            } else {
                meta.infoset_id = key_it->second;
                if (infoset_action_counts[meta.infoset_id.value] != meta.action_count) {
                    throw std::logic_error("infoset nodes must agree on action_count");
                }
                if (infoset_players[meta.infoset_id.value] != meta.player) {
                    throw std::logic_error("infoset nodes must agree on owning player");
                }
                if (infoset_streets[meta.infoset_id.value] != meta.street) {
                    throw std::logic_error("infoset nodes must agree on street");
                }
                if (infoset_boards[meta.infoset_id.value] != canonical_board(frame.state.board)) {
                    throw std::logic_error("infoset nodes must agree on board");
                }
            }
            infoset_node_lists[meta.infoset_id.value].push_back(frame.node_idx);
        }

        if (frame.is_terminal || frame.is_depth_limited) {
            stack.pop_back();
            continue;
        }

        if (frame.is_chance) {
            const auto chance_branch_count = frame.collapsed_public_chance
                ? frame.public_chance_classes.size()
                : frame.chance_outcomes.size();
            if (frame.next_child < chance_branch_count) {
                const auto child_slot = frame.next_child;
                const auto outcome = frame.collapsed_public_chance
                    ? frame.chance_outcomes[frame.public_chance_classes[child_slot].representative_outcome_idx]
                    : frame.chance_outcomes[child_slot];
                ++frame.next_child;
                const auto child_state = frame.state.apply(outcome.action);
                const auto [child_idx, child_is_new] = find_or_create_node(child_state, frame.depth + 1);
                frame.child_indices.push_back(child_idx);
                auto& meta = graph.node_meta[frame.node_idx];
                const auto probability = frame.collapsed_public_chance
                    ? frame.public_chance_classes[child_slot].probability
                    : outcome.probability;
                const auto multiplicity = frame.collapsed_public_chance
                    ? frame.public_chance_classes[child_slot].multiplicity
                    : 1U;
                graph.children[meta.child_begin + child_slot] = child_idx;
                graph.chance_outcomes[meta.chance_begin + child_slot] = HUNLFlatChanceOutcome{
                    static_cast<std::uint8_t>(outcome.action),
                    probability,
                    child_idx,
                    multiplicity,
                };
                if (child_is_new) {
                    stack.push_back(HUNLFlatBuildFrame{std::move(child_state), child_idx, frame.depth + 1});
                }
                continue;
            }
            stack.pop_back();
            continue;
        }

        if (frame.next_child < frame.actions.size()) {
            const auto child_slot = frame.next_child;
            const auto action = frame.actions[child_slot];
            ++frame.next_child;
            const auto child_state = frame.state.apply(action);
            const auto [child_idx, child_is_new] = find_or_create_node(child_state, frame.depth + 1);
            frame.child_indices.push_back(child_idx);
            auto& meta = graph.node_meta[frame.node_idx];
            graph.children[meta.child_begin + child_slot] = child_idx;
            graph.actions[meta.action_begin + child_slot] = action;
            if (child_is_new) {
                stack.push_back(HUNLFlatBuildFrame{std::move(child_state), child_idx, frame.depth + 1});
            }
            continue;
        }
        stack.pop_back();
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

    finalize_graph(graph);
    return graph;
}

}  // namespace core

namespace std {

std::size_t hash<core::HUNLFlatBuilderMemoKey>::operator()(
    const core::HUNLFlatBuilderMemoKey& key) const noexcept {
    std::size_t seed = 0;
    core::detail::hash_combine(seed, key.cur_player);
    core::detail::hash_combine_range(seed, key.contributions);
    core::detail::hash_combine_range(seed, key.stacks);
    core::detail::hash_combine_range(seed, key.street_history);
    core::detail::hash_combine_range(seed, key.folded);
    core::detail::hash_combine_range(seed, key.all_in);
    core::detail::hash_combine_range(seed, key.board);
    for (const auto& hand : key.hole_cards) {
        core::detail::hash_combine_range(seed, hand);
    }
    core::detail::hash_combine_range(seed, key.street_lengths);
    core::detail::hash_combine_range(seed, key.history_codes);
    core::detail::hash_combine(seed, static_cast<std::uint8_t>(key.street));
    core::detail::hash_combine(seed, key.board_count);
    core::detail::hash_combine(seed, key.history_count);
    core::detail::hash_combine(seed, key.street_history_count);
    core::detail::hash_combine(seed, key.pending_board_deals);
    core::detail::hash_combine(seed, key.street_num_raises);
    core::detail::hash_combine(seed, key.has_hole_cards);
    core::detail::hash_combine(seed, key.to_call);
    core::detail::hash_combine(seed, key.street_aggressor);
    return seed;
}

}  // namespace std
