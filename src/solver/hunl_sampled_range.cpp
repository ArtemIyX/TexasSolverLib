#include "solver/hunl_sampled_range.hpp"

#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_traversal.hpp"
#include "util/pcs.hpp"
#include "util/thread_join_guard.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {
namespace {

constexpr std::size_t kMaxDecisionActions = 16U;
constexpr std::size_t kDeltaEntriesPerTrajectory = 4096U;
constexpr std::uint32_t kTrajectorySubbatchSize = 8U;
constexpr auto kRootExportReserve = std::chrono::milliseconds{1};

struct RangeReach {
    std::array<double, 2> player = {1.0, 1.0};
    double chance = 1.0;
    double sampling = 1.0;
};

[[nodiscard]] bool deadline_expired(
    const std::optional<std::chrono::steady_clock::time_point>& deadline) noexcept {
    return deadline.has_value() && std::chrono::steady_clock::now() >= *deadline;
}

[[nodiscard]] bool reserve_expired(
    const std::optional<std::chrono::steady_clock::time_point>& deadline) noexcept {
    return deadline.has_value() && std::chrono::steady_clock::now() + kRootExportReserve >= *deadline;
}

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

    [[nodiscard]] InfosetId admit(const HUNLState& state) {
        const auto player = validate_state(state);
        const auto actions = state.legal_actions();
        validate_actions(actions);
        const auto key = state.infoset_encoding(player);
        const auto existing = infosets_.find(key);
        if (existing != infosets_.end()) {
            validate_existing(existing->second, player, state.street, actions.size());
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

    [[nodiscard]] InfosetId lookup(
        const HUNLState& state,
        PlayerId player,
        std::size_t action_count) const {
        const auto key = state.infoset_encoding(player);
        const auto existing = infosets_.find(key);
        if (existing == infosets_.end()) {
            throw std::logic_error("structured sampled traversal reached an unadmitted private infoset");
        }
        validate_existing(existing->second, player, state.street, action_count);
        return existing->second.id;
    }

private:
    static PlayerId validate_state(const HUNLState& state) {
        const auto player = state.current_player();
        if (player < 0 || player > 1 || !state.hole_cards.has_value()) {
            throw std::logic_error("structured sampled traversal requires an acting private state");
        }
        return player;
    }

    static void validate_actions(const std::vector<ActionId>& actions) {
        if (actions.empty() || actions.size() > kMaxDecisionActions) {
            throw std::logic_error("structured sampled traversal has an invalid action menu");
        }
    }

    static void validate_existing(
        const PrivateInfoset& infoset,
        PlayerId player,
        Street street,
        std::size_t action_count) {
        if (infoset.player != player || infoset.street != street || infoset.action_count != action_count) {
            throw std::logic_error("structured sampled traversal reused an incompatible private infoset");
        }
    }

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
    const PrivateInfosetCoordinator& infosets,
    const HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch,
    PcsRng& rng,
    RangeReach reach,
    std::uint32_t plies,
    std::mutex& leaf_evaluator_mutex,
    const std::optional<std::chrono::steady_clock::time_point>& deadline,
    bool& cancelled,
    HUNLSampledTraversalResult& result) {
    if (deadline_expired(deadline)) {
        cancelled = true;
        return 0.0;
    }
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
        request.deadline = deadline;
        request.abstraction_version = root.blueprint_version;
        request.model_version = root.model_version;
        HUNLLeafEvaluationResult leaf;
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(leaf_evaluator_mutex);
            if (deadline_expired(deadline)) {
                cancelled = true;
                return 0.0;
            }
            accepted = leaf_evaluator->evaluate_batch(leaf_evaluator->context, &request, &leaf, 1U);
        }
        if (deadline_expired(deadline)) {
            cancelled = true;
            return 0.0;
        }
        if (!accepted ||
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
                        trajectory_id, infosets, storage, scratch, rng, reach, plies + 1U, leaf_evaluator_mutex, deadline,
                        cancelled, result);
    }

