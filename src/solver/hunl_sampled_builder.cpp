#include "solver/hunl_sampled_builder.hpp"

#include <stdexcept>

namespace core {

std::uint32_t HUNLSampledBuilder::find_or_create(const HUNLSampledStateKey& key) {
    const auto it = node_lookup_.find(key);
    if (it != node_lookup_.end()) {
        return it->second;
    }

    const auto node_id = static_cast<std::uint32_t>(nodes_.size());
    HUNLSampledNode node;
    node.key = key;
    nodes_.push_back(node);
    node_lookup_.emplace(key, node_id);
    return node_id;
}

const HUNLSampledNode& HUNLSampledBuilder::node(std::uint32_t node_id) const {
    return nodes_.at(node_id);
}

HUNLSampledNode& HUNLSampledBuilder::node_mut(std::uint32_t node_id) {
    return nodes_.at(node_id);
}

std::size_t HUNLSampledBuilder::node_count() const noexcept {
    return nodes_.size();
}

}  // namespace core
