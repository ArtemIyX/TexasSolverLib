#include "solver/hunl_sampled_range.hpp"

#include "solver/hunl_sampled_traversal.hpp"
#include "util/pcs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {
namespace {

constexpr std::size_t kMaxDecisionActions = 16U;
constexpr std::size_t kDeltaEntriesPerTrajectory = 4096U;

struct RangeReach {
    std::array<double, 2> player = {1.0, 1.0};
    double chance = 1.0;
    double sampling = 1.0;
};

struct PrivateInfoset {
    InfosetId id{};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint8_t action_count = 0;
};

class PrivateInfosetCoordinator {
public:
    explicit PrivateInfosetCoordinator(HUNLSampledStorage& storage) : storage_(storage) {
        infosets_.reserve(1024U);
    }

    [[nodiscard]] InfosetId ensure(const HUNLState& state) {
        const auto player = state.current_player();
        if (player < 0 || player > 1 || !state.hole_cards.has_value()) {
            throw std::logic_error("structured sampled traversal requires an acting private state");
        }
        const auto actions = state.legal_actions();
        if (actions.empty() || actions.size() > kMaxDecisionActions) {
            throw std::logic_error("structured sampled traversal has an invalid action menu");
        }
        const auto key = state.infoset_encoding(player);
        const auto existing = infosets_.find(key);
        if (existing != infosets_.end()) {
            if (existing->second.player != player || existing->second.street != state.street ||
                existing->second.action_count != actions.size()) {
                throw std::logic_error("structured sampled traversal reused an incompatible private infoset");
            }
            return existing->second.id;
        }
        if (infosets_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::length_error("structured sampled private infoset id space exhausted");
        }
        const auto id = InfosetId{static_cast<std::uint32_t>(infosets_.size())};
        storage_.ensure_row({id, player, state.street, 1U, static_cast<std::uint8_t>(actions.size())});
        infosets_.emplace(key, PrivateInfoset{id, player, state.street, static_cast<std::uint8_t>(actions.size())});
        return id;
    }

private:
    HUNLSampledStorage& storage_;
    std::unordered_map<HUNLInfosetEncoding, PrivateInfoset, HUNLInfosetEncodingHash> infosets_;
};

void append_delta(
    HUNLSampledWorkerScratch& scratch,
    std::uint64_t trajectory_id,
    InfosetId infoset,
    std::size_t action,
    double regret,
    double strategy_sum) {
    if (!std::isfinite(regret) || !std::isfinite(strategy_sum) ||
        scratch.deltas.size() == scratch.deltas.capacity()) {
        throw std::runtime_error("structured sampled traversal delta stream capacity exhausted");
    }
    scratch.deltas.push_back({infoset, 0U, static_cast<std::uint8_t>(action), regret, strategy_sum, trajectory_id});
}

std::pair<std::size_t, double> sample_probability(
    const std::vector<ChanceOutcome>& outcomes,
    PcsRng& rng) {
    double total = 0.0;
    for (const auto& outcome : outcomes) {
        if (!std::isfinite(outcome.probability) || outcome.probability < 0.0) {
            throw std::logic_error("structured sampled traversal received an invalid chance probability");
        }
        total += outcome.probability;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::logic_error("structured sampled traversal chance distribution is empty");
    }
    auto draw = rng.next_unit_f64() * total;
    for (std::size_t index = 0; index < outcomes.size(); ++index) {
        if (outcomes[index].probability > 0.0 && draw < outcomes[index].probability) {
            return {index, outcomes[index].probability / total};
        }
        draw -= outcomes[index].probability;
    }
    for (std::size_t index = outcomes.size(); index > 0; --index) {
        if (outcomes[index - 1U].probability > 0.0) {
            return {index - 1U, outcomes[index - 1U].probability / total};
        }
    }
    throw std::logic_error("structured sampled traversal has no selectable chance outcome");
}

std::pair<std::size_t, double> sample_strategy(
    const std::array<float, kMaxDecisionActions>& strategy,
    std::size_t action_count,
    PcsRng& rng) {
    auto draw = rng.next_unit_f64();
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto probability = static_cast<double>(strategy[action]);
        if (draw < probability) return {action, probability};
        draw -= probability;
    }
    for (std::size_t action = action_count; action > 0; --action) {
        if (strategy[action - 1U] > 0.0F) return {action - 1U, static_cast<double>(strategy[action - 1U])};
    }
    throw std::logic_error("structured sampled traversal has no selectable strategy action");
}

