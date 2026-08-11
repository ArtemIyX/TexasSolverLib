#include "solver/multiway_blueprint_trainer.hpp"
#include "solver/multiway_checkpoint.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

bool due(std::uint64_t batch, std::uint64_t interval) noexcept {
    return interval != 0U && batch % interval == 0U;
}

MultiwayBlueprintTrainingMetadata make_metadata(
    const MultiwayBlueprintTrainingStatus& status,
    const MultiwayBlueprintIterationSchedule& schedule,
    std::uint64_t seed) noexcept {
    MultiwayBlueprintTrainingMetadata metadata;
    metadata.batches = status.batches;
    metadata.trajectories = status.trajectories;
    metadata.deterministic_seed = seed;
    metadata.late_window_start_batch = status.late_window_start_batch;
    metadata.schedule_hash = schedule.identity();
    metadata.pruned_negative_regrets = status.pruned_negative_regrets;
    metadata.linear_iteration_weighting = schedule.linear_iteration_weighting ? 1U : 0U;
    metadata.discounting_enabled = schedule.discount_regrets ? 1U : 0U;
    metadata.negative_regret_pruning_enabled = schedule.prune_negative_regrets ? 1U : 0U;
    return metadata;
}

}  // namespace

void MultiwayBlueprintIterationSchedule::validate() const {
    if (!std::isfinite(regret_discount_factor) || regret_discount_factor <= 0.0 ||
        regret_discount_factor > 1.0 || discount_interval_batches == 0U ||
        pruning_interval_batches == 0U) {
        throw std::invalid_argument("multiway blueprint schedule has invalid discount or pruning controls");
    }
}

double MultiwayBlueprintIterationSchedule::strategy_weight(std::uint64_t one_based_batch) const {
    if (one_based_batch == 0U) {
        throw std::invalid_argument("multiway blueprint strategy weight requires a one-based batch index");
    }
    return linear_iteration_weighting ? static_cast<double>(one_based_batch) : 1.0;
}

std::uint64_t MultiwayBlueprintIterationSchedule::identity() const noexcept {
    auto hash = kFnvOffset;
    append_u64(hash, linear_iteration_weighting ? 1U : 0U);
    append_u64(hash, discount_regrets ? 1U : 0U);
    append_u64(hash, static_cast<std::uint64_t>(regret_discount_factor * 1000000000.0));
    append_u64(hash, discount_interval_batches);
    append_u64(hash, prune_negative_regrets ? 1U : 0U);
    append_u64(hash, pruning_warmup_batches);
    append_u64(hash, pruning_interval_batches);
    append_u64(hash, late_window_start_batch);
    return hash == 0U ? 1U : hash;
}

void MultiwayBlueprintTrainingConfig::validate() const {
    rules.validate();
    blueprint.validate();
    bucket_profile.validate();
    action_abstraction.validate();
    cfr.validate();
    limits.validate();
    schedule.validate();
    if (deterministic_seed == 0U || cfr.player_count != rules.player_count ||
        blueprint.player_count != rules.player_count ||
        blueprint.initial_stack_chips != rules.initial_stack_chips ||
        blueprint.small_blind_chips != rules.small_blind_chips ||
        blueprint.big_blind_chips != rules.big_blind_chips ||
        blueprint.ante_chips != rules.ante_chips || blueprint.rake_policy.identity() != rules.rake_policy.identity() ||
        blueprint.rules_profile_version != rules.profile_version ||
        blueprint.flop_bucket_count != bucket_profile.flop_bucket_count ||
        blueprint.turn_bucket_count != bucket_profile.turn_bucket_count ||
        blueprint.river_bucket_count != bucket_profile.river_bucket_count ||
        max_decision_depth == 0U || max_decision_depth > MULTIWAY_MAX_DECISION_DEPTH ||
        max_public_chance_depth > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH) {
        throw std::invalid_argument("multiway training configuration has inconsistent artifact or traversal inputs");
    }
}

MultiwayModelIdentity MultiwayBlueprintTrainingConfig::identity() const {
    validate();
    return make_multiway_model_identity(blueprint);
}

MultiwayBlueprintTrainer::MultiwayBlueprintTrainer(
    MultiwayModelIdentity identity,
    MultiwayRootBatchRunner& batch_runner,
    MultiwaySolverCoordinator& coordinator,
    MultiwayBlueprintIterationSchedule schedule,
    std::uint64_t deterministic_seed)
    : identity_(identity),
      batch_runner_(&batch_runner),
      coordinator_(&coordinator),
      schedule_(schedule),
      deterministic_seed_(deterministic_seed) {
    identity_.validate();
    schedule_.validate();
    if (deterministic_seed_ == 0U) {
        throw std::invalid_argument("multiway blueprint trainer requires a non-zero deterministic seed");
    }
    status_.late_window_start_batch = schedule_.late_window_start_batch;
}

