#include "solver/multiway_traversal.hpp"

#include <stdexcept>
#include <utility>

namespace core {

bool MultiwayExternalSamplingTraversal::append_infoset_update(
    MultiwayWorkerDeltaStream& stream,
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t trajectory_id,
    const MultiwayExternalSamplingRequest& request) {
    if (infoset.public_state.value == 0U || infoset.seat != request.traverser) {
        throw std::invalid_argument("multiway traversal update infoset must identify the traverser");
    }
    const auto update = make_multiway_external_sampling_cfr_update(request);
    if (update.regret_deltas.size() != update.strategy_deltas.size()) {
        throw std::logic_error("multiway traversal produced mismatched action deltas");
    }
    if (stream.capacity() - stream.size() < update.regret_deltas.size()) return false;
    for (std::size_t action = 0; action < update.regret_deltas.size(); ++action) {
        if (!stream.try_append({
                infoset,
                bucket,
                static_cast<std::uint8_t>(action),
                update.regret_deltas[action],
                update.strategy_deltas[action],
                trajectory_id,
            })) {
            throw std::logic_error("multiway traversal delta capacity changed during append");
        }
    }
    return true;
}

MultiwayRootExternalSamplingTraversal::MultiwayRootExternalSamplingTraversal(
    MultiwaySolverCoordinator& coordinator,
    const MultiwayRootSnapshot& root,
    const MultiwayActionAbstraction& action_abstraction,
    const MultiwayBucketRegistry& buckets,
    const MultiwayLeafEvaluator* leaf_evaluator)
    : coordinator_(&coordinator),
      root_(&root),
      action_abstraction_(&action_abstraction),
      buckets_(&buckets),
      leaf_evaluator_(leaf_evaluator) {
    root.validate();
    if (root.public_state.betting.street < Street::Flop ||
        root.public_state.betting.street > Street::River) {
        throw std::invalid_argument("multiway root traversal currently requires a postflop root");
    }
}

bool MultiwayRootExternalSamplingTraversal::run(
    PlayerId traverser,
    std::uint64_t trajectory_id,
    std::uint64_t seed,
    MultiwayWorkerDeltaStream& stream) {
    const auto& root_state = root_->public_state;
    if (traverser != root_state.betting.current_player || root_state.legal_actions.empty()) {
        throw std::invalid_argument("multiway root traversal requires the acting root traverser");
    }
    const auto& table = buckets_->table(root_state.betting.street, root_state.board);
    MultiwayTerminalAdapter terminal(*coordinator_);
    const auto deal = terminal.sample_private_deal(seed);
    const auto bucket = table.lookup(terminal.sampled_hole(deal, traverser));
    const MultiwayInfosetId infoset = {root_state.id, traverser};
    coordinator_->admit_infoset_row({
        infoset,
        table.bucket_count(),
        static_cast<std::uint8_t>(root_state.legal_actions.size()),
    });
    const auto strategy = coordinator_->storage().regret_matched_strategy(infoset, bucket);
    std::vector<Value> action_values;
    action_values.reserve(root_state.legal_actions.size());
    for (std::size_t action = 0; action < root_state.legal_actions.size(); ++action) {
        const auto next = MultiwayState::from_snapshot(root_state.betting)
            .apply(root_state.legal_actions[action].action,
                   root_state.legal_actions[action].target_street_contribution);
        std::vector<MultiwayActionDescriptor> child_actions;
        if (next.current_player() >= 0) {
            child_actions = action_abstraction_->make_legal_actions(next.snapshot(), root_->action_menu_id());
        }
        const auto child = MultiwayPublicBuilder::make_action_child(
            root_state, static_cast<std::uint32_t>(action), std::move(child_actions));
        coordinator_->admit_public_state(child);
        if (next.is_terminal()) {
            action_values.push_back(terminal.resolve_terminal(child.id, deal).utilities[traverser]);
        } else if (leaf_evaluator_ != nullptr && leaf_evaluator_->valid()) {
            action_values.push_back((*leaf_evaluator_)({&child.betting, &child.board, traverser}));
        } else {
            throw std::logic_error("multiway root traversal requires a leaf evaluator for non-terminal children");
        }
    }
    std::vector<Probability> reaches(root_state.betting.stacks.size(), 1.0);
    const auto request = terminal.make_external_sampling_request(
        deal, std::move(reaches), traverser, strategy, std::move(action_values));
    return MultiwayExternalSamplingTraversal::append_infoset_update(
        stream, infoset, bucket, trajectory_id, request);
}

namespace {

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t trajectory_id) noexcept {
    auto value = seed + 0x9e3779b97f4a7c15ULL + trajectory_id;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}  // namespace

MultiwayRootBatchRunner::MultiwayRootBatchRunner(
    MultiwayRootExternalSamplingTraversal traversal,
    MultiwaySolverCoordinator& coordinator,
    std::uint32_t worker_count,
    std::size_t worker_delta_capacity)
    : traversal_(std::move(traversal)),
      coordinator_(&coordinator),
      worker_count_(worker_count),
      worker_delta_capacity_(worker_delta_capacity) {
    if (worker_count_ == 0U || worker_delta_capacity_ == 0U) {
        throw std::invalid_argument("multiway root batch runner requires positive worker limits");
    }
}

MultiwayRootBatchResult MultiwayRootBatchRunner::run(
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t seed) {
    MultiwayRootBatchResult result;
    const auto batches = MultiwayScheduler::partition_deterministic(trajectory_count, worker_count_);
    std::vector<MultiwayWorkerDeltaStream> streams;
    streams.reserve(batches.size());
    for (const auto& batch : batches) {
        streams.emplace_back(batch.worker_index, worker_delta_capacity_);
        auto& stream = streams.back();
        for (auto local_id = batch.trajectories.begin; local_id < batch.trajectories.end; ++local_id) {
            const auto trajectory_id = first_trajectory_id + local_id;
            ++result.trajectories_attempted;
            if (traversal_.run(
                    traversal_.root_traverser(), trajectory_id, mix_seed(seed, trajectory_id), stream)) {
                ++result.trajectories_accepted;
            } else {
                ++result.trajectories_discarded;
            }
        }
        stream.sort_fixed_order();
        result.delta_entries_merged += stream.size();
    }
    coordinator_->merge_worker_streams(streams);
    return result;
}

}  // namespace core
