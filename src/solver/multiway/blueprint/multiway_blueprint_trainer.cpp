#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "games/multiway_state.hpp"
#include "core/fingerprint.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {
namespace {

using texas::core::fingerprint::append_u64;

template <class Range>
void append_range(std::uint64_t& hash, const Range& values) noexcept {
    for (const auto value : values) append_u64(hash, value);
}

std::uint64_t hash_action_abstraction(const MultiwayActionAbstractionConfig& config) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, config.menu_profile_version);
    append_u64(hash, config.translation_policy_version);
    append_u64(hash, config.translation_max_pseudo_harmonic_distance_basis_points);
    append_range(hash, config.first_bet_basis_points);
    append_range(hash, config.raise_basis_points);
    append_u64(hash, config.multiway_first_bet_count);
    append_u64(hash, config.three_way_first_bet_count);
    append_u64(hash, config.heads_up_first_bet_count);
    append_range(hash, config.unopened_raise_to_big_blind_basis_points);
    append_u64(hash, config.single_open_in_position_basis_points);
    append_u64(hash, config.single_open_out_of_position_basis_points);
    append_u64(hash, config.open_caller_increment_big_blind_basis_points);
    append_u64(hash, config.three_bet_or_more_basis_points);
    append_range(hash, config.contextual_multiway_first_bet_basis_points);
    append_range(hash, config.contextual_three_way_first_bet_basis_points);
    append_range(hash, config.contextual_heads_up_first_bet_basis_points);
    append_range(hash, config.contextual_raise_basis_points);
    return hash == 0U ? 1U : hash;
}

std::uint64_t hash_bucket_profile(
    std::uint64_t bucket_model_version,
    const MultiwayBucketBaselineProfile& profile) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, bucket_model_version);
    append_u64(hash, profile.schema_version);
    append_u64(hash, profile.feature_version);
    append_u64(hash, profile.flop_bucket_count);
    append_u64(hash, profile.turn_bucket_count);
    append_u64(hash, profile.river_bucket_count);
    return hash == 0U ? 1U : hash;
}

std::uint64_t combine_identity(const MultiwayModelIdentity& identity) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    visit_multiway_model_identity_components(identity, [&](std::uint64_t field) {
        append_u64(hash, field);
    });
    return hash == 0U ? 1U : hash;
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
    metadata.terminal_visits = status.terminal_visits;
    metadata.leaf_visits = status.leaf_visits;
    metadata.missing_lookup_requests = status.missing_lookup_requests;
    metadata.linear_iteration_weighting = schedule.linear_iteration_weighting ? 1U : 0U;
    metadata.discounting_enabled = schedule.discount_regrets ? 1U : 0U;
    metadata.negative_regret_pruning_enabled = schedule.prune_negative_regrets ? 1U : 0U;
    return metadata;
}

}  // namespace

MultiwayRootSnapshot make_multiway_initial_blueprint_root(
    const MultiwayGameRules& rules,
    MultiwayPrivateConfig private_ranges,
    const MultiwayActionAbstraction& action_abstraction,
    std::uint64_t action_abstraction_version,
    std::uint64_t leaf_model_version,
    PlayerId first_player) {
    rules.validate();
    auto state = games::multiway::MultiwayState::initial(rules, first_player);
    auto betting = state.snapshot();
    MultiwayRootSnapshot root;
    root.public_state = MultiwayPublicBuilder::make_root(
        betting, {}, action_abstraction.make_legal_actions(betting));
    root.root_infoset = {root.public_state.id, betting.current_player};
    root.root_bucket = 0U;
    root.seat_order.resize(rules.player_count);
    for (std::uint8_t seat = 0; seat < rules.player_count; ++seat) root.seat_order[seat] = seat;
    root.next_street_first_seat = first_player;
    root.odd_chip_first_seat = first_player;
    root.private_ranges = std::move(private_ranges);
    root.action_abstraction_version = action_abstraction_version;
    root.leaf_model_version = leaf_model_version;
    root.validate();
    return root;
}

