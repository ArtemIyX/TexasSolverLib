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
constexpr std::uint64_t kSaturatedMemoryEstimate = std::numeric_limits<std::uint64_t>::max();

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    return left > kSaturatedMemoryEstimate - right ? kSaturatedMemoryEstimate : left + right;
}

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0U || right == 0U) return 0U;
    return left > kSaturatedMemoryEstimate / right ? kSaturatedMemoryEstimate : left * right;
}

std::uint64_t saturating_size(std::size_t value) noexcept {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>(kSaturatedMemoryEstimate)) {
            return kSaturatedMemoryEstimate;
        }
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t range_hand_count(const HUNLStructuredRootRequest& root, std::size_t player) noexcept {
    if (player >= root.config.initial_ranges.size() ||
        !root.config.initial_ranges[player].has_value()) {
        return 0U;
    }
    return saturating_size(root.config.initial_ranges[player]->hand_weights.size());
}

class RangeMemoryGuard {
public:
    explicit RangeMemoryGuard(std::uint64_t limit_bytes) : limit_bytes_(limit_bytes) {}

    void admit_retained(std::uint64_t bytes) {
        if (limit_bytes_ != 0U &&
            (retained_bytes_ > limit_bytes_ || bytes > limit_bytes_ - retained_bytes_)) {
            throw std::runtime_error("structured sampled range session exceeded its memory budget");
        }
        retained_bytes_ = saturating_add(retained_bytes_, bytes);
    }

    void check_peak(std::uint64_t transient_bytes) const {
        if (limit_bytes_ != 0U &&
            (retained_bytes_ > limit_bytes_ || transient_bytes > limit_bytes_ - retained_bytes_)) {
            throw std::runtime_error("structured sampled range batch exceeds its memory budget");
        }
    }

