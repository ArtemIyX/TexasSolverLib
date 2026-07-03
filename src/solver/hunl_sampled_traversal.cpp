#include "solver/hunl_sampled_traversal.hpp"

namespace core {

void HUNLSampledWorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    strategy.clear();
}

HUNLSampledTraversal::HUNLSampledTraversal(
    HUNLSampledBuilder& builder,
    HUNLSampledStorage& storage,
    const HUNLSampledTerminalEvaluator& terminal_evaluator)
    : builder_(builder), storage_(storage), terminal_evaluator_(terminal_evaluator) {
}

HUNLSampledTraversalResult HUNLSampledTraversal::run(
    const HUNLSampledTraversalRequest& request,
    HUNLSampledWorkerScratch& scratch) {
    scratch.clear_keep_capacity();
    if (builder_.node_count() == 0 || request.root_node_id >= builder_.node_count()) {
        return {};
    }

    HUNLSampledTraversalResult result;
    auto node_id = request.root_node_id;
    while (true) {
        const auto& node = builder_.node(node_id);
        ++result.nodes_visited;

        if (node.type == HUNLFlatNodeType::Decision) {
            const auto row = storage_.view(node.infoset_id);
            if (!row.empty()) {
                ++result.infosets_updated;
            }
        }

        if (node.type == HUNLFlatNodeType::TerminalFold) {
            HUNLSampledTerminalInput input;
            input.contributions = node.contributions;
            input.traversing_player = request.traversing_player;
            result.value = terminal_evaluator_.evaluate_fold(input, node.player);
            return result;
        }
        if (node.type == HUNLFlatNodeType::TerminalShowdown) {
            HUNLSampledTerminalInput input;
            input.contributions = node.contributions;
            input.traversing_player = request.traversing_player;
            result.value = terminal_evaluator_.evaluate_showdown(input, 0.0);
            return result;
        }

        builder_.ensure_expanded(node_id);
        const auto& expanded = builder_.node(node_id);
        if (expanded.edge_count == 0) {
            return result;
        }

        const auto selector = static_cast<std::uint32_t>(request.trajectory_id % expanded.edge_count);
        const auto& edge = builder_.edge(expanded.edge_begin + selector);
        node_id = edge.child;
    }
}

}  // namespace core