void MultiwayBlueprintTrainer::run_batches(
    std::uint64_t batch_count,
    std::uint64_t trajectories_per_batch,
    std::uint64_t seed) {
    if (trajectories_per_batch == 0U || seed != deterministic_seed_ ||
        batch_count > (std::numeric_limits<std::uint64_t>::max() - status_.trajectories) / trajectories_per_batch ||
        batch_count > std::numeric_limits<std::uint64_t>::max() - status_.batches ||
        status_.batches > std::numeric_limits<std::uint64_t>::max() - seed ||
        batch_count > std::numeric_limits<std::uint64_t>::max() - seed - status_.batches) {
        throw std::invalid_argument("multiway blueprint trainer has an invalid batch request");
    }
    for (std::uint64_t batch = 0; batch < batch_count; ++batch) {
        const auto one_based_batch = status_.batches + 1U;
        if (status_.late_window_start_batch != 0U &&
            one_based_batch == status_.late_window_start_batch && late_window_baseline_.empty()) {
            late_window_baseline_ = coordinator_->export_root_strategy_sums();
            status_.late_window_active = true;
        }
        (void)batch_runner_->run(
            status_.trajectories,
            trajectories_per_batch,
            seed + status_.batches,
            schedule_.strategy_weight(one_based_batch));
        status_.trajectories += trajectories_per_batch;
        ++status_.batches;
        if (schedule_.discount_regrets && due(status_.batches, schedule_.discount_interval_batches)) {
            coordinator_->scale_regrets(schedule_.regret_discount_factor);
        }
        if (schedule_.prune_negative_regrets && status_.batches > schedule_.pruning_warmup_batches &&
            due(status_.batches - schedule_.pruning_warmup_batches, schedule_.pruning_interval_batches)) {
            status_.pruned_negative_regrets += coordinator_->prune_negative_regrets();
        }
        const auto& diagnostics = coordinator_->diagnostics();
        status_.visited_public_descriptors = diagnostics.public_states_admitted;
        status_.admitted_rows = diagnostics.sparse_rows_admitted;
        status_.admitted_action_cells = coordinator_->storage().value_count();
    }
}

MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainer::coverage_manifest() const noexcept {
    return {
        status_.visited_public_descriptors,
        status_.admitted_rows,
        status_.admitted_action_cells,
        status_.terminal_visits,
        status_.leaf_visits,
        status_.missing_lookup_requests,
    };
}

MultiwayFullBlueprintArtifact MultiwayBlueprintTrainer::export_full_policy() const {
    MultiwayFullBlueprintArtifact artifact;
    artifact.identity = identity_;
    artifact.training = make_metadata(status_, schedule_, deterministic_seed_);
    const auto& storage = coordinator_->storage();
    for (const auto& metadata : storage.rows()) {
        const auto* state = coordinator_->find_public_state(metadata.shape.infoset.public_state);
        if (state == nullptr) throw std::logic_error("multiway sparse row has no admitted public state");
        for (std::uint32_t bucket = 0U; bucket < metadata.shape.bucket_count; ++bucket) {
            const auto strategy = storage.average_strategy(metadata.shape.infoset, bucket);
            MultiwayBlueprintRow row;
            row.infoset = metadata.shape.infoset;
            row.bucket = bucket;
            row.action_menu_id = state->legal_actions.front().action_menu_id;
            row.actions.reserve(strategy.size());
            std::uint32_t assigned = 0U;
            for (std::size_t action = 0U; action < strategy.size(); ++action) {
                const auto probability = action + 1U == strategy.size()
                    ? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
                    : static_cast<std::uint16_t>(std::floor(strategy[action] *
                        std::numeric_limits<std::uint16_t>::max()));
                assigned += probability;
                row.actions.push_back({state->legal_actions[action], probability});
            }
            artifact.rows.push_back(std::move(row));
        }
    }
    artifact.payload_hash = MultiwayFullBlueprintArtifacts::payload_hash(artifact);
    artifact.validate();
    return artifact;
}