std::size_t sample_deal_index(
    const std::vector<HUNLJointRangeDeal>& deals,
    PcsRng& rng) {
    if (deals.empty()) {
        throw std::logic_error("structured sampled root has no compatible private deals");
    }
    const auto draw = rng.next_unit_f64();
    double cumulative = 0.0;
    for (std::size_t index = 0; index < deals.size(); ++index) {
        cumulative += deals[index].weight;
        if (draw < cumulative) return index;
    }
    return deals.size() - 1U;
}

double convert_terminal_value(
    double big_blinds,
    const HUNLStructuredRootRequest& root) {
    if (root.value_units == HUNLLeafValueUnits::BigBlinds) return big_blinds;
    if (root.value_units == HUNLLeafValueUnits::Chips) {
        return big_blinds * static_cast<double>(root.config.big_blind);
    }
    throw std::logic_error("structured sampled traversal has unsupported terminal value units");
}

double traverse(
    const HUNLState& state,
    const HUNLStructuredRootRequest& root,
    const HUNLLeafEvaluator* leaf_evaluator,
    PlayerId traverser,
    std::uint64_t trajectory_id,
    PrivateInfosetCoordinator& infosets,
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch,
    PcsRng& rng,
    RangeReach reach,
    std::uint32_t plies,
    HUNLSampledTraversalResult& result) {
    ++result.nodes_visited;
    if (state.is_terminal()) {
        const auto values = state.utility();
        if (values.size() != 2U) throw std::logic_error("structured sampled terminal has invalid utility arity");
        return convert_terminal_value(values[static_cast<std::size_t>(traverser)], root);
    }
    if (root.config.depth_limit_plies != 0U && plies >= root.config.depth_limit_plies) {
        if (leaf_evaluator == nullptr || !leaf_evaluator->valid()) {
            throw std::invalid_argument(
                "structured sampled depth limit requires a typed leaf evaluator");
        }
        HUNLLeafEvaluationRequest request;
        request.public_state = state;
        request.public_state.hole_cards.reset();
        request.private_hole_cards = *state.hole_cards;
        request.bucket_reach[0] = {reach.player[0]};
        request.bucket_reach[1] = {reach.player[1]};
        request.scope = HUNLLeafEvaluationScope::DealConditional;
        request.units = root.value_units;
        request.abstraction_version = root.blueprint_version;
        request.model_version = root.model_version;
        HUNLLeafEvaluationResult leaf;
        if (!leaf_evaluator->evaluate_batch(leaf_evaluator->context, &request, &leaf, 1U) ||
            leaf.units != root.value_units ||
            !std::isfinite(leaf.values[0]) || !std::isfinite(leaf.values[1])) {
            throw std::runtime_error("structured sampled leaf evaluator rejected a depth cutoff");
        }
        return leaf.values[static_cast<std::size_t>(traverser)];
    }
    if (state.current_player() == -1) {
        const auto outcomes = state.chance_outcomes();
        const auto selected = sample_probability(outcomes, rng);
        ++result.chance_nodes_sampled;
        reach.chance *= selected.second;
        reach.sampling *= selected.second;
        return traverse(state.apply(outcomes[selected.first].action), root, leaf_evaluator, traverser,
                        trajectory_id, infosets, storage, scratch, rng, reach, plies + 1U, result);
    }

    const auto acting_player = state.current_player();
    const auto acting_index = static_cast<std::size_t>(acting_player);
    const auto actions = state.legal_actions();
    if (actions.empty() || actions.size() > kMaxDecisionActions ||
        !std::isfinite(reach.sampling) || reach.sampling <= 0.0) {
        throw std::logic_error("structured sampled traversal has invalid decision state");
    }
    const auto infoset = infosets.ensure(state);
    const auto row = storage.view(infoset);
    std::array<float, kMaxDecisionActions> strategy = {};
    HUNLSampledStorage::compute_current_strategy(row, 0U, strategy.data());
    const auto strategy_weight = reach.player[acting_index] / reach.sampling;
    if (!std::isfinite(strategy_weight)) throw std::overflow_error("structured sampled strategy weight is non-finite");

    if (acting_player != traverser) {
        const auto selected = sample_strategy(strategy, actions.size(), rng);
        ++result.opponent_nodes_sampled;
        for (std::size_t action = 0; action < actions.size(); ++action) {
            append_delta(scratch, trajectory_id, infoset, action, 0.0,
                         strategy_weight * static_cast<double>(strategy[action]));
        }
        ++result.infosets_updated;
        reach.player[acting_index] *= selected.second;
        reach.sampling *= selected.second;
        return traverse(state.apply(actions[selected.first]), root, leaf_evaluator, traverser, trajectory_id,
                        infosets, storage, scratch, rng, reach, plies + 1U, result);
    }

    std::array<double, kMaxDecisionActions> action_values = {};
    double node_value = 0.0;
    for (std::size_t action = 0; action < actions.size(); ++action) {
        auto child_reach = reach;
        child_reach.player[acting_index] *= static_cast<double>(strategy[action]);
        action_values[action] = traverse(state.apply(actions[action]), root, leaf_evaluator, traverser,
                                         trajectory_id, infosets, storage, scratch, rng, child_reach,
                                         plies + 1U, result);
        node_value += static_cast<double>(strategy[action]) * action_values[action];
    }
    const auto other = 1U - acting_index;
    const auto counterfactual_reach = reach.chance * reach.player[other];
    const auto importance_weight = counterfactual_reach / reach.sampling;
    if (!std::isfinite(importance_weight)) throw std::overflow_error("structured sampled regret weight is non-finite");
    for (std::size_t action = 0; action < actions.size(); ++action) {
        append_delta(scratch, trajectory_id, infoset, action,
                     importance_weight * (action_values[action] - node_value),
                     strategy_weight * static_cast<double>(strategy[action]));
    }
    ++result.infosets_updated;
    return node_value;
}

