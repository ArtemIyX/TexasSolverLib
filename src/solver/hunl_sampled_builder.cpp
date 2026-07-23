#include "solver/hunl_sampled_builder.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

template <class T>
void hash_combine(std::size_t& seed, const T& value) noexcept {
    seed ^= std::hash<T>{}(value) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
}

template <class T, std::size_t N>
void hash_combine_array(std::size_t& seed, const std::array<T, N>& values) noexcept {
    for (const auto& value : values) {
        hash_combine(seed, value);
    }
}

std::size_t string_vector_bytes(const std::vector<std::string>& values) noexcept {
    std::size_t bytes = static_cast<std::size_t>(values.capacity()) * sizeof(std::string);
    for (const auto& value : values) {
        bytes += value.capacity();
    }
    return bytes;
}

std::size_t nested_string_vector_bytes(const std::vector<std::vector<std::string>>& values) noexcept {
    std::size_t bytes = static_cast<std::size_t>(values.capacity()) * sizeof(std::vector<std::string>);
    for (const auto& inner : values) {
        bytes += string_vector_bytes(inner);
    }
    return bytes;
}

std::size_t nested_int_vector_bytes(const std::vector<std::vector<int>>& values) noexcept {
    std::size_t bytes = static_cast<std::size_t>(values.capacity()) * sizeof(std::vector<int>);
    for (const auto& inner : values) {
        bytes += static_cast<std::size_t>(inner.capacity()) * sizeof(int);
    }
    return bytes;
}

}  // namespace

std::size_t HUNLSampledStateKeyHash::operator()(const HUNLSampledStateKey& key) const noexcept {
    std::size_t seed = 0;
    hash_combine_array(seed, key.history_codes);
    hash_combine_array(seed, key.street_lengths);
    hash_combine_array(seed, key.board.cards);
    hash_combine(seed, key.board.count);
    hash_combine_array(seed, key.contributions);
    hash_combine_array(seed, key.stacks);
    hash_combine(seed, key.to_call);
    hash_combine(seed, key.cur_player);
    hash_combine(seed, key.street_aggressor);
    hash_combine(seed, static_cast<std::uint8_t>(key.street));
    hash_combine(seed, key.street_num_raises);
    hash_combine(seed, key.pending_board_deals);
    hash_combine(seed, key.history_count);
    hash_combine_array(seed, key.folded);
    hash_combine_array(seed, key.all_in);
    return seed;
}

HUNLSampledBuilder::HUNLSampledBuilder(HUNLSampledBuilderConfig config)
    : config_(config) {
}

std::uint32_t HUNLSampledBuilder::initialize(const HUNLState& root_state) {
    clear();
    root_id_ = find_or_create(root_state);
    return root_id_;
}

void HUNLSampledBuilder::ensure_expanded(std::uint32_t node_id) {
    const auto current_snapshot = nodes_.at(node_id);
    if (current_snapshot.expanded || current_snapshot.type == HUNLFlatNodeType::TerminalFold ||
        current_snapshot.type == HUNLFlatNodeType::TerminalShowdown ||
        current_snapshot.type == HUNLFlatNodeType::DepthLimited) {
        return;
    }

    const auto state = states_.at(node_id);
    const auto edge_begin = static_cast<std::uint32_t>(edges_.size());
    std::uint16_t edge_count = 0;
    bool chance_isomorphic = false;

    if (current_snapshot.type == HUNLFlatNodeType::Chance) {
        const auto outcomes = state.chance_outcomes();
        admit_growth(static_cast<std::uint64_t>(outcomes.size()) * sizeof(HUNLSampledEdge));
        // Public-board suit symmetry alone is insufficient: fixed holes and
        // asymmetric ranges can change blockers, buckets, and reach. Until the
        // relative suit permutation is carried through private state, always
        // retain the complete chance expansion, even if the legacy flag is set.
        edges_.reserve(edges_.size() + outcomes.size());
        for (const auto& outcome : outcomes) {
            const auto child = find_or_create(state.apply(outcome.action));
            edges_.push_back(HUNLSampledEdge{
                outcome.action,
                child,
                outcome.probability,
                1U,
            });
        }
        edge_count = static_cast<std::uint16_t>(outcomes.size());
        auto& updated = nodes_.at(node_id);
        updated.edge_begin = edge_begin;
        updated.edge_count = edge_count;
        updated.chance_isomorphic = chance_isomorphic;
        updated.expanded = true;
        return;
    }

    const auto actions = state.legal_actions();
    admit_growth(static_cast<std::uint64_t>(actions.size()) * sizeof(HUNLSampledEdge));
    edges_.reserve(edges_.size() + actions.size());
    for (const auto action : actions) {
        const auto child = find_or_create(state.apply(action));
        edges_.push_back(HUNLSampledEdge{
            action,
            child,
            0.0,
            1U,
        });
    }
    edge_count = static_cast<std::uint16_t>(actions.size());
    auto& updated = nodes_.at(node_id);
    updated.edge_begin = edge_begin;
    updated.edge_count = edge_count;
    updated.chance_isomorphic = false;
    updated.expanded = true;
}