    const auto acting_player = state.current_player();
    const auto acting_index = static_cast<std::size_t>(acting_player);
    const auto actions = state.legal_actions();
    if (actions.empty() || actions.size() > kMaxDecisionActions ||
        !std::isfinite(reach.sampling) || reach.sampling <= 0.0) {
        throw std::logic_error("structured sampled traversal has invalid decision state");
    }
    const auto infoset = infosets.lookup(state, acting_player, actions.size());
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
                        infosets, storage, scratch, rng, reach, plies + 1U, leaf_evaluator_mutex, deadline, cancelled, result);
    }

    std::array<double, kMaxDecisionActions> action_values = {};
    double node_value = 0.0;
    for (std::size_t action = 0; action < actions.size(); ++action) {
        auto child_reach = reach;
        child_reach.player[acting_index] *= static_cast<double>(strategy[action]);
        action_values[action] = traverse(state.apply(actions[action]), root, leaf_evaluator, traverser,
                                         trajectory_id, infosets, storage, scratch, rng, child_reach,
                                         plies + 1U, leaf_evaluator_mutex, deadline, cancelled, result);
        if (cancelled) return 0.0;
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

bool admit_trajectory(
    const HUNLState& state,
    const HUNLStructuredRootRequest& root,
    PlayerId traverser,
    PrivateInfosetCoordinator& infosets,
    const HUNLSampledStorage& storage,
    PcsRng& rng,
    std::uint32_t plies,
    const std::optional<std::chrono::steady_clock::time_point>& deadline) {
    if (deadline_expired(deadline) || state.is_terminal() ||
        (root.config.depth_limit_plies != 0U && plies >= root.config.depth_limit_plies)) {
        return !deadline_expired(deadline);
    }
    if (state.current_player() == -1) {
        const auto outcomes = state.chance_outcomes();
        const auto selected = sample_probability(outcomes, rng);
        return admit_trajectory(
            state.apply(outcomes[selected.first].action),
            root,
            traverser,
            infosets,
            storage,
            rng,
            plies + 1U,
            deadline);
    }

    const auto acting_player = state.current_player();
    const auto actions = state.legal_actions();
    if (actions.empty() || actions.size() > kMaxDecisionActions) {
        throw std::logic_error("structured sampled admission has an invalid action menu");
    }
    const auto infoset = infosets.admit(state);
    const auto row = storage.view(infoset);
    std::array<float, kMaxDecisionActions> strategy = {};
    HUNLSampledStorage::compute_current_strategy(row, 0U, strategy.data());

    if (acting_player != traverser) {
        const auto selected = sample_strategy(strategy, actions.size(), rng);
        return admit_trajectory(
            state.apply(actions[selected.first]),
            root,
            traverser,
            infosets,
            storage,
            rng,
            plies + 1U,
            deadline);
    }
    for (std::size_t action = 0; action < actions.size(); ++action) {
        if (!admit_trajectory(
                state.apply(actions[action]),
                root,
                traverser,
                infosets,
                storage,
                rng,
                plies + 1U,
                deadline)) {
            return false;
        }
    }
    return true;
}

HUNLState root_for_deal(
    const HUNLState& public_root_state,
    const HUNLJointRangeDeal& deal) {
    return public_root_state.clone_with_hole_cards(deal.hole);
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
    std::mutex leaf_evaluator_mutex;
    std::shared_ptr<const HUNLConfig> game_config;
    HUNLState public_root_state;
    std::vector<HUNLJointRangeDeal> deals;
    PrivateInfosetCoordinator infosets;
    HUNLState root_state;
    std::vector<ActionId> root_actions;
    HUNLSampledRootStrategy last_clean_root_strategy;
    std::uint64_t next_batch = 0;
    std::uint32_t next_local_trajectory = 0;

    Impl(
        HUNLStructuredRootRequest input_root,
        HUNLSampledSolverConfig input_config,
        HUNLSampledStorage& input_storage,
        HUNLSampledProfile& input_profile,
        std::uint64_t first_batch,
        const HUNLLeafEvaluator* input_leaf_evaluator)
        : root(std::move(input_root)),
          config(std::move(input_config)),
          storage(input_storage),
          profile(input_profile),
          leaf_evaluator(input_leaf_evaluator),
          game_config(std::make_shared<const HUNLConfig>(root.config)),
          public_root_state(root.public_root_state(game_config)),
          deals(root.normalized_joint_range()),
          infosets(storage),
          root_state(root_for_deal(public_root_state, first_deal(deals))),
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
        last_clean_root_strategy = export_root_strategy();
    }

    bool export_root_strategy_until(
        const std::optional<std::chrono::steady_clock::time_point>& deadline,
        HUNLSampledRootStrategy& output) {
        if (deadline_expired(deadline)) return false;
        auto exported = HUNLSampledStrategyExporter::export_uniform(
            static_cast<std::uint8_t>(root_actions.size()));
        std::vector<double> probabilities(root_actions.size(), 0.0);
        std::vector<int> target_contributions(root_actions.size(), 0);
        for (const auto& deal : deals) {
            if (deadline_expired(deadline)) return false;
            const auto state = root_for_deal(public_root_state, deal);
            const auto infoset = infosets.admit(state);
            const auto strategy = HUNLSampledStrategyExporter::export_average_strategy(storage.view(infoset));
            for (std::size_t action = 0; action < probabilities.size(); ++action) {
                probabilities[action] += deal.weight * strategy.actions[action].probability;
            }
        }
        for (std::size_t action = 0; action < exported.actions.size(); ++action) {
            exported.actions[action].probability = probabilities[action];
            const auto child = root_state.next_state(root_actions[action]);
            target_contributions[action] = root_state.current_player() >= 0
                ? child.contributions[static_cast<std::size_t>(root_state.current_player())]
                : 0;
        }
        HUNLSampledStrategyExporter::attach_action_descriptors(
            exported, root_actions, target_contributions);
        if (deadline_expired(deadline)) return false;
        output = std::move(exported);
        return true;
    }

    HUNLSampledRootStrategy export_root_strategy() {
        HUNLSampledRootStrategy output;
        if (!export_root_strategy_until(std::nullopt, output)) {
            throw std::logic_error("unbounded structured root export was cancelled");
        }
        last_clean_root_strategy = output;
        return output;
    }

    HUNLSampledRangeRunResult resume_batches(
        std::uint32_t batch_count,
        std::optional<std::chrono::steady_clock::time_point> deadline) {
        HUNLSampledRangeRunResult output;
        output.root_strategy = last_clean_root_strategy;
        for (std::uint32_t offset = 0; offset < batch_count; ++offset) {
            if (reserve_expired(deadline)) {
                output.timed_out = true;
                break;
            }
            const auto batch = next_batch;
            if (batch == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("structured sampled batch id space exhausted");
            }
            while (next_local_trajectory < config.minibatch_size) {
                if (reserve_expired(deadline)) {
                    output.timed_out = true;
                    break;
                }
                const auto remaining = config.minibatch_size - next_local_trajectory;
                const auto requested_workers = std::max<std::size_t>(1U, config.workers);
                const auto subbatch_count = std::min(remaining, kTrajectorySubbatchSize);
                const auto subbatch_begin = next_local_trajectory;

                // Coordinator admission consumes the same per-trajectory RNG
                // stream as execution. All rows are therefore immutable before
                // a worker enters recursive traversal.
                for (std::uint32_t offset_in_batch = 0; offset_in_batch < subbatch_count; ++offset_in_batch) {
                    const auto local = subbatch_begin + offset_in_batch;
                    if (batch > (std::numeric_limits<std::uint64_t>::max() - local) / config.minibatch_size) {
                        throw std::overflow_error("structured sampled trajectory id space exhausted");
                    }
                    const auto trajectory_id =
                        batch * static_cast<std::uint64_t>(config.minibatch_size) + local;
                    const auto traverser = static_cast<PlayerId>(trajectory_id & 1U);
                    const auto seed = PcsRng::mix_seed(
                        config.seed,
                        trajectory_id,
                        batch + 1U,
                        static_cast<std::uint64_t>(traverser));
                    PcsRng rng(seed);
                    const auto deal_index = deals.size() == 1U ? 0U : sample_deal_index(deals, rng);
                    if (!admit_trajectory(
                            root_for_deal(public_root_state, deals[deal_index]),
                            root,
                            traverser,
                            infosets,
                            storage,
                            rng,
                            0U,
                            deadline)) {
                        output.timed_out = true;
                        break;
                    }
                }
                if (output.timed_out || reserve_expired(deadline)) {
                    output.timed_out = true;
                    break;
                }

                const auto worker_batches = HUNLSampledScheduler::partition_deterministic(
                    subbatch_count,
                    std::min<std::size_t>(requested_workers, subbatch_count));
                std::vector<HUNLSampledWorkerScratch> worker_streams(worker_batches.size());
                std::vector<HUNLSampledTraversalResult> worker_results(worker_batches.size());
                std::vector<std::thread> threads;
                threads.reserve(worker_batches.size() > 0U ? worker_batches.size() - 1U : 0U);
                std::atomic<bool> cancelled{false};
                auto thread_guard = detail::make_thread_join_guard(
                    threads,
                    [&cancelled] { cancelled.store(true, std::memory_order_release); });
                std::exception_ptr worker_error;
                std::mutex worker_error_mutex;
                const auto execute_worker = [&](std::size_t worker_index) {
                    try {
                        const auto range = worker_batches[worker_index].trajectories;
                        auto& stream = worker_streams[worker_index];
                        stream.reserve_deltas(
                            static_cast<std::size_t>(range.size()) * kDeltaEntriesPerTrajectory);
                        for (std::uint64_t relative = range.begin; relative < range.end; ++relative) {
                            if (cancelled.load(std::memory_order_acquire)) return;
                            const auto local = subbatch_begin + static_cast<std::uint32_t>(relative);
                            const auto trajectory_id =
                                batch * static_cast<std::uint64_t>(config.minibatch_size) + local;
                            const auto traverser = static_cast<PlayerId>(trajectory_id & 1U);
                            const auto seed = PcsRng::mix_seed(
                                config.seed,
                                trajectory_id,
                                batch + 1U,
                                static_cast<std::uint64_t>(traverser));
                            PcsRng rng(seed);
                            const auto deal_index = deals.size() == 1U ? 0U : sample_deal_index(deals, rng);
                            HUNLSampledWorkerScratch trajectory_stream;
                            trajectory_stream.reserve_deltas(kDeltaEntriesPerTrajectory);
                            HUNLSampledTraversalResult result;
                            bool trajectory_cancelled = false;
                            const auto chance = deals.size() == 1U ? 1.0 : deals[deal_index].weight;
                            (void)traverse(
                                root_for_deal(public_root_state, deals[deal_index]),
                                root,
                                leaf_evaluator,
                                traverser,
                                trajectory_id,
                                infosets,
                                storage,
                                trajectory_stream,
                                rng,
                                RangeReach{{1.0, 1.0}, chance, chance},
                                0U,
                                leaf_evaluator_mutex,
                                deadline,
                                trajectory_cancelled,
                                result);
                            if (trajectory_cancelled) {
                                cancelled.store(true, std::memory_order_release);
                                return;
                            }
                            stream.deltas.insert(
                                stream.deltas.end(),
                                trajectory_stream.deltas.begin(),
                                trajectory_stream.deltas.end());
                            worker_results[worker_index].nodes_visited += result.nodes_visited;
                            worker_results[worker_index].infosets_updated += result.infosets_updated;
                        }
                    } catch (...) {
                        cancelled.store(true, std::memory_order_release);
                        std::lock_guard<std::mutex> lock(worker_error_mutex);
                        if (worker_error == nullptr) worker_error = std::current_exception();
                    }
                };
                for (std::size_t worker = 1U; worker < worker_batches.size(); ++worker) {
                    threads.emplace_back(execute_worker, worker);
                }
                if (!worker_batches.empty()) execute_worker(0U);
                for (auto& thread : threads) thread.join();
                thread_guard.release();
                if (worker_error != nullptr) std::rethrow_exception(worker_error);
                if (cancelled.load(std::memory_order_acquire) || reserve_expired(deadline)) {
                    output.timed_out = true;
                    break;
                }

                merge_hunl_sampled_worker_streams(storage, worker_streams);
                next_local_trajectory += subbatch_count;
                for (std::size_t worker = 0; worker < worker_results.size(); ++worker) {
                    profile.record_traversal(
                        worker_batches[worker].trajectories.size(),
                        worker_results[worker].nodes_visited,
                        worker_results[worker].infosets_updated);
                }
                if (next_local_trajectory == config.minibatch_size) {
                    next_local_trajectory = 0U;
                    ++next_batch;
                    ++output.batches_completed;
                    if (deadline_expired(deadline)) output.timed_out = true;
                    break;
                }
                if (deadline_expired(deadline)) {
                    output.timed_out = true;
                    break;
                }
            }
            if (output.timed_out) break;
        }
        if (!output.timed_out) {
            HUNLSampledRootStrategy exported;
            if (export_root_strategy_until(deadline, exported)) {
                last_clean_root_strategy = std::move(exported);
            } else {
                output.timed_out = deadline.has_value();
            }
        }
        output.root_strategy = last_clean_root_strategy;
        return output;
    }
};

HUNLSampledRangeSession::HUNLSampledRangeSession(
    HUNLStructuredRootRequest root,
    HUNLSampledSolverConfig config,
    HUNLSampledStorage& storage,
    HUNLSampledProfile& profile,
    std::uint64_t first_batch,
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

std::uint64_t HUNLSampledRangeSession::next_batch() const noexcept {
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