void MultiwayBlueprintIterationSchedule::validate() const {
    if (!std::isfinite(regret_discount_factor) || regret_discount_factor <= 0.0 ||
        regret_discount_factor > 1.0 || discount_interval_batches == 0U ||
        pruning_interval_batches == 0U || !std::isfinite(pruning_threshold) ||
        pruning_threshold > 0.0 || !std::isfinite(regret_floor) || regret_floor > 0.0 ||
        !std::isfinite(pruning_exploration_probability) ||
        pruning_exploration_probability < 0.0 || pruning_exploration_probability > 1.0 ||
        !std::isfinite(pruning_action_probability_threshold) ||
        pruning_action_probability_threshold < 0.0 || pruning_action_probability_threshold > 1.0) {
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
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(hash, linear_iteration_weighting ? 1U : 0U);
    append_u64(hash, discount_regrets ? 1U : 0U);
    append_u64(hash, static_cast<std::uint64_t>(regret_discount_factor * 1000000000.0));
    append_u64(hash, discount_interval_batches);
    append_u64(hash, prune_negative_regrets ? 1U : 0U);
    append_u64(hash, pruning_warmup_batches);
    append_u64(hash, pruning_interval_batches);
    append_u64(hash, static_cast<std::uint64_t>(pruning_threshold * -1000000000.0));
    append_u64(hash, static_cast<std::uint64_t>(regret_floor * -1000000000.0));
    append_u64(hash, recovery_interval_batches);
    append_u64(hash, static_cast<std::uint64_t>(pruning_exploration_probability * 1000000000.0));
    append_u64(hash, static_cast<std::uint64_t>(pruning_action_probability_threshold * 1000000000.0));
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
    auto identity = make_multiway_model_identity(blueprint);
    identity.action_abstraction_hash = hash_action_abstraction(action_abstraction);
    identity.bucket_model_hash = hash_bucket_profile(blueprint.bucket_model_version, bucket_profile);
    auto runtime_hash = texas::core::fingerprint::FNV1A_OFFSET;
    append_u64(runtime_hash, blueprint.runtime_search_schema_version);
    append_u64(runtime_hash, max_decision_depth);
    append_u64(runtime_hash, max_public_chance_depth);
    append_u64(runtime_hash, cfr.player_count);
    append_u64(runtime_hash, cfr.deterministic_trajectory_merges ? 1U : 0U);
    append_u64(runtime_hash, limits.worker_count);
    append_u64(runtime_hash, limits.trajectories_per_batch);
    append_u64(runtime_hash, limits.max_public_states);
    append_u64(runtime_hash, limits.max_sparse_rows);
    append_u64(runtime_hash, limits.max_sparse_values);
    append_u64(runtime_hash, limits.max_worker_delta_entries);
    append_u64(runtime_hash, limits.max_batches);
    append_u64(runtime_hash, static_cast<std::uint8_t>(limits.storage_backend));
    append_u64(runtime_hash, schedule.identity());
    append_u64(runtime_hash, deterministic_seed);
    identity.runtime_search_schema_hash = runtime_hash == 0U ? 1U : runtime_hash;
    identity.combined_hash = combine_identity(identity);
    identity.validate();
    return identity;
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
        batch_runner_->set_batch_number(one_based_batch);
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
            const auto pruning_batch = status_.batches - schedule_.pruning_warmup_batches;
            const bool recovery = schedule_.recovery_interval_batches != 0U &&
                pruning_batch % schedule_.recovery_interval_batches == 0U;
            if (!recovery) {
                status_.pruned_negative_regrets += coordinator_->prune_negative_regrets(
                    schedule_.pruning_threshold, schedule_.regret_floor);
            }
        }
        const auto& diagnostics = coordinator_->diagnostics();
        status_.visited_public_descriptors = diagnostics.public_states_admitted;
        status_.admitted_rows = diagnostics.sparse_rows_admitted;
        status_.admitted_action_cells = coordinator_->compact_storage() != nullptr
            ? coordinator_->compact_storage()->value_count() : coordinator_->storage().value_count();
        status_.terminal_visits = diagnostics.terminal_visits;
        status_.leaf_visits = diagnostics.leaf_visits;
        status_.missing_lookup_requests = diagnostics.missing_lookup_requests;
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
    if (coordinator_->compact_storage() != nullptr) {
        const auto checkpoint = coordinator_->checkpoint();
        std::size_t offset = 0U;
        for (const auto& shape : checkpoint.storage.shapes) {
            const auto* state = coordinator_->find_public_state(shape.infoset.public_state);
            if (state == nullptr) throw std::logic_error("compact row has no admitted public state");
            for (std::uint32_t bucket = 0; bucket < shape.bucket_count; ++bucket) {
                MultiwayBlueprintRow row;
                row.infoset = shape.infoset;
                row.bucket = bucket;
                row.action_menu_id = state->legal_actions.front().action_menu_id;
                double total = 0.0;
                for (std::uint8_t action = 0; action < shape.action_count; ++action) total += checkpoint.storage.strategy_sums[offset + static_cast<std::size_t>(action) * shape.bucket_count + bucket];
                std::uint32_t assigned = 0U;
                for (std::uint8_t action = 0; action < shape.action_count; ++action) {
                    const auto mass = checkpoint.storage.strategy_sums[offset + static_cast<std::size_t>(action) * shape.bucket_count + bucket];
                    const auto probability = action + 1U == shape.action_count
                        ? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
                        : static_cast<std::uint16_t>(total > 0.0 ? std::floor(mass / total * std::numeric_limits<std::uint16_t>::max()) : 0U);
                    assigned += probability;
                    row.actions.push_back({state->legal_actions[action], probability});
                }
                artifact.rows.push_back(std::move(row));
            }
            offset += static_cast<std::size_t>(shape.action_count) * shape.bucket_count;
        }
        artifact.payload_hash = MultiwayFullBlueprintArtifacts::payload_hash(artifact);
        artifact.validate();
        return artifact;
    }
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

MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainer::checkpoint() const {
    MultiwayBlueprintTrainingCheckpoint result;
    result.identity = identity_;
    result.training = make_metadata(status_, schedule_, deterministic_seed_);
    result.coverage = coverage_manifest();
    result.coordinator = coordinator_->checkpoint();
    result.late_window_baseline = late_window_baseline_;
    return result;
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

void MultiwayBlueprintTrainer::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot) {
    MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, identity_);
    if (snapshot.training.schedule_hash != schedule_.identity() ||
        snapshot.training.deterministic_seed != deterministic_seed_) {
        throw std::invalid_argument("multiway checkpoint schedule or seed does not match the live trainer");
    }
    if (snapshot.training.trajectories != snapshot.trajectories ||
        snapshot.training.batches != status_.batches || snapshot.trajectories != status_.trajectories) {
        throw std::invalid_argument("multiway compact checkpoint requires matching live training state");
    }
}