MultiwayBlueprintSnapshot MultiwayBlueprintTrainer::publish(MultiwayBlueprintPolicyKind policy_kind) const {
    auto metadata = make_metadata(status_, schedule_, deterministic_seed_);
    if (policy_kind == MultiwayBlueprintPolicyKind::LateWindowAverage) {
        if (late_window_baseline_.empty()) {
            throw std::logic_error("multiway late-window policy was requested before its window started");
        }
        const auto policy = coordinator_->export_root_policy_since(late_window_baseline_);
        MultiwayBlueprintSnapshot snapshot;
        snapshot.identity = identity_;
        snapshot.public_state = policy.public_state;
        snapshot.infoset = policy.infoset;
        snapshot.bucket = policy.bucket;
        snapshot.trajectories = status_.trajectories;
        snapshot.policy_kind = policy_kind;
        snapshot.training = metadata;
        std::uint32_t assigned = 0;
        snapshot.actions.reserve(policy.actions.size());
        for (std::size_t index = 0; index < policy.actions.size(); ++index) {
            const auto quantized = index + 1U == policy.actions.size()
                ? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
                : static_cast<std::uint16_t>(std::floor(policy.actions[index].probability *
                    std::numeric_limits<std::uint16_t>::max()));
            assigned += quantized;
            snapshot.actions.push_back({policy.actions[index].action, quantized});
        }
        snapshot.validate();
        return snapshot;
    }
    return export_multiway_root_snapshot(identity_, *coordinator_, status_.trajectories, policy_kind, metadata);
}

void MultiwayBlueprintTrainer::resume_from(const MultiwayBlueprintSnapshot& checkpoint) {
    MultiwayCheckpoint::validate_resume_identity(checkpoint, identity_);
    if (checkpoint.training.schedule_hash != schedule_.identity() ||
        checkpoint.training.deterministic_seed != deterministic_seed_) {
        throw std::invalid_argument("multiway checkpoint schedule or seed does not match the live trainer");
    }
    if (checkpoint.training.trajectories != checkpoint.trajectories ||
        checkpoint.training.batches != status_.batches || checkpoint.trajectories != status_.trajectories) {
        throw std::invalid_argument("multiway compact checkpoint requires matching live training state");
    }
}

MultiwayBlueprintTrainingSession::MultiwayBlueprintTrainingSession(
    MultiwayBlueprintTrainingConfig config,
    MultiwayRootSnapshot root,
    const MultiwayBucketRegistry& buckets,
    const MultiwayLeafEvaluator* leaf_evaluator)
    : config_(std::move(config)), root_(std::move(root)), buckets_(&buckets) {
    config_.validate();
    root_.validate();
    const auto identity = config_.identity();
    if (buckets.identity() != identity || root_.seat_order.size() != config_.rules.player_count ||
        root_.rake_policy.identity() != config_.rules.rake_policy.identity() ||
        root_.action_abstraction_version != config_.blueprint.action_abstraction_version ||
        root_.leaf_model_version != config_.blueprint.terminal_model_version ||
        (leaf_evaluator != nullptr && !leaf_evaluator->valid())) {
        throw std::invalid_argument("multiway training session has incompatible root, bucket, or leaf inputs");
    }
    if (leaf_evaluator != nullptr) leaf_evaluator_ = *leaf_evaluator;
    MultiwaySolveRequest request(root_, config_.cfr, config_.limits);
    coordinator_ = std::make_unique<MultiwaySolverCoordinator>(request);
    action_abstraction_ = std::make_unique<MultiwayActionAbstraction>(config_.action_abstraction);
    MultiwayRootExternalSamplingTraversal traversal(
        *coordinator_, root_, *action_abstraction_, *buckets_, leaf_evaluator == nullptr ? nullptr : &leaf_evaluator_,
        config_.max_decision_depth, config_.max_public_chance_depth);
    batch_runner_ = std::make_unique<MultiwayRootBatchRunner>(
        std::move(traversal), *coordinator_, config_.limits.worker_count,
        config_.limits.max_worker_delta_entries);
    trainer_ = std::make_unique<MultiwayBlueprintTrainer>(
        identity, *batch_runner_, *coordinator_, config_.schedule, config_.deterministic_seed);
}

void MultiwayBlueprintTrainingSession::run_batches(std::uint64_t batch_count) {
    trainer_->run_batches(batch_count, config_.limits.trajectories_per_batch, config_.deterministic_seed);
}

void MultiwayBlueprintTrainingSession::resume_from_checkpoint(const MultiwayBlueprintSnapshot& checkpoint) {
    trainer_->resume_from(checkpoint);
}

MultiwayBlueprintSnapshot MultiwayBlueprintTrainingSession::export_policy(
    MultiwayBlueprintPolicyKind policy_kind) const {
    return trainer_->publish(policy_kind);
}

const MultiwayBlueprintTrainingStatus& MultiwayBlueprintTrainingSession::status() const noexcept {
    return trainer_->status();
}

MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainingSession::coverage_manifest() const noexcept {
    return trainer_->coverage_manifest();
}

MultiwayFullBlueprintArtifact MultiwayBlueprintTrainingSession::export_full_policy() const {
    return trainer_->export_full_policy();
}

}  // namespace core