HUNLState root_for_deal(
    const std::shared_ptr<const HUNLConfig>& config,
    const HUNLJointRangeDeal& deal) {
    return HUNLState::initial(config).clone_with_hole_cards(deal.hole);
}

const HUNLJointRangeDeal& first_deal(const std::vector<HUNLJointRangeDeal>& deals) {
    if (deals.empty()) throw std::logic_error("structured sampled root has no compatible private deals");
    return deals.front();
}

}  // namespace

struct HUNLSampledRangeSession::Impl {
    HUNLStructuredRootRequest root;
    HUNLSampledSolverConfig config;
    HUNLSampledStorage& storage;
    HUNLSampledProfile& profile;
    const HUNLLeafEvaluator* leaf_evaluator = nullptr;
    std::shared_ptr<const HUNLConfig> game_config;
    std::vector<HUNLJointRangeDeal> deals;
    PrivateInfosetCoordinator infosets;
    HUNLState root_state;
    std::vector<ActionId> root_actions;
    std::uint32_t next_batch = 0;

    Impl(
        HUNLStructuredRootRequest input_root,
        HUNLSampledSolverConfig input_config,
        HUNLSampledStorage& input_storage,
        HUNLSampledProfile& input_profile,
        std::uint32_t first_batch,
        const HUNLLeafEvaluator* input_leaf_evaluator)
        : root(std::move(input_root)),
          config(std::move(input_config)),
          storage(input_storage),
          profile(input_profile),
          leaf_evaluator(input_leaf_evaluator),
          game_config(std::make_shared<const HUNLConfig>(root.config)),
          deals(root.normalized_joint_range()),
          infosets(storage),
          root_state(root_for_deal(game_config, first_deal(deals))),
          root_actions(root_state.legal_actions()),
          next_batch(first_batch) {
        validate_sampled_config_or_throw(config);
        root.validate();
        if (root.config.depth_limit_plies != 0U &&
            (leaf_evaluator == nullptr || !leaf_evaluator->valid())) {
            throw std::invalid_argument(
                "structured sampled depth limit requires a typed leaf evaluator");
        }
        if (deals.empty() || root_actions.empty()) {
            throw std::logic_error("structured sampled root has no compatible deal or legal root action");
        }
    }

    HUNLSampledRootStrategy export_root_strategy() {
        auto output = HUNLSampledStrategyExporter::export_uniform(
            static_cast<std::uint8_t>(root_actions.size()));
        std::vector<double> probabilities(root_actions.size(), 0.0);
        std::vector<int> target_contributions(root_actions.size(), 0);
        for (const auto& deal : deals) {
            const auto state = root_for_deal(game_config, deal);
            const auto infoset = infosets.ensure(state);
            const auto strategy = HUNLSampledStrategyExporter::export_average_strategy(storage.view(infoset));
            for (std::size_t action = 0; action < probabilities.size(); ++action) {
                probabilities[action] += deal.weight * strategy.actions[action].probability;
            }
        }
        for (std::size_t action = 0; action < output.actions.size(); ++action) {
            output.actions[action].probability = probabilities[action];
            const auto child = root_state.next_state(root_actions[action]);
            target_contributions[action] = root_state.current_player() >= 0
                ? child.contributions[static_cast<std::size_t>(root_state.current_player())]
                : 0;
        }
        HUNLSampledStrategyExporter::attach_action_descriptors(
            output, root_actions, target_contributions);
        return output;
    }