void MultiwayBlueprintTrainer::resume_from_checkpoint(
    const MultiwayBlueprintTrainingCheckpoint& checkpoint) {
    checkpoint.identity.validate();
    if (checkpoint.identity != identity_ || checkpoint.training.schedule_hash != schedule_.identity() ||
        checkpoint.training.deterministic_seed != deterministic_seed_ ||
        checkpoint.training.trajectories < checkpoint.training.batches ||
        checkpoint.training.linear_iteration_weighting != (schedule_.linear_iteration_weighting ? 1U : 0U) ||
        checkpoint.training.discounting_enabled != (schedule_.discount_regrets ? 1U : 0U) ||
        checkpoint.training.negative_regret_pruning_enabled != (schedule_.prune_negative_regrets ? 1U : 0U)) {
        throw std::invalid_argument("multiway full checkpoint identity or schedule does not match the trainer");
    }
    coordinator_->restore_checkpoint(checkpoint.coordinator);
    status_.batches = checkpoint.training.batches;
    status_.trajectories = checkpoint.training.trajectories;
    status_.pruned_negative_regrets = checkpoint.training.pruned_negative_regrets;
    status_.late_window_start_batch = checkpoint.training.late_window_start_batch;
    status_.late_window_active = !checkpoint.late_window_baseline.empty();
    status_.visited_public_descriptors = checkpoint.coverage.visited_public_descriptors;
    status_.admitted_rows = checkpoint.coverage.admitted_rows;
    status_.admitted_action_cells = checkpoint.coverage.admitted_action_cells;
    status_.terminal_visits = checkpoint.coverage.terminal_visits;
    status_.leaf_visits = checkpoint.coverage.leaf_visits;
    status_.missing_lookup_requests = checkpoint.coverage.missing_lookup_requests;
    late_window_baseline_ = checkpoint.late_window_baseline;
    const auto& diagnostics = coordinator_->diagnostics();
    status_.visited_public_descriptors = diagnostics.public_states_admitted;
    status_.admitted_rows = diagnostics.sparse_rows_admitted;
    status_.admitted_action_cells = coordinator_->storage().value_count();
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
    continuation_selector_ = std::make_unique<MultiwayFixedContinuationSelector>(
        config_.blueprint.continuation_policy);
    MultiwayRootExternalSamplingTraversal traversal(
        *coordinator_, root_, *action_abstraction_, *buckets_, leaf_evaluator == nullptr ? nullptr : &leaf_evaluator_,
        config_.max_decision_depth, config_.max_public_chance_depth, nullptr, continuation_selector_.get(), nullptr,
        {config_.schedule.prune_negative_regrets, config_.schedule.pruning_warmup_batches,
         config_.schedule.recovery_interval_batches, config_.schedule.pruning_exploration_probability,
         config_.schedule.pruning_action_probability_threshold,
         config_.schedule.pruning_threshold});
    batch_runner_ = std::make_unique<MultiwayRootBatchRunner>(
        std::move(traversal), *coordinator_, config_.limits.worker_count,
        config_.limits.max_worker_delta_entries);
    trainer_ = std::make_unique<MultiwayBlueprintTrainer>(
        identity, *batch_runner_, *coordinator_, config_.schedule, config_.deterministic_seed);
}

void MultiwayBlueprintTrainingSession::run_batches(std::uint64_t batch_count) {
    trainer_->run_batches(batch_count, config_.limits.trajectories_per_batch, config_.deterministic_seed);
}

void MultiwayBlueprintTrainingSession::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot) {
    trainer_->resume_from_root_policy(snapshot);
}

void MultiwayBlueprintTrainingSession::resume_from_checkpoint(
    const MultiwayBlueprintTrainingCheckpoint& checkpoint) {
    trainer_->resume_from_checkpoint(checkpoint);
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

MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainingSession::checkpoint() const {
    return trainer_->checkpoint();
}

}  // namespace texas::solver::multiway