    [[nodiscard]] std::uint64_t retained_bytes() const noexcept { return retained_bytes_; }
    [[nodiscard]] std::uint64_t limit_bytes() const noexcept { return limit_bytes_; }

private:
    std::uint64_t limit_bytes_ = 0U;
    std::uint64_t retained_bytes_ = 0U;
};

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
    explicit PrivateInfosetCoordinator(HUNLSampledStorage& storage, RangeMemoryGuard& memory_guard)
        : storage_(storage),
          memory_guard_(memory_guard) {
        memory_guard_.admit_retained(
            saturating_multiply(2048U, sizeof(void*)));
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
        const auto next_size = infosets_.size() + 1U;
        if (next_size > static_cast<std::size_t>(
                static_cast<double>(infosets_.bucket_count()) * infosets_.max_load_factor())) {
            const auto target_buckets = std::max<std::uint64_t>(
                saturating_multiply(saturating_size(next_size), 4U),
                saturating_multiply(saturating_size(infosets_.bucket_count()), 2U));
            memory_guard_.admit_retained(saturating_multiply(target_buckets, sizeof(void*)));
            infosets_.reserve(next_size * 2U);
        }
        memory_guard_.admit_retained(
            saturating_add(
                sizeof(HUNLInfosetEncoding) + sizeof(PrivateInfoset),
                sizeof(void*) * 3U));
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

public:
    [[nodiscard]] std::uint64_t memory_bytes() const noexcept {
        const auto buckets = saturating_multiply(
            saturating_size(infosets_.bucket_count()), sizeof(void*));
        const auto nodes = saturating_multiply(
            saturating_size(infosets_.size()),
            saturating_add(
                sizeof(HUNLInfosetEncoding) + sizeof(PrivateInfoset),
                sizeof(void*) * 3U));
        return saturating_add(buckets, nodes);
    }

private:
    HUNLSampledStorage& storage_;
    RangeMemoryGuard& memory_guard_;
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

HUNLLeafEvaluationRequest make_leaf_request(
    const HUNLState& state,
    const HUNLStructuredRootRequest& root,
    const RangeReach& reach,
    const std::optional<std::chrono::steady_clock::time_point>& deadline) {
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
    return request;
}

bool valid_leaf_result(
    const HUNLLeafEvaluationRequest& request,
    const HUNLLeafEvaluationResult& result) noexcept {
    const auto total = result.values[0] + result.values[1];
    const auto scale = std::max({1.0, std::abs(result.values[0]), std::abs(result.values[1])});
    return result.populated &&
        result.units == request.units &&
        result.scope == request.scope &&
        result.abstraction_version == request.abstraction_version &&
        result.model_version == request.model_version &&
        std::isfinite(result.values[0]) &&
        std::isfinite(result.values[1]) &&
        std::abs(total) <= scale * 1e-9;
}

bool same_leaf_request(
    const HUNLLeafEvaluationRequest& expected,
    const HUNLLeafEvaluationRequest& actual) noexcept {
    const auto& left = expected.public_state;
    const auto& right = actual.public_state;
    const auto same_public_state =
        !left.hole_cards.has_value() && !right.hole_cards.has_value() &&
        left.board == right.board && left.street == right.street &&
        left.contributions == right.contributions && left.stacks == right.stacks &&
        left.street_history == right.street_history &&
        left.street_aggressor == right.street_aggressor &&
        left.street_num_raises == right.street_num_raises && left.to_call == right.to_call &&
        left.cur_player == right.cur_player && left.folded == right.folded && left.all_in == right.all_in &&
        left.config == right.config && left.betting_tokens == right.betting_tokens &&
        left.current_street_tokens == right.current_street_tokens &&
        left.betting_history_codes == right.betting_history_codes &&
        left.current_street_history_codes == right.current_street_history_codes &&
        left.pending_board_deals == right.pending_board_deals;
    return same_public_state && expected.private_hole_cards == actual.private_hole_cards &&
        expected.bucket_reach == actual.bucket_reach &&
        expected.scope == actual.scope &&
        expected.units == actual.units &&
        expected.abstraction_version == actual.abstraction_version &&
        expected.model_version == actual.model_version;
}

struct LeafResultCursor {
    const std::vector<HUNLLeafEvaluationRequest>& requests;
    const std::vector<HUNLLeafEvaluationResult>& results;
    std::size_t next = 0U;
    std::size_t end = 0U;
};

double traverse(
    const HUNLState& state,
    const HUNLStructuredRootRequest& root,
    PlayerId traverser,
    std::uint64_t trajectory_id,
    const PrivateInfosetCoordinator& infosets,
    const HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch,
    PcsRng& rng,
    RangeReach reach,
    std::uint32_t plies,
    std::vector<HUNLLeafEvaluationRequest>* leaf_requests,
    LeafResultCursor* leaf_results,
    bool write_deltas,
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
        const auto request = make_leaf_request(state, root, reach, deadline);
        if (leaf_requests != nullptr) {
            leaf_requests->push_back(request);
            return 0.0;
        }
        if (leaf_results == nullptr || leaf_results->next >= leaf_results->end ||
            leaf_results->next >= leaf_results->results.size()) {
            throw std::logic_error("structured sampled leaf batch has an invalid result shape");
        }
        const auto index = leaf_results->next++;
        const auto& planned_request = leaf_results->requests[index];
        if (!same_leaf_request(planned_request, request) ||
            !valid_leaf_result(planned_request, leaf_results->results[index])) {
            throw std::runtime_error("structured sampled leaf evaluator returned invalid provenance or utilities");
        }
        return leaf_results->results[index].values[static_cast<std::size_t>(traverser)];
    }
    if (state.current_player() == -1) {
        const auto outcomes = state.chance_outcomes();
        const auto selected = sample_probability(outcomes, rng);
        ++result.chance_nodes_sampled;
        reach.chance *= selected.second;
        reach.sampling *= selected.second;
        return traverse(state.apply(outcomes[selected.first].action), root, traverser,
                        trajectory_id, infosets, storage, scratch, rng, reach, plies + 1U, leaf_requests, leaf_results, write_deltas, deadline,
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
        if (write_deltas) {
            for (std::size_t action = 0; action < actions.size(); ++action) {
                append_delta(scratch, trajectory_id, infoset, action, 0.0,
                             strategy_weight * static_cast<double>(strategy[action]));
            }
        }
        ++result.infosets_updated;
        reach.player[acting_index] *= selected.second;
        reach.sampling *= selected.second;
        return traverse(state.apply(actions[selected.first]), root, traverser, trajectory_id,
                        infosets, storage, scratch, rng, reach, plies + 1U, leaf_requests, leaf_results, write_deltas, deadline, cancelled, result);
    }

    std::array<double, kMaxDecisionActions> action_values = {};
    double node_value = 0.0;
    for (std::size_t action = 0; action < actions.size(); ++action) {
        auto child_reach = reach;
        child_reach.player[acting_index] *= static_cast<double>(strategy[action]);
        action_values[action] = traverse(state.apply(actions[action]), root, traverser,
                                         trajectory_id, infosets, storage, scratch, rng, child_reach,
                                         plies + 1U, leaf_requests, leaf_results, write_deltas, deadline, cancelled, result);
        if (cancelled) return 0.0;
        node_value += static_cast<double>(strategy[action]) * action_values[action];
    }
    const auto other = 1U - acting_index;
    const auto counterfactual_reach = reach.chance * reach.player[other];
    const auto importance_weight = counterfactual_reach / reach.sampling;
    if (!std::isfinite(importance_weight)) throw std::overflow_error("structured sampled regret weight is non-finite");
    if (write_deltas) {
        for (std::size_t action = 0; action < actions.size(); ++action) {
            append_delta(scratch, trajectory_id, infoset, action,
                         importance_weight * (action_values[action] - node_value),
                         strategy_weight * static_cast<double>(strategy[action]));
        }
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

HUNLSampledRangeMemoryEstimate estimate_range_memory(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config) noexcept {
    HUNLSampledRangeMemoryEstimate estimate;
    const auto deal_capacity = saturating_multiply(range_hand_count(root, 0U), range_hand_count(root, 1U));
    estimate.joint_deal_bytes = saturating_multiply(deal_capacity, sizeof(HUNLJointRangeDeal));

    const auto estimated_infosets = std::max<std::uint64_t>(
        1024U,
        saturating_multiply(static_cast<std::uint64_t>(config.minibatch_size), 4096U));
    const auto lookup_nodes = saturating_multiply(
        estimated_infosets,
        saturating_add(
            sizeof(HUNLInfosetEncoding) + sizeof(PrivateInfoset),
            sizeof(void*) * 3U));
    const auto lookup_buckets = saturating_multiply(
        saturating_multiply(estimated_infosets, 4U),
        sizeof(void*));
    estimate.infoset_lookup_bytes = saturating_add(lookup_nodes, lookup_buckets);

    const auto trajectories = std::min<std::uint64_t>(
        std::max<std::uint64_t>(1U, config.minibatch_size),
        kTrajectorySubbatchSize);
    const auto workers = std::min<std::uint64_t>(
        trajectories,
        std::max<std::uint64_t>(1U, saturating_size(config.workers)));
    const auto largest_partition = (trajectories + workers - 1U) / workers;
    const auto per_worker = saturating_add(
        saturating_multiply(
            saturating_multiply(largest_partition + 1U, kDeltaEntriesPerTrajectory),
            sizeof(HUNLSampledValueDelta)),
            sizeof(HUNLSampledWorkerScratch) + sizeof(HUNLSampledTraversalResult) + sizeof(std::thread));
    const auto leaf_slots = root.config.depth_limit_plies == 0U ? 0U :
        saturating_multiply(trajectories, kDeltaEntriesPerTrajectory);
    const auto leaf_metadata = saturating_add(
        saturating_add(saturating_size(root.blueprint_version.size() + 1U),
                       saturating_size(root.model_version.size() + 1U)),
        sizeof(double) * 2U);
    const auto leaf_batch_bytes = saturating_multiply(
        leaf_slots,
        saturating_add(
            sizeof(HUNLLeafEvaluationRequest) + sizeof(HUNLLeafEvaluationResult),
            leaf_metadata));
    estimate.batch_scratch_bytes = saturating_add(
        saturating_multiply(workers, per_worker), leaf_batch_bytes);
    estimate.export_bytes = saturating_add(
        saturating_multiply(16U, sizeof(HUNLSampledActionProbability)),
        saturating_add(16U * sizeof(double), 16U * sizeof(int)));
    estimate.retained_bytes = saturating_add(
        saturating_add(estimate.joint_deal_bytes, estimate.infoset_lookup_bytes),
        sizeof(HUNLState) + sizeof(HUNLSampledRootStrategy) + 4096U);
    return estimate;
}

}  // namespace

std::uint64_t HUNLSampledRangeMemoryEstimate::peak_bytes() const noexcept {
    return saturating_add(
        retained_bytes,
        saturating_add(batch_scratch_bytes, export_bytes));
}

HUNLSampledRangeMemoryEstimate estimate_hunl_sampled_range_memory(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config) noexcept {
    return estimate_range_memory(root, config);
}

struct HUNLSampledRangeSession::Impl {
    HUNLStructuredRootRequest root;
    HUNLSampledSolverConfig config;
    HUNLSampledStorage& storage;
    HUNLSampledProfile& profile;
    RangeMemoryGuard memory_guard;
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
        const HUNLLeafEvaluator* input_leaf_evaluator,
        std::uint64_t memory_limit_bytes)
        : root(std::move(input_root)),
          config(std::move(input_config)),
          storage(input_storage),
          profile(input_profile),
          memory_guard(memory_limit_bytes),
          leaf_evaluator(input_leaf_evaluator),
          game_config(std::make_shared<const HUNLConfig>(root.config)),
          public_root_state(root.public_root_state(game_config)),
          infosets(storage, memory_guard),
          next_batch(first_batch) {
        validate_sampled_config_or_throw(config);
        root.validate();
        if (root.config.depth_limit_plies != 0U &&
            (leaf_evaluator == nullptr || !leaf_evaluator->valid())) {
            throw std::invalid_argument(
                "structured sampled depth limit requires a typed leaf evaluator");
        }
        const auto planned_memory = estimate_range_memory(root, config);
        memory_guard.admit_retained(planned_memory.joint_deal_bytes);
        memory_guard.admit_retained(sizeof(HUNLState) + sizeof(HUNLSampledRootStrategy) + 4096U);
        deals = root.normalized_joint_range();
        root_state = root_for_deal(public_root_state, first_deal(deals));
        root_actions = root_state.legal_actions();
        if (deals.empty() || root_actions.empty()) {
            throw std::logic_error("structured sampled root has no compatible deal or legal root action");
        }
        last_clean_root_strategy = export_root_strategy();
    }

    bool export_root_strategy_until(
        const std::optional<std::chrono::steady_clock::time_point>& deadline,
        HUNLSampledRootStrategy& output) {
        if (deadline_expired(deadline)) return false;
        memory_guard.check_peak(estimate_range_memory(root, config).export_bytes);
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
                memory_guard.check_peak(estimate_range_memory(root, config).batch_scratch_bytes);
                struct LeafRequestRange {
                    std::size_t begin = 0U;
                    std::size_t end = 0U;
                };
                std::array<LeafRequestRange, kTrajectorySubbatchSize> leaf_request_ranges = {};
                std::vector<HUNLLeafEvaluationRequest> leaf_requests;
                std::vector<HUNLLeafEvaluationResult> leaf_results;
                if (root.config.depth_limit_plies != 0U) {
                    leaf_requests.reserve(
                        static_cast<std::size_t>(subbatch_count) * kDeltaEntriesPerTrajectory);
                    for (std::uint32_t offset_in_batch = 0; offset_in_batch < subbatch_count; ++offset_in_batch) {
                        const auto local = subbatch_begin + offset_in_batch;
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
                        auto& leaf_range = leaf_request_ranges[offset_in_batch];
                        leaf_range.begin = leaf_requests.size();
                        HUNLSampledWorkerScratch planning_scratch;
                        HUNLSampledTraversalResult planning_result;
                        bool planning_cancelled = false;
                        const auto chance = deals.size() == 1U ? 1.0 : deals[deal_index].weight;
                        (void)traverse(
                            root_for_deal(public_root_state, deals[deal_index]),
                            root,
                            traverser,
                            trajectory_id,
                            infosets,
                            storage,
                            planning_scratch,
                            rng,
                            RangeReach{{1.0, 1.0}, chance, chance},
                            0U,
                            &leaf_requests,
                            nullptr,
                            false,
                            deadline,
                            planning_cancelled,
                            planning_result);
                        if (planning_cancelled) {
                            output.timed_out = true;
                            break;
                        }
                        leaf_range.end = leaf_requests.size();
                        if (leaf_range.end - leaf_range.begin > kDeltaEntriesPerTrajectory) {
                            throw std::length_error("structured sampled leaf batch exceeds its per-trajectory cap");
                        }
                    }
                    if (output.timed_out || reserve_expired(deadline)) {
                        output.timed_out = true;
                        break;
                    }
                    if (!leaf_requests.empty()) {
                        leaf_results.resize(leaf_requests.size());
                        bool accepted = false;
                        {
                            std::lock_guard<std::mutex> lock(leaf_evaluator_mutex);
                            if (!deadline_expired(deadline)) {
                                accepted = leaf_evaluator->evaluate_batch(
                                    leaf_evaluator->context,
                                    leaf_requests.data(),
                                    leaf_results.data(),
                                    leaf_requests.size());
                            }
                        }
                        if (deadline_expired(deadline)) {
                            output.timed_out = true;
                            break;
                        }
                        if (!accepted) {
                            throw std::runtime_error("structured sampled leaf evaluator rejected a depth-cutoff batch");
                        }
                        for (std::size_t index = 0; index < leaf_requests.size(); ++index) {
                            if (!valid_leaf_result(leaf_requests[index], leaf_results[index])) {
                                throw std::runtime_error(
                                    "structured sampled leaf evaluator returned invalid provenance or utilities");
                            }
                        }
                    }
                }
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
                            const auto& leaf_range = leaf_request_ranges[relative];
                            LeafResultCursor leaf_cursor{leaf_requests, leaf_results, leaf_range.begin, leaf_range.end};
                            (void)traverse(
                                root_for_deal(public_root_state, deals[deal_index]),
                                root,
                                traverser,
                                trajectory_id,
                                infosets,
                                storage,
                                trajectory_stream,
                                rng,
                                RangeReach{{1.0, 1.0}, chance, chance},
                                0U,
                                nullptr,
                                root.config.depth_limit_plies == 0U ? nullptr : &leaf_cursor,
                                true,
                                deadline,
                                trajectory_cancelled,
                                result);
                            if (trajectory_cancelled) {
                                cancelled.store(true, std::memory_order_release);
                                return;
                            }
                            if (root.config.depth_limit_plies != 0U && leaf_cursor.next != leaf_cursor.end) {
                                throw std::logic_error("structured sampled leaf batch result count does not match traversal");
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
    const HUNLLeafEvaluator* leaf_evaluator,
    std::uint64_t memory_limit_bytes)
    : impl_(std::make_unique<Impl>(
          std::move(root), std::move(config), storage, profile, first_batch, leaf_evaluator, memory_limit_bytes)) {}

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

HUNLSampledRangeMemoryEstimate HUNLSampledRangeSession::memory_estimate() const noexcept {
    if (impl_ == nullptr) return {};
    HUNLSampledRangeMemoryEstimate estimate;
    estimate.joint_deal_bytes = saturating_multiply(
        saturating_size(impl_->deals.capacity()), sizeof(HUNLJointRangeDeal));
    estimate.infoset_lookup_bytes = impl_->infosets.memory_bytes();
    estimate.retained_bytes = std::max(
        impl_->memory_guard.retained_bytes(),
        saturating_add(
            saturating_add(estimate.joint_deal_bytes, estimate.infoset_lookup_bytes),
            sizeof(HUNLState) + sizeof(HUNLSampledRootStrategy)));
    const auto planned = estimate_range_memory(impl_->root, impl_->config);
    estimate.batch_scratch_bytes = planned.batch_scratch_bytes;
    estimate.export_bytes = planned.export_bytes;
    return estimate;
}

HUNLSampledRangeRunResult run_hunl_sampled_structured_range_batches(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config,
    std::uint64_t first_batch,
    std::uint32_t batch_count,
    std::optional<std::chrono::steady_clock::time_point> deadline,
    HUNLSampledStorage& storage,
    HUNLSampledProfile& profile,
    const HUNLLeafEvaluator* leaf_evaluator,
    std::uint64_t memory_limit_bytes) {
    HUNLSampledRangeSession session(
        root, config, storage, profile, first_batch, leaf_evaluator, memory_limit_bytes);
    return session.resume_batches(batch_count, deadline);
}

}  // namespace core