const HUNLSampledNode& HUNLSampledBuilder::node(std::uint32_t node_id) const {
    return nodes_.at(node_id);
}

HUNLSampledNode& HUNLSampledBuilder::node_mut(std::uint32_t node_id) {
    return nodes_.at(node_id);
}

const HUNLSampledEdge& HUNLSampledBuilder::edge(std::uint32_t edge_id) const {
    return edges_.at(edge_id);
}

const HUNLState& HUNLSampledBuilder::state(std::uint32_t node_id) const {
    return states_.at(node_id);
}

std::uint32_t HUNLSampledBuilder::root_id() const noexcept {
    return root_id_;
}

std::size_t HUNLSampledBuilder::node_count() const noexcept {
    return nodes_.size();
}

std::size_t HUNLSampledBuilder::edge_count() const noexcept {
    return edges_.size();
}

std::size_t HUNLSampledBuilder::infoset_count() const noexcept {
    return infoset_keys_.size();
}

HUNLSampledBuilderMemoryEstimate HUNLSampledBuilder::memory_estimate() const noexcept {
    HUNLSampledBuilderMemoryEstimate estimate;
    estimate.nodes_bytes = static_cast<std::uint64_t>(nodes_.capacity()) * sizeof(HUNLSampledNode);
    estimate.edges_bytes = static_cast<std::uint64_t>(edges_.capacity()) * sizeof(HUNLSampledEdge);
    estimate.lookup_bytes =
        static_cast<std::uint64_t>(node_lookup_.size()) *
            static_cast<std::uint64_t>(sizeof(HUNLSampledStateKey) + sizeof(std::uint32_t) + sizeof(void*) * 2U) +
        static_cast<std::uint64_t>(node_lookup_.bucket_count()) * sizeof(void*);
    estimate.infoset_bytes =
        static_cast<std::uint64_t>(infoset_lookup_.size()) *
        static_cast<std::uint64_t>(sizeof(std::string) + sizeof(InfosetId) + sizeof(void*) * 2U) +
        static_cast<std::uint64_t>(infoset_lookup_.bucket_count()) * sizeof(void*) +
        static_cast<std::uint64_t>(string_vector_bytes(infoset_keys_));
    estimate.state_cache_bytes = static_cast<std::uint64_t>(states_.capacity()) * sizeof(HUNLState);
    for (const auto& state : states_) {
        estimate.state_cache_bytes += estimate_state_bytes(state);
    }
    estimate.nodes = static_cast<std::uint64_t>(nodes_.size());
    estimate.edges = static_cast<std::uint64_t>(edges_.size());
    return estimate;
}

const HUNLSampledBuilderConfig& HUNLSampledBuilder::config() const noexcept {
    return config_;
}

void HUNLSampledBuilder::set_max_cached_public_states(std::size_t maximum) noexcept {
    config_.max_cached_public_states = maximum;
}

void HUNLSampledBuilder::set_memory_limit_bytes(std::uint64_t limit) noexcept {
    config_.memory_limit_bytes = limit;
}

void HUNLSampledBuilder::admit_growth(std::uint64_t bytes) const {
    if (config_.memory_limit_bytes == 0U) return;
    const auto current = memory_estimate().total_bytes();
    if (current > config_.memory_limit_bytes || bytes > config_.memory_limit_bytes - current) {
        throw std::runtime_error("sampled public-state growth would exceed the configured memory limit");
    }
}

void HUNLSampledBuilder::clear() noexcept {
    root_id_ = 0;
    node_lookup_.clear();
    infoset_lookup_.clear();
    infoset_keys_.clear();
    nodes_.clear();
    states_.clear();
    edges_.clear();
}

