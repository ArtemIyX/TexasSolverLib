#include "solver/hunl_sampled_traversal.hpp"

namespace core {

void HUNLSampledWorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    strategy.clear();
}

HUNLSampledTraversal::HUNLSampledTraversal(
    const HUNLSampledBuilder& builder,
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

    const auto& root = builder_.node(request.root_node_id);
    HUNLSampledTraversalResult result;
    result.nodes_visited = 1;

    const auto row = storage_.view(root.infoset_id);
    if (!row.empty()) {
        result.infosets_updated = 1;
    }

    if (root.type == HUNLFlatNodeType::TerminalFold) {
        HUNLSampledTerminalInput input;
        input.contributions = root.contributions;
        input.traversing_player = request.traversing_player;
        result.value = terminal_evaluator_.evaluate_fold(input, root.player);
    } else if (root.type == HUNLFlatNodeType::TerminalShowdown) {
        HUNLSampledTerminalInput input;
        input.contributions = root.contributions;
        input.traversing_player = request.traversing_player;
        result.value = terminal_evaluator_.evaluate_showdown(input, 0.0);
    }

    return result;
}

}  // namespace core