    HUNLSampledRangeRunResult resume_batches(
        std::uint32_t batch_count,
        std::optional<std::chrono::steady_clock::time_point> deadline) {
        HUNLSampledRangeRunResult output;
        for (std::uint32_t offset = 0; offset < batch_count; ++offset) {
            if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline) {
                output.timed_out = true;
                break;
            }
            const auto batch = next_batch;
            HUNLSampledWorkerScratch batch_stream;
            batch_stream.reserve_deltas(
                static_cast<std::size_t>(config.minibatch_size) * kDeltaEntriesPerTrajectory);
            std::uint64_t nodes = 0;
            std::uint64_t updated = 0;
            for (std::uint32_t local = 0; local < config.minibatch_size; ++local) {
                const auto trajectory_id = static_cast<std::uint64_t>(batch) * config.minibatch_size + local;
                const auto traverser = static_cast<PlayerId>(trajectory_id & 1U);
                const auto seed = PcsRng::mix_seed(config.seed, trajectory_id, batch + 1U,
                                                    static_cast<std::uint64_t>(traverser));
                PcsRng rng(seed);
                const auto deal_index = sample_deal_index(deals, rng);
                HUNLSampledWorkerScratch trajectory_stream;
                trajectory_stream.reserve_deltas(kDeltaEntriesPerTrajectory);
                HUNLSampledTraversalResult result;
                const auto chance = deals[deal_index].weight;
                (void)traverse(root_for_deal(game_config, deals[deal_index]), root, leaf_evaluator, traverser,
                               trajectory_id, infosets, storage, trajectory_stream, rng,
                               RangeReach{{1.0, 1.0}, chance, chance}, 0U, result);
                batch_stream.deltas.insert(
                    batch_stream.deltas.end(), trajectory_stream.deltas.begin(), trajectory_stream.deltas.end());
                nodes += result.nodes_visited;
                updated += result.infosets_updated;
            }
            merge_hunl_sampled_worker_deltas(storage, batch_stream);
            profile.record_traversal(config.minibatch_size, nodes, updated);
            ++next_batch;
            ++output.batches_completed;
        }
        output.root_strategy = export_root_strategy();
        return output;
    }
};

HUNLSampledRangeSession::HUNLSampledRangeSession(
    HUNLStructuredRootRequest root,
    HUNLSampledSolverConfig config,
    HUNLSampledStorage& storage,
    HUNLSampledProfile& profile,
    std::uint32_t first_batch,
    const HUNLLeafEvaluator* leaf_evaluator)
    : impl_(std::make_unique<Impl>(
          std::move(root), std::move(config), storage, profile, first_batch, leaf_evaluator)) {}

HUNLSampledRangeSession::~HUNLSampledRangeSession() = default;
HUNLSampledRangeSession::HUNLSampledRangeSession(HUNLSampledRangeSession&&) noexcept = default;
HUNLSampledRangeSession& HUNLSampledRangeSession::operator=(HUNLSampledRangeSession&&) noexcept = default;

HUNLSampledRangeRunResult HUNLSampledRangeSession::resume_batches(
    std::uint32_t batch_count,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    return impl_->resume_batches(batch_count, deadline);
}

std::uint32_t HUNLSampledRangeSession::next_batch() const noexcept {
    return impl_ == nullptr ? 0U : impl_->next_batch;
}

HUNLSampledRootStrategy HUNLSampledRangeSession::export_root_strategy() {
    return impl_->export_root_strategy();
}

HUNLSampledRangeRunResult run_hunl_sampled_structured_range_batches(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config,
    std::uint32_t first_batch,
    std::uint32_t batch_count,
    std::optional<std::chrono::steady_clock::time_point> deadline,
    HUNLSampledStorage& storage,
    HUNLSampledProfile& profile,
    const HUNLLeafEvaluator* leaf_evaluator) {
    HUNLSampledRangeSession session(root, config, storage, profile, first_batch, leaf_evaluator);
    return session.resume_batches(batch_count, deadline);
}

}  // namespace core