HUNLSampledStateKey HUNLSampledBuilder::make_key(const HUNLState& state) {
    HUNLSampledStateKey key;
    key.board = HUNLFlatSolveGraph::pack_board(state.board);
    key.contributions = state.contributions;
    key.stacks = state.stacks;
    key.to_call = state.to_call;
    key.cur_player = state.cur_player;
    key.street_aggressor = state.street_aggressor;
    key.street = state.street;
    key.street_num_raises = state.street_num_raises;
    key.pending_board_deals = state.pending_board_deals;
    key.folded = state.folded;
    key.all_in = state.all_in;

    if (state.betting_history_codes.size() > key.street_lengths.size()) {
        throw std::invalid_argument(
            "HUNLSampledBuilder::make_key betting history has too many streets");
    }
    if (!state.current_street_history_codes.empty() &&
        state.betting_history_codes.size() >= key.street_lengths.size()) {
        throw std::invalid_argument(
            "HUNLSampledBuilder::make_key cannot encode current history after four street segments");
    }

    std::size_t offset = 0;
    for (std::size_t street_index = 0;
         street_index < state.betting_history_codes.size();
         ++street_index) {
        const auto& codes = state.betting_history_codes[street_index];
        if (codes.size() > key.history_codes.size() - offset) {
            throw std::invalid_argument(
                "HUNLSampledBuilder::make_key exceeded history capacity");
        }
        key.street_lengths[street_index] = static_cast<std::uint8_t>(codes.size());
        for (std::size_t i = 0; i < codes.size(); ++i) {
            key.history_codes[offset++] = codes[i];
        }
    }

    const auto current_street_index =
        std::min<std::size_t>(state.betting_history_codes.size(), key.street_lengths.size() - 1U);
    if (state.current_street_history_codes.size() > key.history_codes.size() - offset) {
        throw std::invalid_argument(
            "HUNLSampledBuilder::make_key exceeded history capacity");
    }
    key.street_lengths[current_street_index] =
        static_cast<std::uint8_t>(state.current_street_history_codes.size());
    for (std::size_t i = 0; i < state.current_street_history_codes.size(); ++i) {
        key.history_codes[offset++] = state.current_street_history_codes[i];
    }
    key.history_count = static_cast<std::uint8_t>(offset);
    return key;
}

std::uint32_t HUNLSampledBuilder::find_or_create(const HUNLState& state) {
    const auto key = make_key(state);
    const auto it = node_lookup_.find(key);
    if (it != node_lookup_.end()) {
        return it->second;
    }
    if (config_.max_cached_public_states > 0U && nodes_.size() >= config_.max_cached_public_states) {
        throw std::runtime_error("sampled public-state cache admission limit reached");
    }
    admit_growth(static_cast<std::uint64_t>(sizeof(HUNLSampledNode) + sizeof(HUNLState)) +
                 estimate_state_bytes(state));

    const auto node_id = static_cast<std::uint32_t>(nodes_.size());
    HUNLSampledNode node;
    node.key = key;
    node.board = HUNLFlatSolveGraph::pack_board(state.board);
    node.contributions = state.contributions;
    node.player = state.cur_player;
    node.street = state.street;

    if (state.is_terminal()) {
        const auto utility = state.utility();
        node.terminal_utility = {
            utility.size() > 0 ? utility[0] : 0.0,
            utility.size() > 1 ? utility[1] : 0.0,
        };
        node.terminal_kind = classify_terminal_kind(state);
        node.type = node.terminal_kind.tag == TerminalKindTag::Fold
            ? HUNLFlatNodeType::TerminalFold
            : HUNLFlatNodeType::TerminalShowdown;
    } else if (state.cur_player == -1) {
        node.type = HUNLFlatNodeType::Chance;
    } else {
        node.type = HUNLFlatNodeType::Decision;
        node.infoset_id = find_or_create_infoset_id(state);
    }

    nodes_.push_back(node);
    states_.push_back(state);
    node_lookup_.emplace(key, node_id);
    return node_id;
}

InfosetId HUNLSampledBuilder::find_or_create_infoset_id(const HUNLState& state) {
    const auto key = state.infoset_key(static_cast<std::uint8_t>(state.cur_player));
    const auto it = infoset_lookup_.find(key);
    if (it != infoset_lookup_.end()) {
        return it->second;
    }

    const auto id = InfosetId{static_cast<std::uint32_t>(infoset_keys_.size())};
    infoset_keys_.push_back(key);
    infoset_lookup_.emplace(infoset_keys_.back(), id);
    return id;
}

std::uint64_t HUNLSampledBuilder::estimate_state_bytes(const HUNLState& state) noexcept {
    std::uint64_t bytes = 0;
    bytes += static_cast<std::uint64_t>(state.board.capacity()) * sizeof(std::uint8_t);
    bytes += static_cast<std::uint64_t>(state.street_history.capacity()) * sizeof(ActionId);
    bytes += static_cast<std::uint64_t>(string_vector_bytes(state.current_street_tokens));
    bytes += static_cast<std::uint64_t>(nested_string_vector_bytes(state.betting_tokens));
    bytes += static_cast<std::uint64_t>(state.current_street_history_codes.capacity()) * sizeof(int);
    bytes += static_cast<std::uint64_t>(nested_int_vector_bytes(state.betting_history_codes));
    return bytes;
}

}  // namespace core
