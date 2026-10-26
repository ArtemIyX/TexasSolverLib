#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "games/multiway_state.hpp"
#include "core/fingerprint.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	namespace
	{
		constexpr std::uint64_t kQuantizedProbabilityMax = std::numeric_limits<std::uint16_t>::max();

		class MultiwayDurationRecorder final
		{
		public:
			explicit MultiwayDurationRecorder(std::uint64_t& target) noexcept
				: target_(target), started_at_(std::chrono::steady_clock::now())
			{
			}

			~MultiwayDurationRecorder() noexcept
			{
				const auto elapsed = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - started_at_)
						.count());
				if (elapsed > std::numeric_limits<std::uint64_t>::max() - target_)
					target_ = std::numeric_limits<std::uint64_t>::max();
				else
					target_ += elapsed;
			}

		private:
			std::uint64_t& target_;
			std::chrono::steady_clock::time_point started_at_;
		};

		bool export_shape_less(
			const MultiwaySparseRowShape& left,
			const MultiwaySparseRowShape& right) noexcept
		{
			return left.infoset.public_state.value != right.infoset.public_state.value
				? left.infoset.public_state.value < right.infoset.public_state.value
				: left.infoset.seat < right.infoset.seat;
		}

		void validate_export_menu(
			const MultiwaySparseRowShape& shape,
			const MultiwayPackedMenuView& menu)
		{
			if (shape.infoset.public_state.value == 0U || shape.infoset.seat < 0
				|| shape.bucket_count == 0U || shape.action_count == 0U
				|| shape.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS
				|| menu.actions == nullptr || menu.action_count != shape.action_count
				|| menu.action_menu_id == 0U)
			{
				throw std::invalid_argument("multiway full blueprint export shape is invalid");
			}
			for (std::size_t action = 0U; action < menu.action_count; ++action)
			{
				if (menu.actions[action].action_index != action
					|| menu.actions[action].action_menu_id != menu.action_menu_id)
					throw std::invalid_argument("multiway full blueprint export menu is invalid");
			}
		}

		std::uint64_t checked_export_add(std::uint64_t left, std::uint64_t right)
		{
			if (right > std::numeric_limits<std::uint64_t>::max() - left)
				throw std::overflow_error("multiway full blueprint export count overflows");
			return left + right;
		}

		std::uint64_t checked_export_multiply(std::uint64_t left, std::uint64_t right)
		{
			if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
				throw std::overflow_error("multiway full blueprint export count overflows");
			return left * right;
		}

		MultiwayFullBlueprintExportPlan make_export_plan(
			const MultiwaySolverCoordinator::ExportLease& lease,
			const MultiwayModelIdentity& identity,
			const MultiwayBlueprintTrainingMetadata& training,
			std::vector<std::uint32_t>& order)
		{
			const auto* compact = lease.compact_storage();
			const auto* external = lease.external_storage();
			if (external != nullptr)
			{
				MultiwayFullBlueprintExportPlan plan;
				plan.identity = identity;
				plan.training = training;
				plan.compact_direct_order = true;
				struct Context
				{
					MultiwayFullBlueprintExportPlan* plan;
					const MultiwayExternalTrainingStorage* storage;
				};
				Context context{ &plan, external };
				const auto visit_row = [](void* opaque, MultiwayPublicStateId state,
					std::int32_t seat, const MultiwayExternalRowMetadata& metadata) {
					auto& current = *static_cast<Context*>(opaque);
					MultiwaySparseRowShape shape{};
					shape.infoset = { state, static_cast<core::PlayerId>(seat) };
					shape.bucket_count = metadata.bucket_count;
					shape.action_count = metadata.action_count;
					std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
					std::size_t action_count = 0U;
					if (!current.storage->catalog().lookup_menu(state, actions.data(), actions.size(), action_count))
						throw std::invalid_argument("external blueprint row has no published action menu");
					const MultiwayPackedMenuView menu{ actions.data(),
						static_cast<std::uint8_t>(action_count), actions[0].action_menu_id };
					validate_export_menu(shape, menu);
					current.plan->shape_count = checked_export_add(current.plan->shape_count, 1U);
					current.plan->row_count = checked_export_add(
						current.plan->row_count, shape.bucket_count);
					current.plan->action_count = checked_export_add(current.plan->action_count,
						checked_export_multiply(shape.bucket_count, shape.action_count));
					return true;
				};
				if (!external->compact_storage().visit_ordered_rows(visit_row, &context) ||
					plan.shape_count != external->current_manifest().row_count ||
					plan.action_count != external->current_manifest().logical_value_count)
					throw std::invalid_argument("external blueprint counts disagree with the committed generation");
				const auto stream = make_multiway_full_blueprint_stream_plan(
					plan.identity, plan.training, plan.row_count, plan.action_count);
				plan.serialized_bytes = stream.serialized_bytes;
				plan.validate();
				order.clear();
				return plan;
			}
			const auto& rows = compact != nullptr ? compact->rows() : lease.storage().rows();
			if (rows.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("multiway full blueprint export shape index overflows");

			order.clear();
			if (compact == nullptr)
			{
				order.resize(rows.size());
				for (std::uint32_t index = 0U; index < order.size(); ++index)
					order[index] = index;
				std::sort(order.begin(), order.end(), [&rows](std::uint32_t left, std::uint32_t right) {
					return export_shape_less(rows[left].shape, rows[right].shape);
				});
			}

			MultiwayFullBlueprintExportPlan plan;
			plan.identity = identity;
			plan.training = training;
			plan.shape_count = rows.size();
			plan.compact_direct_order = compact != nullptr;
			plan.order_index_bytes = plan.compact_direct_order
				? 0U
				: checked_export_multiply(plan.shape_count, sizeof(std::uint32_t));
			const auto visit_shape = [&plan, &lease](const MultiwaySparseRowShape& shape) {
				const auto menu = lease.public_catalog().packed_menu(shape.infoset.public_state);
				validate_export_menu(shape, menu);
				plan.row_count = checked_export_add(plan.row_count, shape.bucket_count);
				plan.action_count = checked_export_add(
					plan.action_count,
					checked_export_multiply(shape.bucket_count, shape.action_count));
			};
			if (compact != nullptr)
			{
				bool has_previous = false;
				MultiwaySparseRowShape previous{};
				for (const auto& metadata : rows)
				{
					if (has_previous && !export_shape_less(previous, metadata.shape))
						throw std::invalid_argument("multiway compact export rows are not ordered");
					visit_shape(metadata.shape);
					previous = metadata.shape;
					has_previous = true;
				}
			}
			else
			{
				bool has_previous = false;
				MultiwaySparseRowShape previous{};
				for (const auto index : order)
				{
					const auto& shape = rows[index].shape;
					if (has_previous && !export_shape_less(previous, shape))
						throw std::invalid_argument("multiway Float64 export rows are not ordered");
					visit_shape(shape);
					previous = shape;
					has_previous = true;
				}
			}
			const auto stream = make_multiway_full_blueprint_stream_plan(
				plan.identity, plan.training, plan.row_count, plan.action_count);
			plan.serialized_bytes = stream.serialized_bytes;
			plan.validate();
			return plan;
		}

		void quantize_compact_strategy(
			const Probability* strategy,
			std::size_t action_count,
			const MultiwayPackedMenuView& menu,
			MultiwayQuantizedRootAction* output)
		{
			double total = 0.0;
			for (std::size_t action = 0U; action < action_count; ++action)
				total += strategy[action];
			std::uint32_t assigned = 0U;
			for (std::size_t action = 0U; action < action_count; ++action)
			{
				const auto remaining_actions = static_cast<std::uint32_t>(action_count - action - 1U);
				const auto remaining_mass = std::numeric_limits<std::uint16_t>::max() - assigned;
				const auto raw_probability = total > 0.0
					? static_cast<std::uint32_t>(std::floor(strategy[action] / total * kQuantizedProbabilityMax))
					: 0U;
				const auto probability = action + 1U == action_count
					? static_cast<std::uint16_t>(remaining_mass)
					: static_cast<std::uint16_t>(std::clamp(raw_probability, 1U, remaining_mass - remaining_actions));
				assigned += probability;
				output[action] = { menu.actions[action], probability };
			}
		}

		void quantize_float64_strategy(
			const Probability* strategy,
			std::size_t action_count,
			const MultiwayPackedMenuView& menu,
			MultiwayQuantizedRootAction* output)
		{
			std::uint32_t assigned = 0U;
			for (std::size_t action = 0U; action < action_count; ++action)
			{
				const auto remaining_actions = static_cast<std::uint32_t>(action_count - action - 1U);
				const auto remaining_mass = std::numeric_limits<std::uint16_t>::max() - assigned;
				const auto raw_probability = static_cast<std::uint32_t>(std::floor(strategy[action] * kQuantizedProbabilityMax));
				const auto probability = action + 1U == action_count
					? static_cast<std::uint16_t>(remaining_mass)
					: static_cast<std::uint16_t>(std::clamp(raw_probability, 1U, remaining_mass - remaining_actions));
				assigned += probability;
				output[action] = { menu.actions[action], probability };
			}
		}

		void sort_blueprint_rows(std::vector<MultiwayBlueprintRow>& rows)
		{
			std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
				if (left.infoset.public_state.value != right.infoset.public_state.value)
				{
					return left.infoset.public_state.value < right.infoset.public_state.value;
				}
				if (left.infoset.seat != right.infoset.seat)
					return left.infoset.seat < right.infoset.seat;
				return left.bucket < right.bucket;
			});
		}

	} // namespace

	void MultiwayFullBlueprintExportPlan::validate() const
	{
		identity.validate();
		if (shape_count > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("multiway full blueprint export shape index overflows");
		const auto stream = make_multiway_full_blueprint_stream_plan(
			identity, training, row_count, action_count);
		const auto expected_order_index_bytes = compact_direct_order
			? 0U
			: checked_export_multiply(shape_count, sizeof(std::uint32_t));
		if (serialized_bytes != stream.serialized_bytes
			|| order_index_bytes != expected_order_index_bytes)
			throw std::invalid_argument("multiway full blueprint export plan is invalid");
	}

	void MultiwayBlueprintTrainingCheckpoint::validate() const
	{
		identity.validate();
		if (training.trajectories < training.batches || coverage.admitted_rows != coordinator.storage.shapes.size() || coverage.terminal_visits != coordinator.terminal_visits || coverage.leaf_visits != coordinator.leaf_visits || coverage.missing_lookup_requests != coordinator.missing_lookup_requests || coverage.street_visits != coordinator.street_visits || coordinator.storage.regrets.size() != coordinator.storage.strategy_sums.size() || coordinator.storage.compact_regrets.size() != coordinator.storage.compact_strategy_masses.size())
		{
			throw std::invalid_argument("multiway training checkpoint metadata is invalid");
		}
		std::size_t expected_values = 0U;
		for (const auto& shape : coordinator.storage.shapes)
		{
			if (shape.infoset.seat < 0 || shape.bucket_count == 0U || shape.action_count == 0U)
			{
				throw std::invalid_argument("multiway training checkpoint row shape is invalid");
			}
			expected_values += static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
		}
		if (expected_values != coordinator.storage.regrets.size())
		{
			throw std::invalid_argument("multiway training checkpoint value count is invalid");
		}
		for (const auto value : coordinator.storage.regrets)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint regret is non-finite");
		}
		for (const auto value : coordinator.storage.strategy_sums)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint strategy sum is non-finite");
		}
		for (const auto value : late_window_baseline)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint baseline is non-finite");
		}
	}
	namespace
	{

		using texas::core::fingerprint::append_u64;

		template <class Range>
		void append_range(std::uint64_t& hash, const Range& values) noexcept
		{
			for (const auto value : values)
				append_u64(hash, value);
		}

		std::uint64_t hash_action_abstraction(const MultiwayActionAbstractionConfig& config) noexcept
		{
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
			const MultiwayBucketBaselineProfile& profile) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, bucket_model_version);
			append_u64(hash, profile.schema_version);
			append_u64(hash, profile.feature_version);
			append_u64(hash, profile.flop_bucket_count);
			append_u64(hash, profile.turn_bucket_count);
			append_u64(hash, profile.river_bucket_count);
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t combine_identity(const MultiwayModelIdentity& identity) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			visit_multiway_model_identity_components(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
			return hash == 0U ? 1U : hash;
		}

		bool due(std::uint64_t batch, std::uint64_t interval) noexcept
		{
			return interval != 0U && batch % interval == 0U;
		}

		MultiwayBlueprintTrainingMetadata make_metadata(
			const MultiwayBlueprintTrainingStatus& status,
			const MultiwayBlueprintIterationSchedule& schedule,
			std::uint64_t seed) noexcept
		{
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

	} // namespace

	MultiwayRootSnapshot make_multiway_initial_blueprint_root(
		const MultiwayGameRules& rules,
		MultiwayPrivateConfig private_ranges,
		const MultiwayActionAbstraction& action_abstraction,
		std::uint64_t action_abstraction_version,
		std::uint64_t leaf_model_version,
		PlayerId first_player)
	{
		rules.validate();
		auto state = games::multiway::MultiwayState::initial(rules, first_player);
		auto betting = state.snapshot();
		MultiwayRootSnapshot root;
		root.public_state = MultiwayPublicBuilder::make_root(
			betting, {}, action_abstraction.make_legal_actions(betting));
		root.root_infoset = { root.public_state.id, betting.current_player };
		root.root_bucket = 0U;
		root.seat_order.resize(rules.player_count);
		for (std::uint8_t seat = 0; seat < rules.player_count; ++seat)
			root.seat_order[seat] = seat;
		root.next_street_first_seat = first_player;
		root.odd_chip_first_seat = first_player;
		root.private_ranges = std::move(private_ranges);
		root.action_abstraction_version = action_abstraction_version;
		root.leaf_model_version = leaf_model_version;
		root.validate();
		return root;
	}

	void MultiwayBlueprintIterationSchedule::validate() const
	{
		if (!std::isfinite(regret_discount_factor) || regret_discount_factor <= 0.0 || regret_discount_factor > 1.0 || discount_interval_batches == 0U || pruning_interval_batches == 0U || !std::isfinite(pruning_threshold) || pruning_threshold > 0.0 || !std::isfinite(regret_floor) || regret_floor > 0.0 || !std::isfinite(pruning_exploration_probability) || pruning_exploration_probability < 0.0 || pruning_exploration_probability > 1.0 || !std::isfinite(pruning_action_probability_threshold) || pruning_action_probability_threshold < 0.0 || pruning_action_probability_threshold > 1.0)
		{
			throw std::invalid_argument("multiway blueprint schedule has invalid discount or pruning controls");
		}
	}

	double MultiwayBlueprintIterationSchedule::strategy_weight(std::uint64_t one_based_batch) const
	{
		if (one_based_batch == 0U)
		{
			throw std::invalid_argument("multiway blueprint strategy weight requires a one-based batch index");
		}
		return linear_iteration_weighting ? static_cast<double>(one_based_batch) : 1.0;
	}

	std::uint64_t MultiwayBlueprintIterationSchedule::identity() const noexcept
	{
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

	void MultiwayBlueprintTrainingConfig::validate() const
	{
		rules.validate();
		blueprint.validate();
		bucket_profile.validate();
		action_abstraction.validate();
		cfr.validate();
		limits.validate();
		schedule.validate();
		placement.validate();
		working_set_monitor.validate();
		if (deterministic_seed == 0U || cfr.player_count != rules.player_count || blueprint.player_count != rules.player_count || blueprint.initial_stack_chips != rules.initial_stack_chips || blueprint.small_blind_chips != rules.small_blind_chips || blueprint.big_blind_chips != rules.big_blind_chips || blueprint.ante_chips != rules.ante_chips || blueprint.rake_policy.identity() != rules.rake_policy.identity() || blueprint.rules_profile_version != rules.profile_version || blueprint.flop_bucket_count != bucket_profile.flop_bucket_count || blueprint.turn_bucket_count != bucket_profile.turn_bucket_count || blueprint.river_bucket_count != bucket_profile.river_bucket_count || max_decision_depth == 0U || max_decision_depth > MULTIWAY_MAX_DECISION_DEPTH || max_public_chance_depth > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH || !multiway_scheduler_mode_valid(scheduler_mode) || dynamic_chunk_size == 0U || (requested_worker_count != 0U && requested_worker_count < limits.worker_count))
		{
			throw std::invalid_argument("multiway training configuration has inconsistent artifact or traversal inputs");
		}
	}

	MultiwayModelIdentity MultiwayBlueprintTrainingConfig::identity() const
	{
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
		append_u64(runtime_hash, limits.regret_delta_capacity());
		append_u64(runtime_hash, limits.continuation_delta_capacity());
		// max_batches is a run horizon, not checkpoint/model semantics. Excluding
		// it permits a checkpoint to resume with a different remaining batch count.
		append_u64(runtime_hash, static_cast<std::uint8_t>(limits.storage_backend));
		append_u64(runtime_hash, limits.compact_page_width);
		if (limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			const auto storage_directory = limits.external_storage.directory.lexically_normal().generic_u8string();
			append_u64(runtime_hash, storage_directory.size());
			for (const auto character : storage_directory)
				texas::core::fingerprint::append_u8(runtime_hash, static_cast<std::uint8_t>(character));
			append_u64(runtime_hash, limits.external_storage.public_catalog_cache_bytes);
			append_u64(runtime_hash, limits.external_storage.row_page_cache_bytes);
			append_u64(runtime_hash, limits.external_storage.maximum_wal_bytes);
			append_u64(runtime_hash, limits.external_storage.segment_target_bytes);
			append_u64(runtime_hash, limits.external_storage.compaction_fan_in);
			append_u64(runtime_hash, limits.external_storage.compaction_scratch_bytes);
			append_u64(runtime_hash, limits.external_storage.disk_reserve_bytes);
			append_u64(runtime_hash, limits.external_storage.synchronous_durability ? 1U : 0U);
		}
		append_u64(runtime_hash, limits.discovery_public_state_capacity());
		append_u64(runtime_hash, limits.discovery_row_capacity());
		append_u64(runtime_hash, immutable_batch_epochs ? 1U : 0U);
		append_u64(runtime_hash, MULTIWAY_PARTITION_VERSION);
		append_u64(runtime_hash, MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION);
		append_u64(runtime_hash, MULTIWAY_SCHEDULER_POLICY_VERSION);
		append_u64(runtime_hash, static_cast<std::uint64_t>(scheduler_mode));
		append_u64(runtime_hash, dynamic_chunk_size);
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
		std::uint64_t deterministic_seed,
		std::uint32_t requested_worker_count,
		std::uint64_t memory_preflight_estimate_bytes, MultiwayMemoryLedger memory_ledger,
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots,
		MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots,
		const MultiwayBucketRegistry* buckets,
		const MultiwayFixedContinuationSelector* continuation_selector,
		MultiwayWorkingSetMonitor working_set_monitor)
		: identity_(identity), batch_runner_(&batch_runner), coordinator_(&coordinator), schedule_(schedule), deterministic_seed_(deterministic_seed), started_at_(std::chrono::steady_clock::now()), process_cpu_started_nanoseconds_(observed_multiway_training_process_cpu_nanoseconds()), buckets_(buckets), continuation_selector_(continuation_selector), working_set_monitor_(working_set_monitor)
	{
		identity_.validate();
		schedule_.validate();
		if (coordinator_->limits().storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
			throw std::invalid_argument("ExternalCompactInt32 is not ready for blueprint training");
		if (deterministic_seed_ == 0U)
		{
			throw std::invalid_argument("multiway blueprint trainer requires a non-zero deterministic seed");
		}
		status_.late_window_start_batch = schedule_.late_window_start_batch;
		status_.scheduler_mode = batch_runner_->scheduler_mode();
		status_.dynamic_chunk_size = batch_runner_->dynamic_chunk_size();
		status_.scheduler_policy_version = MULTIWAY_SCHEDULER_POLICY_VERSION;
		status_.static_block_scheduler_version = MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION;
		status_.partition_version = MULTIWAY_PARTITION_VERSION;
		status_.merge_order_version = MULTIWAY_MERGE_ORDER_VERSION;
		status_.admitted_rows_per_batch.reserve(256U);
		status_.configured_max_public_states = coordinator_->limits().max_public_states;
		status_.configured_max_sparse_rows = coordinator_->limits().max_sparse_rows;
		status_.configured_max_sparse_values = coordinator_->limits().max_sparse_values;
		status_.configured_max_regret_delta_entries_per_worker = coordinator_->limits().regret_delta_capacity();
		status_.configured_max_continuation_delta_entries_per_worker = coordinator_->limits().continuation_delta_capacity();
		status_.configured_max_discovery_public_states_per_worker = coordinator_->limits().discovery_public_state_capacity();
		status_.configured_max_discovery_rows_per_worker = coordinator_->limits().discovery_row_capacity();
		status_.configured_worker_delta_capacity = std::max(
			status_.configured_max_regret_delta_entries_per_worker,
			status_.configured_max_continuation_delta_entries_per_worker);
		status_.configured_discovery_capacity = std::max(
			status_.configured_max_discovery_public_states_per_worker,
			status_.configured_max_discovery_rows_per_worker);
		status_.requested_worker_count = requested_worker_count == 0U
			? coordinator_->limits().worker_count
			: requested_worker_count;
		status_.effective_worker_count = coordinator_->limits().worker_count;
		status_.trajectories_per_batch = coordinator_->limits().trajectories_per_batch;
		status_.memory_preflight_estimate_bytes = memory_preflight_estimate_bytes;
		status_.memory_ledger = memory_ledger;
		status_.working_set_gate_config = working_set_monitor_.gate;
		status_.working_set_gate.hard_limit_bytes = working_set_monitor_.gate.hard_limit_bytes;
		status_.working_set_gate.resident_allowance_bytes = working_set_monitor_.gate.resident_allowance_bytes;
		status_.working_set_gate.estimated_working_set_bytes = memory_ledger.estimated_working_set_peak_bytes;
		status_.system_memory_guard_config = working_set_monitor_.system_memory_guard;
		status_.process_memory_snapshots = process_memory_snapshots;
		status_.memory_subsystem_snapshots = memory_subsystem_snapshots;
	}

	void MultiwayBlueprintTrainer::record_process_memory_snapshot(
		MultiwayMemoryPhase phase,
		MultiwayMemoryComponent checkpoint_buffers,
		MultiwayMemoryComponent export_buffers)
	{
		if (status_.process_memory_snapshots.enabled && !status_.process_memory_snapshots.record_current(phase))
			throw std::logic_error("multiway training process memory snapshot phase order is invalid");
		if (status_.memory_subsystem_snapshots.enabled)
		{
			auto snapshot = capture_multiway_memory_subsystems(
				buckets_, coordinator_, batch_runner_, continuation_selector_, nullptr,
				checkpoint_buffers, export_buffers);
			if (!status_.memory_subsystem_snapshots.record(phase, snapshot))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
	}

	void MultiwayBlueprintTrainer::record_export_memory_snapshot(
		MultiwayMemoryPhase phase,
		MultiwayProcessMemorySnapshot snapshot,
		MultiwayMemoryComponent export_buffers) const
	{
		if (status_.process_memory_snapshots.enabled && !status_.process_memory_snapshots.record(phase, snapshot))
			throw std::logic_error("multiway training process memory snapshot phase order is invalid");
		if (status_.memory_subsystem_snapshots.enabled)
		{
			auto subsystems = capture_multiway_memory_subsystems(
				buckets_, coordinator_, batch_runner_, continuation_selector_, nullptr,
				{}, export_buffers);
			if (!status_.memory_subsystem_snapshots.record(phase, subsystems))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
	}

	void MultiwayBlueprintTrainer::run_batches(
		std::uint64_t batch_count,
		std::uint64_t trajectories_per_batch,
		std::uint64_t seed,
		MultiwayBlueprintBatchProgressCallback progress_callback,
		void* progress_context)
	{
		if (trajectories_per_batch == 0U || seed != deterministic_seed_ || batch_count > (std::numeric_limits<std::uint64_t>::max() - status_.trajectories) / trajectories_per_batch || batch_count > std::numeric_limits<std::uint64_t>::max() - status_.batches || status_.batches > std::numeric_limits<std::uint64_t>::max() - seed || batch_count > std::numeric_limits<std::uint64_t>::max() - seed - status_.batches)
		{
			throw std::invalid_argument("multiway blueprint trainer has an invalid batch request");
		}
		for (std::uint64_t batch = 0; batch < batch_count; ++batch)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.batch");
			const auto system_memory = working_set_monitor_.system_memory_observer == nullptr
				? capture_multiway_system_memory_snapshot()
				: working_set_monitor_.system_memory_observer(working_set_monitor_.system_memory_observer_context);
			const auto system_memory_guard = evaluate_multiway_system_memory_guard(
				system_memory, working_set_monitor_.system_memory_guard);
			status_.system_memory_guard = system_memory_guard;
			if (system_memory_guard.blocked())
			{
				status_.failed = true;
				throw std::runtime_error(
					std::string("multiway blueprint training system memory abort: abort_phase=runtime; rejected_category=") + system_memory_guard.rejected_category + "; message=" + system_memory_guard.message + "; used_physical_bytes=" + std::to_string(system_memory_guard.used_physical_bytes) + "; maximum_used_physical_bytes=" + std::to_string(system_memory_guard.maximum_used_physical_bytes) + "; total_physical_bytes=" + std::to_string(system_memory_guard.total_physical_bytes) + "; available_physical_bytes=" + std::to_string(system_memory_guard.available_physical_bytes) + "; minimum_available_physical_bytes=" + std::to_string(system_memory_guard.minimum_available_physical_bytes));
			}
			const auto pre_batch_memory = working_set_monitor_.observer == nullptr
				? capture_multiway_process_memory_snapshot()
				: working_set_monitor_.observer(working_set_monitor_.observer_context);
			const auto pre_batch_gate = evaluate_multiway_working_set_runtime(
				pre_batch_memory, working_set_monitor_.gate);
			if (pre_batch_gate.blocked())
			{
				status_.working_set_gate = pre_batch_gate;
				throw std::runtime_error(
					std::string("multiway blueprint training working-set abort: abort_phase=runtime; rejected_category=") + pre_batch_gate.rejected_category + "; admission_stage=" + std::to_string(static_cast<unsigned>(pre_batch_gate.admission_stage)));
			}
			const auto one_based_batch = status_.batches + 1U;
			if (status_.late_window_start_batch != 0U && one_based_batch == status_.late_window_start_batch && late_window_baseline_.empty())
			{
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.late_window_snapshot");
					late_window_baseline_ = coordinator_->export_root_strategy_sums();
				}
				status_.late_window_active = true;
			}
			batch_runner_->set_batch_number(one_based_batch);
			const auto batch_result = batch_runner_->run(
				status_.trajectories,
				trajectories_per_batch,
				seed + status_.batches,
				schedule_.strategy_weight(one_based_batch));
			if (!batch_result.clean)
			{
				status_.admission_diagnostics.accumulate(batch_result.admission_diagnostics);
				status_.failed_batch_available = true;
				status_.failed_batch = batch_result;
				if (batch_result.capacity_exhausted)
				{
					if (batch_result.regret_delta_capacity_exhausted || batch_result.continuation_delta_capacity_exhausted)
						status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::WorkerDeltas;
					else
						status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::Discovery;
				}
				else if (batch_result.discovery_capacity_exhausted)
					status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::Discovery;
				throw std::runtime_error(
					"multiway blueprint training batch did not complete cleanly: attempted="
					+ std::to_string(batch_result.trajectories_attempted)
					+ " accepted=" + std::to_string(batch_result.trajectories_accepted)
					+ " discarded=" + std::to_string(batch_result.trajectories_discarded)
					+ " delta_entries=" + std::to_string(batch_result.delta_entries_merged)
					+ " discovery_capacity_exhausted="
					+ (batch_result.discovery_capacity_exhausted ? "true" : "false"));
			}
			status_.trajectories += trajectories_per_batch;
			++status_.batches;
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.discount_prune");
				const auto trainer_phase_start = std::chrono::steady_clock::now();
				const bool discount_due = schedule_.discount_regrets && due(status_.batches, schedule_.discount_interval_batches);
				const bool pruning_due = schedule_.prune_negative_regrets && status_.batches > schedule_.pruning_warmup_batches && due(status_.batches - schedule_.pruning_warmup_batches, schedule_.pruning_interval_batches);
				if (pruning_due)
				{
					const auto pruning_batch = status_.batches - schedule_.pruning_warmup_batches;
					const bool recovery = schedule_.recovery_interval_batches != 0U && pruning_batch % schedule_.recovery_interval_batches == 0U;
					if (!recovery && discount_due)
					{
						status_.pruned_negative_regrets += coordinator_->scale_and_prune_regrets(
							schedule_.regret_discount_factor, schedule_.pruning_threshold, schedule_.regret_floor);
					}
					else if (!recovery)
					{
						status_.pruned_negative_regrets += coordinator_->prune_negative_regrets(
							schedule_.pruning_threshold, schedule_.regret_floor);
					}
					else if (discount_due)
					{
						coordinator_->scale_regrets(schedule_.regret_discount_factor);
					}
				}
				else if (discount_due)
				{
					coordinator_->scale_regrets(schedule_.regret_discount_factor);
				}
				status_.trainer_only_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - trainer_phase_start)
						.count());
			}
			const auto& diagnostics = coordinator_->diagnostics();
			status_.visited_public_descriptors = diagnostics.public_states_admitted;
			status_.admitted_rows = diagnostics.sparse_rows_admitted;
			status_.preflop_rows = diagnostics.preflop_rows_admitted;
			status_.flop_rows = diagnostics.flop_rows_admitted;
			status_.turn_rows = diagnostics.turn_rows_admitted;
			status_.river_rows = diagnostics.river_rows_admitted;
			status_.admitted_action_cells = coordinator_->compact_storage() != nullptr
				? coordinator_->compact_storage()->value_count()
				: coordinator_->storage().value_count();
			status_.terminal_visits = diagnostics.terminal_visits;
			status_.leaf_visits = diagnostics.leaf_visits;
			status_.street_visits = diagnostics.street_visits;
			status_.missing_lookup_requests = diagnostics.missing_lookup_requests;
			status_.discarded_trajectories = diagnostics.trajectories_discarded;
			status_.merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
			status_.schedule_fingerprint = batch_result.run.schedule_fingerprint;
			status_.peak_public_states = std::max(status_.peak_public_states,
				diagnostics.public_states_admitted);
			status_.peak_sparse_rows = std::max(status_.peak_sparse_rows,
				diagnostics.sparse_rows_admitted);
			status_.peak_sparse_values = std::max(status_.peak_sparse_values,
				static_cast<std::uint64_t>(coordinator_->compact_storage() != nullptr
						? coordinator_->compact_storage()->value_count()
						: coordinator_->storage().value_count()));
			status_.peak_worker_delta_entries = std::max(status_.peak_worker_delta_entries,
				batch_result.maximum_worker_delta_entries);
			status_.total_peak_regret_delta_entries = std::max(
				status_.total_peak_regret_delta_entries, batch_result.total_peak_regret_delta_entries);
			status_.total_peak_continuation_delta_entries = std::max(
				status_.total_peak_continuation_delta_entries, batch_result.total_peak_continuation_delta_entries);
			status_.total_peak_delta_entries = std::max(
				status_.total_peak_delta_entries, batch_result.total_peak_delta_entries);
			status_.generated_update_count += batch_result.generated_update_count;
			status_.generated_discovery_public_state_count += batch_result.generated_discovery_public_state_count;
			status_.generated_discovery_row_count += batch_result.generated_discovery_row_count;
			status_.coordinator_admission_nanoseconds += batch_result.coordinator_admission_nanoseconds;
			status_.memory_ledger.coordinator_allocation_count += batch_result.coordinator_allocation_count;
			status_.memory_ledger.coordinator_allocation_bytes += batch_result.coordinator_allocated_bytes;
			status_.admission_diagnostics.accumulate(batch_result.admission_diagnostics);
			status_.placement_policy = batch_result.placement_policy;
			status_.affinity_requested = batch_result.affinity_requested;
			status_.cumulative_worker_delta_entries = diagnostics.worker_delta_entries_merged;
			status_.worker_active_nanoseconds += batch_result.worker_active_nanoseconds;
			status_.worker_wake_latency_nanoseconds += batch_result.worker_wake_latency_nanoseconds;
			status_.worker_cv_wait_nanoseconds += batch_result.worker_cv_wait_nanoseconds;
			status_.worker_scheduler_claim_nanoseconds += batch_result.worker_scheduler_claim_nanoseconds;
			status_.worker_completion_nanoseconds += batch_result.worker_completion_nanoseconds;
			status_.worker_tail_idle_nanoseconds += batch_result.worker_tail_idle_nanoseconds;
			status_.minimum_worker_active_nanoseconds = status_.batches == 1U
				? batch_result.minimum_worker_active_nanoseconds
				: std::min(status_.minimum_worker_active_nanoseconds,
					  batch_result.minimum_worker_active_nanoseconds);
			status_.maximum_worker_active_nanoseconds = std::max(
				status_.maximum_worker_active_nanoseconds, batch_result.maximum_worker_active_nanoseconds);
			status_.coordinator_wait_nanoseconds += batch_result.coordinator_wait_nanoseconds;
			status_.barrier_wait_nanoseconds += batch_result.barrier_wait_nanoseconds;
			status_.coordinator_lock_wait_nanoseconds += batch_result.coordinator_lock_wait_nanoseconds;
			status_.delta_sort_nanoseconds += batch_result.delta_sort_nanoseconds;
			status_.merge_nanoseconds += batch_result.merge_nanoseconds;
			if (status_.worker_trajectory_counts.size() != batch_result.worker_trajectory_counts.size())
			{
				status_.worker_trajectory_counts.assign(batch_result.worker_trajectory_counts.size(), 0U);
				status_.worker_active_nanoseconds_by_worker.assign(
					batch_result.worker_active_nanoseconds_by_worker.size(), 0U);
				status_.worker_sort_nanoseconds_by_worker.assign(batch_result.worker_sort_nanoseconds_by_worker.size(), 0U);
				status_.worker_wake_latency_nanoseconds_by_worker.assign(batch_result.worker_wake_latency_nanoseconds_by_worker.size(), 0U);
				status_.worker_cv_wait_nanoseconds_by_worker.assign(batch_result.worker_cv_wait_nanoseconds_by_worker.size(), 0U);
				status_.worker_scheduler_claim_nanoseconds_by_worker.assign(batch_result.worker_scheduler_claim_nanoseconds_by_worker.size(), 0U);
				status_.worker_completion_nanoseconds_by_worker.assign(batch_result.worker_completion_nanoseconds_by_worker.size(), 0U);
				status_.worker_tail_idle_nanoseconds_by_worker.assign(batch_result.worker_tail_idle_nanoseconds_by_worker.size(), 0U);
				status_.worker_regret_delta_entries.assign(batch_result.worker_regret_delta_entries.size(), 0U);
				status_.worker_continuation_delta_entries.assign(batch_result.worker_continuation_delta_entries.size(), 0U);
				status_.worker_discovery_public_state_counts.assign(batch_result.worker_discovery_public_state_counts.size(), 0U);
				status_.worker_discovery_row_counts.assign(batch_result.worker_discovery_row_counts.size(), 0U);
				status_.worker_allocation_counts.assign(batch_result.worker_allocation_counts.size(), 0U);
				status_.worker_allocated_bytes.assign(batch_result.worker_allocated_bytes.size(), 0U);
				status_.worker_requested_processor.assign(batch_result.worker_requested_processor.size(), 0xffffffffU);
				status_.worker_resolved_processor.assign(batch_result.worker_resolved_processor.size(), 0U);
				status_.worker_requested_cpu_set.assign(batch_result.worker_requested_cpu_set.size(), kMultiwayUnknownTopologyId);
				status_.worker_resolved_cpu_set.assign(batch_result.worker_resolved_cpu_set.size(), kMultiwayUnknownTopologyId);
				status_.worker_requested_physical_core.assign(batch_result.worker_requested_physical_core.size(), kMultiwayUnknownTopologyId);
				status_.worker_resolved_physical_core.assign(batch_result.worker_resolved_physical_core.size(), kMultiwayUnknownTopologyId);
				status_.worker_requested_smt_sibling.assign(batch_result.worker_requested_smt_sibling.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_resolved_smt_sibling.assign(batch_result.worker_resolved_smt_sibling.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_requested_cache_domain.assign(batch_result.worker_requested_cache_domain.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_resolved_cache_domain.assign(batch_result.worker_resolved_cache_domain.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_migration_sampled.assign(batch_result.worker_migration_sampled.size(), 0U);
				status_.worker_migration_detected.assign(batch_result.worker_migration_detected.size(), 0U);
				status_.worker_affinity_masks.assign(batch_result.worker_affinity_masks.size(), 0U);
				status_.worker_affinity_succeeded.assign(batch_result.worker_affinity_succeeded.size(), 0U);
				status_.worker_profiles.assign(batch_result.worker_profiles.size(), MultiwaySearchProfileSnapshot{});
			}
			for (std::size_t worker_index = 0U;
				worker_index < batch_result.worker_trajectory_counts.size(); ++worker_index)
			{
				status_.worker_trajectory_counts[worker_index] +=
					batch_result.worker_trajectory_counts[worker_index];
				status_.worker_active_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_active_nanoseconds_by_worker[worker_index];
				status_.worker_sort_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_sort_nanoseconds_by_worker[worker_index];
				status_.worker_wake_latency_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_wake_latency_nanoseconds_by_worker[worker_index];
				status_.worker_cv_wait_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_cv_wait_nanoseconds_by_worker[worker_index];
				status_.worker_scheduler_claim_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_scheduler_claim_nanoseconds_by_worker[worker_index];
				status_.worker_completion_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_completion_nanoseconds_by_worker[worker_index];
				status_.worker_tail_idle_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_tail_idle_nanoseconds_by_worker[worker_index];
				status_.worker_regret_delta_entries[worker_index] = std::max(
					status_.worker_regret_delta_entries[worker_index], batch_result.worker_regret_delta_entries[worker_index]);
				status_.worker_continuation_delta_entries[worker_index] = std::max(
					status_.worker_continuation_delta_entries[worker_index], batch_result.worker_continuation_delta_entries[worker_index]);
				status_.worker_discovery_public_state_counts[worker_index] = std::max(
					status_.worker_discovery_public_state_counts[worker_index], batch_result.worker_discovery_public_state_counts[worker_index]);
				status_.worker_discovery_row_counts[worker_index] = std::max(
					status_.worker_discovery_row_counts[worker_index], batch_result.worker_discovery_row_counts[worker_index]);
				status_.worker_allocation_counts[worker_index] += batch_result.worker_allocation_counts[worker_index];
				status_.worker_allocated_bytes[worker_index] += batch_result.worker_allocated_bytes[worker_index];
				status_.memory_ledger.worker_allocation_count += batch_result.worker_allocation_counts[worker_index];
				status_.memory_ledger.worker_allocation_bytes += batch_result.worker_allocated_bytes[worker_index];
				status_.worker_resolved_processor[worker_index] = batch_result.worker_resolved_processor[worker_index];
				status_.worker_requested_processor[worker_index] = batch_result.worker_requested_processor[worker_index];
				status_.worker_affinity_masks[worker_index] = batch_result.worker_affinity_masks[worker_index];
				status_.worker_affinity_succeeded[worker_index] = batch_result.worker_affinity_succeeded[worker_index];
				status_.worker_requested_cpu_set[worker_index] = batch_result.worker_requested_cpu_set[worker_index];
				status_.worker_resolved_cpu_set[worker_index] = batch_result.worker_resolved_cpu_set[worker_index];
				status_.worker_requested_physical_core[worker_index] = batch_result.worker_requested_physical_core[worker_index];
				status_.worker_resolved_physical_core[worker_index] = batch_result.worker_resolved_physical_core[worker_index];
				status_.worker_requested_smt_sibling[worker_index] = batch_result.worker_requested_smt_sibling[worker_index];
				status_.worker_resolved_smt_sibling[worker_index] = batch_result.worker_resolved_smt_sibling[worker_index];
				status_.worker_requested_cache_domain[worker_index] = batch_result.worker_requested_cache_domain[worker_index];
				status_.worker_resolved_cache_domain[worker_index] = batch_result.worker_resolved_cache_domain[worker_index];
				status_.worker_migration_sampled[worker_index] = std::max(
					status_.worker_migration_sampled[worker_index], batch_result.worker_migration_sampled[worker_index]);
				status_.worker_migration_detected[worker_index] = std::max(
					status_.worker_migration_detected[worker_index], batch_result.worker_migration_detected[worker_index]);
				if (batch_result.worker_profiles[worker_index].profiled())
				{
					status_.worker_profiles[worker_index].mode = batch_result.worker_profiles[worker_index].mode;
					for (std::size_t stage = 0U; stage < status_.worker_profiles[worker_index].checkpoints.size(); ++stage)
					{
						status_.worker_profiles[worker_index].checkpoints[stage].elapsed_nanoseconds +=
							batch_result.worker_profiles[worker_index].checkpoints[stage].elapsed_nanoseconds;
						status_.worker_profiles[worker_index].checkpoints[stage].calls +=
							batch_result.worker_profiles[worker_index].checkpoints[stage].calls;
					}
				}
			}
			status_.minimum_worker_trajectories = status_.batches == 1U
				? batch_result.minimum_worker_trajectories
				: std::min(status_.minimum_worker_trajectories, batch_result.minimum_worker_trajectories);
			status_.maximum_worker_trajectories = std::max(
				status_.maximum_worker_trajectories, batch_result.maximum_worker_trajectories);
			status_.peak_compact_storage_bytes = std::max(status_.peak_compact_storage_bytes,
				static_cast<std::uint64_t>(coordinator_->compact_storage() != nullptr
						? coordinator_->compact_storage()->memory_bytes()
						: 0U));
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.telemetry.process_sample");
				const auto process_memory = capture_multiway_process_memory_snapshot();
				const auto runtime_gate = evaluate_multiway_working_set_runtime(
					working_set_monitor_.observer == nullptr
						? process_memory
						: working_set_monitor_.observer(working_set_monitor_.observer_context),
					working_set_monitor_.gate);
				status_.working_set_gate = runtime_gate;
				if (runtime_gate.blocked())
				{
					throw std::runtime_error(
						std::string("multiway blueprint training working-set abort: abort_phase=runtime; rejected_category=") + runtime_gate.rejected_category + "; admission_stage=" + std::to_string(static_cast<unsigned>(runtime_gate.admission_stage)));
				}
				status_.current_process_rss_bytes = process_memory.current_working_set_available
					? process_memory.current_working_set_bytes
					: 0U;
				status_.process_rss_available = process_memory.current_working_set_available;
				status_.peak_process_rss_bytes = std::max(status_.peak_process_rss_bytes,
					status_.current_process_rss_bytes);
				const auto process_cpu_now = observed_multiway_training_process_cpu_nanoseconds();
				status_.process_cpu_nanoseconds = process_cpu_now >= process_cpu_started_nanoseconds_
					? process_cpu_now - process_cpu_started_nanoseconds_
					: 0U;
				status_.peak_working_set_bytes = std::max(status_.peak_working_set_bytes,
					process_memory.peak_working_set_available ? process_memory.peak_working_set_bytes : 0U);
				if (status_.process_memory_snapshots.enabled && status_.process_memory_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterColdBatch))
				{
					if (!status_.process_memory_snapshots.record(MultiwayMemoryPhase::AfterColdBatch, process_memory))
						throw std::logic_error("multiway training memory snapshot phase order is invalid");
				}
				if (status_.memory_subsystem_snapshots.enabled && status_.memory_subsystem_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterColdBatch))
				{
					if (!status_.memory_subsystem_snapshots.record(
							MultiwayMemoryPhase::AfterColdBatch,
							capture_multiway_memory_subsystems(buckets_, coordinator_, batch_runner_, continuation_selector_)))
						throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
				}
				if (status_.process_memory_snapshots.enabled && status_.process_memory_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterSteadyBatch) && coordinator_->limits().max_batches != 0U && status_.batches >= coordinator_->limits().max_batches)
				{
					if (!status_.process_memory_snapshots.record(MultiwayMemoryPhase::AfterSteadyBatch, process_memory))
						throw std::logic_error("multiway training memory snapshot phase order is invalid");
				}
				if (status_.memory_subsystem_snapshots.enabled && status_.memory_subsystem_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterSteadyBatch) && coordinator_->limits().max_batches != 0U && status_.batches >= coordinator_->limits().max_batches)
				{
					if (!status_.memory_subsystem_snapshots.record(
							MultiwayMemoryPhase::AfterSteadyBatch,
							capture_multiway_memory_subsystems(buckets_, coordinator_, batch_runner_, continuation_selector_)))
						throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
				}
			}
			status_.elapsed_wall_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started_at_)
					.count());
			if (status_.admitted_rows_per_batch.size() < 256U)
			{
				status_.admitted_rows_per_batch.push_back(status_.admitted_rows);
			}
			else
			{
				status_.admitted_rows_per_batch.back() = status_.admitted_rows;
			}
			if (status_.configured_max_public_states != 0U && status_.peak_public_states >= status_.configured_max_public_states)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::PublicStates;
			}
			else if (status_.configured_max_sparse_rows != 0U && status_.peak_sparse_rows >= status_.configured_max_sparse_rows)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::SparseRows;
			}
			else if (status_.configured_max_sparse_values != 0U && status_.peak_sparse_values >= status_.configured_max_sparse_values)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::SparseValues;
			}
			else if (status_.configured_worker_delta_capacity != 0U && status_.peak_worker_delta_entries >= status_.configured_worker_delta_capacity)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::WorkerDeltas;
			}
			TEXASSOLVER_TRACY_FRAME_MARK("mw.batch");
			TEXASSOLVER_TRACY_PLOT("mw.trajectories.accepted",
				status_.trajectories >= status_.discarded_trajectories
					? status_.trajectories - status_.discarded_trajectories
					: 0U);
			TEXASSOLVER_TRACY_PLOT("mw.worker.active_ns.total", status_.worker_active_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.worker.active_ns.max", status_.maximum_worker_active_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.worker.tail_idle_ns.total", status_.worker_tail_idle_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.delta.regret_entries", status_.total_peak_regret_delta_entries);
			TEXASSOLVER_TRACY_PLOT("mw.delta.continuation_entries", status_.total_peak_continuation_delta_entries);
			TEXASSOLVER_TRACY_PLOT("mw.discovery.public_states", status_.peak_public_states);
			TEXASSOLVER_TRACY_PLOT("mw.discovery.rows", status_.peak_sparse_rows);
			TEXASSOLVER_TRACY_PLOT("mw.phase.admission_ns", status_.coordinator_admission_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.phase.merge_ns", status_.merge_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.phase.trainer_only_ns", status_.trainer_only_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.memory.rss_bytes", status_.current_process_rss_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.current_working_set_bytes", status_.current_process_rss_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.private_committed_bytes",
				status_.process_memory_snapshots.count == 0U || !status_.process_memory_snapshots.snapshots[status_.process_memory_snapshots.count - 1U].private_committed_available
					? 0U
					: status_.process_memory_snapshots.snapshots[status_.process_memory_snapshots.count - 1U].private_committed_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.peak_working_set_bytes", status_.peak_working_set_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.process.cpu_ns.delta", status_.process_cpu_nanoseconds);
			if (progress_callback != nullptr)
				progress_callback(status_, progress_context);
		}
	}

	MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainer::coverage_manifest() const noexcept
	{
		return {
			status_.visited_public_descriptors,
			status_.admitted_rows,
			status_.admitted_action_cells,
			status_.terminal_visits,
			status_.leaf_visits,
			status_.missing_lookup_requests,
			status_.preflop_rows,
			status_.flop_rows,
			status_.turn_rows,
			status_.river_rows,
			status_.street_visits,
		};
	}

	MultiwayFullBlueprintArtifact MultiwayBlueprintTrainer::export_full_policy() const
	{
		MultiwayFullBlueprintArtifact artifact;
		artifact.identity = identity_;
		artifact.training = make_metadata(status_, schedule_, deterministic_seed_);
		if (coordinator_->compact_storage() != nullptr)
		{
			const auto* storage = coordinator_->compact_storage();
			for (const auto& metadata : storage->rows())
			{
				const auto& shape = metadata.shape;
				const auto* state = coordinator_->find_public_state(shape.infoset.public_state);
				if (state == nullptr)
					throw std::logic_error("compact row has no admitted public state");
				for (std::uint32_t bucket = 0; bucket < shape.bucket_count; ++bucket)
				{
					const auto strategy = storage->average_strategy(shape.infoset, bucket);
					MultiwayBlueprintRow row;
					row.infoset = shape.infoset;
					row.bucket = bucket;
					row.action_menu_id = state->legal_actions.front().action_menu_id;
					double total = 0.0;
					for (std::uint8_t action = 0; action < shape.action_count; ++action)
						total += strategy[action];
					std::uint32_t assigned = 0U;
					for (std::uint8_t action = 0; action < shape.action_count; ++action)
					{
						const auto mass = strategy[action];
						const auto remaining_actions = static_cast<std::uint32_t>(shape.action_count - action - 1U);
						const auto remaining_mass = std::numeric_limits<std::uint16_t>::max() - assigned;
						const auto raw_probability = total > 0.0
							? static_cast<std::uint32_t>(std::floor(mass / total * std::numeric_limits<std::uint16_t>::max()))
							: 0U;
						const auto probability = action + 1U == shape.action_count
							? static_cast<std::uint16_t>(remaining_mass)
							: static_cast<std::uint16_t>(std::clamp(raw_probability, 1U, remaining_mass - remaining_actions));
						assigned += probability;
						row.actions.push_back({ state->legal_actions[action], probability });
					}
					artifact.rows.push_back(std::move(row));
				}
			}
			sort_blueprint_rows(artifact.rows);
			artifact.payload_hash = MultiwayFullBlueprintArtifacts::payload_hash(artifact);
			artifact.validate();
			return artifact;
		}
		const auto& storage = coordinator_->storage();
		for (const auto& metadata : storage.rows())
		{
			const auto* state = coordinator_->find_public_state(metadata.shape.infoset.public_state);
			if (state == nullptr)
				throw std::logic_error("multiway sparse row has no admitted public state");
			for (std::uint32_t bucket = 0U; bucket < metadata.shape.bucket_count; ++bucket)
			{
				const auto strategy = storage.average_strategy(metadata.shape.infoset, bucket);
				MultiwayBlueprintRow row;
				row.infoset = metadata.shape.infoset;
				row.bucket = bucket;
				row.action_menu_id = state->legal_actions.front().action_menu_id;
				row.actions.reserve(strategy.size());
				std::uint32_t assigned = 0U;
				for (std::size_t action = 0U; action < strategy.size(); ++action)
				{
					const auto remaining_actions = static_cast<std::uint32_t>(strategy.size() - action - 1U);
					const auto remaining_mass = std::numeric_limits<std::uint16_t>::max() - assigned;
					const auto raw_probability = static_cast<std::uint32_t>(std::floor(strategy[action] * std::numeric_limits<std::uint16_t>::max()));
					const auto probability = action + 1U == strategy.size()
						? static_cast<std::uint16_t>(remaining_mass)
						: static_cast<std::uint16_t>(std::clamp(raw_probability, 1U, remaining_mass - remaining_actions));
					assigned += probability;
					row.actions.push_back({ state->legal_actions[action], probability });
				}
				artifact.rows.push_back(std::move(row));
			}
		}
		sort_blueprint_rows(artifact.rows);
		artifact.payload_hash = MultiwayFullBlueprintArtifacts::payload_hash(artifact);
		artifact.validate();
		return artifact;
	}

	MultiwayFullBlueprintExportResult MultiwayBlueprintTrainer::save_full_policy_atomic(
		const std::filesystem::path& path) const
	{
		const auto export_started = std::chrono::steady_clock::now();
		MultiwayDurationRecorder export_duration(status_.export_wall_nanoseconds);
		auto lease = coordinator_->acquire_export_lease();
		const auto training = make_metadata(status_, schedule_, deterministic_seed_);
		std::vector<std::uint32_t> order;
		const auto plan = make_export_plan(lease, identity_, training, order);
		const auto stream_plan = make_multiway_full_blueprint_stream_plan(
			plan.identity, plan.training, plan.row_count, plan.action_count);
		std::array<Probability, MULTIWAY_MAX_ABSTRACTED_ACTIONS> strategy{};
		std::array<MultiwayQuantizedRootAction, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
		const auto peak_scratch_bytes = checked_export_add(
			checked_export_add(plan.order_index_bytes, sizeof(strategy)), sizeof(actions));
		MultiwayExportTelemetry telemetry;
		telemetry.attempted = true;
		telemetry.plan = {
			MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION,
			plan.shape_count,
			plan.row_count,
			plan.action_count,
			plan.serialized_bytes,
			plan.order_index_bytes,
			plan.compact_direct_order
		};
		auto export_gate = working_set_monitor_.export_gate;
		if (export_gate.process.mapped_residency_allowance_bytes == 0U)
			export_gate.process.mapped_residency_allowance_bytes = status_.memory_ledger.mapped_resident_allowance_bytes;
		telemetry.scratch = make_multiway_export_scratch_estimate(telemetry.plan, export_gate.process);
		const auto export_component = [peak_scratch_bytes]() noexcept {
			MultiwayMemoryComponent component;
			component.add_buffer(1U, 1U, static_cast<std::size_t>(peak_scratch_bytes));
			return component;
		};
		telemetry.system_before = working_set_monitor_.export_system_observer == nullptr
			? capture_multiway_system_memory_snapshot()
			: working_set_monitor_.export_system_observer(working_set_monitor_.export_observer_context);
		status_.system_memory_guard = evaluate_multiway_system_memory_guard(
			telemetry.system_before, working_set_monitor_.system_memory_guard);
		if (status_.system_memory_guard.blocked())
		{
			status_.failed = true;
			status_.export_telemetry = telemetry;
			throw std::runtime_error(
				std::string("multiway blueprint export system memory abort: rejected_category=") + status_.system_memory_guard.rejected_category + "; message=" + status_.system_memory_guard.message + "; used_physical_bytes=" + std::to_string(status_.system_memory_guard.used_physical_bytes) + "; maximum_used_physical_bytes=" + std::to_string(status_.system_memory_guard.maximum_used_physical_bytes));
		}
		const auto before_export = working_set_monitor_.export_process_observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.export_process_observer(working_set_monitor_.export_observer_context);
		telemetry.process_estimate = make_multiway_export_process_estimate(
			telemetry.plan, before_export, export_gate.process);
		status_.memory_ledger.export_scratch_bytes = telemetry.scratch.total_bytes;
		status_.memory_ledger.export_output_bytes = telemetry.plan.serialized_bytes;
		status_.memory_ledger.export_process_peak_bytes = telemetry.process_estimate.estimated_process_export_peak_bytes;
		record_export_memory_snapshot(MultiwayMemoryPhase::BeforeExport, before_export, export_component());
		telemetry.process_gate = evaluate_multiway_export_process_gate(
			telemetry.plan, before_export, export_gate.process);
		telemetry.system_gate = evaluate_multiway_export_system_gate(
			telemetry.plan, telemetry.scratch, telemetry.system_before, export_gate.system);
		status_.memory_ledger.export_system_cache_allowance_bytes = telemetry.plan.serialized_bytes;
		if (telemetry.process_gate.blocked() || telemetry.system_gate.blocked())
		{
			status_.failed = true;
			status_.export_telemetry = telemetry;
			const auto reason = telemetry.process_gate.blocked()
				? multiway_export_process_rejection_reason_name(telemetry.process_gate.rejection_reason)
				: multiway_export_system_rejection_reason_name(telemetry.system_gate.rejection_reason);
			throw std::runtime_error(std::string("multiway blueprint export memory gate rejected: ") + reason);
		}
		MultiwayFullBlueprintStreamWriter writer(path, stream_plan);
		const auto* compact = lease.compact_storage();
		const auto* external = lease.external_storage();
		const auto& rows = compact != nullptr ? compact->rows() : lease.storage().rows();
		const auto emit_shape = [&](const MultiwaySparseRowShape& shape) {
			const auto menu = lease.public_catalog().packed_menu(shape.infoset.public_state);
			for (std::uint32_t bucket = 0U; bucket < shape.bucket_count; ++bucket)
			{
				if (compact != nullptr)
					compact->average_strategy_into(shape.infoset, bucket, strategy.data(), shape.action_count);
				else
					lease.storage().average_strategy_into(shape.infoset, bucket, strategy.data(), shape.action_count);
				if (compact != nullptr)
					quantize_compact_strategy(strategy.data(), shape.action_count, menu, actions.data());
				else
					quantize_float64_strategy(strategy.data(), shape.action_count, menu, actions.data());
				MultiwayFullBlueprintRowView row;
				row.infoset = shape.infoset;
				row.bucket = bucket;
				row.action_menu_id = menu.action_menu_id;
				row.actions = actions.data();
				row.action_count = shape.action_count;
				writer.write_row(row);
			}
		};
		if (external != nullptr)
		{
			struct Context
			{
				const MultiwayExternalTrainingStorage* storage;
				MultiwayFullBlueprintStreamWriter* writer;
				Probability* strategy;
				MultiwayQuantizedRootAction* actions;
			};
			Context context{ external, &writer, strategy.data(), actions.data() };
			const auto write_external_row = [](void* opaque, MultiwayPublicStateId state,
				std::int32_t seat, const MultiwayExternalRowMetadata& metadata) {
				auto& current = *static_cast<Context*>(opaque);
				MultiwaySparseRowShape shape{};
				shape.infoset = { state, static_cast<core::PlayerId>(seat) };
				shape.bucket_count = metadata.bucket_count;
				shape.action_count = metadata.action_count;
				std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> menu_actions{};
				std::size_t action_count = 0U;
				if (!current.storage->catalog().lookup_menu(state, menu_actions.data(),
					menu_actions.size(), action_count))
					throw std::invalid_argument("external blueprint row has no published action menu");
				const MultiwayPackedMenuView menu{ menu_actions.data(),
					static_cast<std::uint8_t>(action_count), menu_actions[0].action_menu_id };
				validate_export_menu(shape, menu);
				MultiwayExternalRowBinding binding;
				if (!current.storage->compact_storage().lookup_row(shape.infoset, binding))
					throw std::invalid_argument("external blueprint row index changed during export");
				for (std::uint32_t bucket = 0U; bucket < shape.bucket_count; ++bucket)
				{
					current.storage->compact_storage().average_strategy_into(
						binding, bucket, current.strategy, shape.action_count);
					quantize_compact_strategy(current.strategy, shape.action_count, menu,
						current.actions);
					MultiwayFullBlueprintRowView row;
					row.infoset = shape.infoset;
					row.bucket = bucket;
					row.action_menu_id = menu.action_menu_id;
					row.actions = current.actions;
					row.action_count = shape.action_count;
					current.writer->write_row(row);
				}
				return true;
			};
			if (!external->compact_storage().visit_ordered_rows(write_external_row, &context))
				throw std::runtime_error("external blueprint ordered export stopped unexpectedly");
		}
		else if (compact != nullptr)
		{
			for (const auto& metadata : rows)
				emit_shape(metadata.shape);
		}
		else
		{
			for (const auto index : order)
				emit_shape(rows[index].shape);
		}
		const auto stream_result = writer.finish();
		const auto after_export = working_set_monitor_.export_process_observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.export_process_observer(working_set_monitor_.export_observer_context);
		telemetry.system_after = working_set_monitor_.export_system_observer == nullptr
			? capture_multiway_system_memory_snapshot()
			: working_set_monitor_.export_system_observer(working_set_monitor_.export_observer_context);
		telemetry.observed_delta = observe_multiway_export_memory_delta(before_export, after_export);
		status_.memory_ledger.export_system_cache_allowance_bytes = telemetry.plan.serialized_bytes;
		status_.memory_ledger.export_observed_before_working_set_bytes = telemetry.observed_delta.before_working_set_bytes;
		status_.memory_ledger.export_observed_after_working_set_bytes = telemetry.observed_delta.after_working_set_bytes;
		telemetry.result_bytes = stream_result.serialized_bytes;
		telemetry.payload_hash = stream_result.payload_hash;
		telemetry.policy_content_hash = stream_result.policy_content_hash;
		telemetry.duration_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - export_started)
				.count());
		telemetry.publication_mode = stream_result.publication_mode;
		telemetry.succeeded = true;
		status_.export_telemetry = telemetry;
		record_export_memory_snapshot(MultiwayMemoryPhase::AfterExport, after_export, export_component());
		MultiwayFullBlueprintExportResult result;
		result.plan = plan;
		result.payload_hash = stream_result.payload_hash;
		result.policy_content_hash = stream_result.policy_content_hash;
		result.peak_scratch_bytes = peak_scratch_bytes;
		result.publication_mode = stream_result.publication_mode;
		return result;
	}

	MultiwayTrainingCheckpointWriteResult MultiwayBlueprintTrainer::save_checkpoint_atomic(
		const std::filesystem::path& path) const
	{
		MultiwayDurationRecorder checkpoint_duration(status_.checkpoint_wall_nanoseconds);
		auto lease = coordinator_->acquire_checkpoint_lease();
		const auto training = make_metadata(status_, schedule_, deterministic_seed_);
		const auto coverage = coverage_manifest();
		const auto& diagnostics = lease.diagnostics();
		MultiwayTrainingCheckpointStreamSource source;
		source.identity = &identity_;
		source.training = &training;
		source.coverage = &coverage;
		source.catalog = &lease.public_catalog();
		source.storage = &lease.storage();
		source.compact_storage = lease.compact_storage();
		source.late_window_baseline = &late_window_baseline_;
		source.gate = &working_set_monitor_.export_gate;
		source.terminal_visits = diagnostics.terminal_visits;
		source.leaf_visits = diagnostics.leaf_visits;
		source.missing_lookup_requests = diagnostics.missing_lookup_requests;
		source.last_merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
		source.street_visits = diagnostics.street_visits;
		MultiwayCheckpointTelemetry telemetry;
		auto result = MultiwayTrainingCheckpointArtifacts::save_atomic_streamed(path, source, &telemetry);
		result.telemetry = telemetry;
		return result;
	}

	MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainer::checkpoint() const
	{
		MultiwayBlueprintTrainingCheckpoint result;
		result.identity = identity_;
		result.training = make_metadata(status_, schedule_, deterministic_seed_);
		result.coverage = coverage_manifest();
		result.coordinator = coordinator_->checkpoint();
		result.telemetry = result.coordinator.telemetry;
		result.late_window_baseline = late_window_baseline_;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.training_validation");
			const auto started = std::chrono::steady_clock::now();
			result.validate();
			result.telemetry.validation_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		return result;
	}

	MultiwayBlueprintSnapshot MultiwayBlueprintTrainer::publish(MultiwayBlueprintPolicyKind policy_kind) const
	{
		auto metadata = make_metadata(status_, schedule_, deterministic_seed_);
		if (policy_kind == MultiwayBlueprintPolicyKind::LateWindowAverage)
		{
			if (late_window_baseline_.empty())
			{
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
			for (std::size_t index = 0; index < policy.actions.size(); ++index)
			{
				const auto quantized = index + 1U == policy.actions.size()
					? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
					: static_cast<std::uint16_t>(std::floor(policy.actions[index].probability * std::numeric_limits<std::uint16_t>::max()));
				assigned += quantized;
				snapshot.actions.push_back({ policy.actions[index].action, quantized });
			}
			snapshot.validate();
			return snapshot;
		}
		return export_multiway_root_snapshot(identity_, *coordinator_, status_.trajectories, policy_kind, metadata);
	}

	void MultiwayBlueprintTrainer::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot)
	{
		MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, identity_);
		if (snapshot.training.schedule_hash != schedule_.identity() || snapshot.training.deterministic_seed != deterministic_seed_)
		{
			throw std::invalid_argument("multiway checkpoint schedule or seed does not match the live trainer");
		}
		if (snapshot.training.trajectories != snapshot.trajectories || snapshot.training.batches != status_.batches || snapshot.trajectories != status_.trajectories)
		{
			throw std::invalid_argument("multiway compact checkpoint requires matching live training state");
		}
	}

	void MultiwayBlueprintTrainer::resume_from_checkpoint(
		const MultiwayBlueprintTrainingCheckpoint& checkpoint)
	{
		checkpoint.validate();
		checkpoint.identity.validate();
		if (checkpoint.identity != identity_ || checkpoint.training.schedule_hash != schedule_.identity() || checkpoint.training.deterministic_seed != deterministic_seed_ || checkpoint.training.trajectories < checkpoint.training.batches || checkpoint.training.linear_iteration_weighting != (schedule_.linear_iteration_weighting ? 1U : 0U) || checkpoint.training.discounting_enabled != (schedule_.discount_regrets ? 1U : 0U) || checkpoint.training.negative_regret_pruning_enabled != (schedule_.prune_negative_regrets ? 1U : 0U))
		{
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
		status_.preflop_rows = checkpoint.coverage.preflop_rows;
		status_.flop_rows = checkpoint.coverage.flop_rows;
		status_.turn_rows = checkpoint.coverage.turn_rows;
		status_.river_rows = checkpoint.coverage.river_rows;
		status_.street_visits = checkpoint.coverage.street_visits;
		late_window_baseline_ = checkpoint.late_window_baseline;
		const auto& diagnostics = coordinator_->diagnostics();
		status_.visited_public_descriptors = diagnostics.public_states_admitted;
		status_.admitted_rows = diagnostics.sparse_rows_admitted;
		status_.preflop_rows = diagnostics.preflop_rows_admitted;
		status_.flop_rows = diagnostics.flop_rows_admitted;
		status_.turn_rows = diagnostics.turn_rows_admitted;
		status_.river_rows = diagnostics.river_rows_admitted;
		status_.discarded_trajectories = diagnostics.trajectories_discarded;
		status_.merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
		status_.admitted_action_cells = coordinator_->storage().value_count();
		status_.street_visits = diagnostics.street_visits;
	}

	MultiwayBlueprintTrainingSession::MultiwayBlueprintTrainingSession(
		MultiwayBlueprintTrainingConfig config,
		MultiwayRootSnapshot root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayLeafEvaluator* leaf_evaluator)
		: config_(std::move(config)), root_(std::move(root)), buckets_(&buckets)
	{
		config_.validate();
		root_.validate();
		if (config_.limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
			throw std::invalid_argument("ExternalCompactInt32 is not ready for blueprint training");
		const auto identity = config_.identity();
		// Bucket artifacts carry their producer model identity. Runtime training
		// identity adds schedule/worker fields and is intentionally separate.
		if (root_.seat_order.size() != config_.rules.player_count || root_.rake_policy.identity() != config_.rules.rake_policy.identity() || root_.action_abstraction_version != config_.blueprint.action_abstraction_version || root_.leaf_model_version != config_.blueprint.terminal_model_version || (leaf_evaluator != nullptr && !leaf_evaluator->valid()))
		{
			throw std::invalid_argument("multiway training session has incompatible root, bucket, or leaf inputs");
		}
		if (leaf_evaluator != nullptr)
			leaf_evaluator_ = *leaf_evaluator;
		MultiwaySolveRequest request(root_, config_.cfr, config_.limits);
		coordinator_ = std::make_unique<MultiwaySolverCoordinator>(request);
		if (!config_.process_memory_snapshots.record_current(MultiwayMemoryPhase::AfterCoordinatorConstruction))
			throw std::logic_error("multiway training memory snapshot phase order is invalid");
		if (config_.memory_subsystem_snapshots.enabled)
		{
			if (!config_.memory_subsystem_snapshots.record(
					MultiwayMemoryPhase::AfterCoordinatorConstruction,
					capture_multiway_memory_subsystems(buckets_, coordinator_.get(), nullptr)))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
		action_abstraction_ = std::make_unique<MultiwayActionAbstraction>(config_.action_abstraction);
		continuation_selector_ = std::make_unique<MultiwayFixedContinuationSelector>(
			config_.blueprint.continuation_policy);
		MultiwayRootExternalSamplingTraversal traversal(
			*coordinator_, root_, *action_abstraction_, *buckets_, leaf_evaluator == nullptr ? nullptr : &leaf_evaluator_,
			config_.max_decision_depth, config_.max_public_chance_depth, nullptr, continuation_selector_.get(), nullptr,
			{ config_.schedule.prune_negative_regrets, config_.schedule.pruning_warmup_batches,
				config_.schedule.recovery_interval_batches, config_.schedule.pruning_exploration_probability,
				config_.schedule.pruning_action_probability_threshold,
				config_.schedule.pruning_threshold });
		batch_runner_ = std::make_unique<MultiwayRootBatchRunner>(
			std::move(traversal), *coordinator_, config_.limits.worker_count,
			MultiwayWorkerStreamCapacities{
				config_.limits.regret_delta_capacity(),
				config_.limits.continuation_delta_capacity(),
				config_.limits.discovery_public_state_capacity(),
				config_.limits.discovery_row_capacity() },
			MultiwaySearchProfileMode::Disabled,
			config_.pin_workers
				? MultiwayWorkerPlacementConfig{ MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity, 0U }
				: config_.placement,
			config_.immutable_batch_epochs,
			config_.scheduler_mode,
			config_.dynamic_chunk_size);
		if (!config_.process_memory_snapshots.record_current(MultiwayMemoryPhase::AfterWorkerPoolConstruction))
			throw std::logic_error("multiway training memory snapshot phase order is invalid");
		if (config_.memory_subsystem_snapshots.enabled)
		{
			if (!config_.memory_subsystem_snapshots.record(
					MultiwayMemoryPhase::AfterWorkerPoolConstruction,
					capture_multiway_memory_subsystems(buckets_, coordinator_.get(), batch_runner_.get(), continuation_selector_.get())))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
		trainer_ = std::make_unique<MultiwayBlueprintTrainer>(
			identity, *batch_runner_, *coordinator_, config_.schedule, config_.deterministic_seed,
			config_.requested_worker_count, config_.memory_preflight_estimate_bytes, config_.memory_ledger,
			config_.process_memory_snapshots, config_.memory_subsystem_snapshots,
			buckets_, continuation_selector_.get(), config_.working_set_monitor);
	}

	void MultiwayBlueprintTrainingSession::run_batches(
		std::uint64_t batch_count,
		MultiwayBlueprintBatchProgressCallback progress_callback,
		void* progress_context)
	{
		trainer_->run_batches(
			batch_count, config_.limits.trajectories_per_batch, config_.deterministic_seed,
			progress_callback, progress_context);
	}

	void MultiwayBlueprintTrainingSession::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot)
	{
		trainer_->resume_from_root_policy(snapshot);
	}

	void MultiwayBlueprintTrainingSession::resume_from_checkpoint(
		const MultiwayBlueprintTrainingCheckpoint& checkpoint)
	{
		trainer_->resume_from_checkpoint(checkpoint);
	}

	MultiwayBlueprintSnapshot MultiwayBlueprintTrainingSession::export_policy(
		MultiwayBlueprintPolicyKind policy_kind) const
	{
		return trainer_->publish(policy_kind);
	}

	const MultiwayBlueprintTrainingStatus& MultiwayBlueprintTrainingSession::status() const noexcept
	{
		return trainer_->status();
	}

	MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainingSession::coverage_manifest() const noexcept
	{
		return trainer_->coverage_manifest();
	}

	MultiwayFullBlueprintArtifact MultiwayBlueprintTrainingSession::export_full_policy() const
	{
		auto result = trainer_->export_full_policy();
		export_buffers_ = result.memory_snapshot();
		return result;
	}

	MultiwayFullBlueprintExportResult MultiwayBlueprintTrainingSession::save_full_policy_atomic(
		const std::filesystem::path& path) const
	{
		return trainer_->save_full_policy_atomic(path);
	}

	MultiwayTrainingCheckpointWriteResult MultiwayBlueprintTrainingSession::save_checkpoint_atomic(
		const std::filesystem::path& path) const
	{
		return trainer_->save_checkpoint_atomic(path);
	}

	MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainingSession::checkpoint() const
	{
		auto result = trainer_->checkpoint();
		checkpoint_buffers_ = result.coordinator.memory_snapshot();
		return result;
	}

	void MultiwayBlueprintTrainingSession::record_process_memory_snapshot(MultiwayMemoryPhase phase)
	{
		trainer_->record_process_memory_snapshot(
			phase,
			phase == MultiwayMemoryPhase::AfterCheckpoint ? checkpoint_buffers_ : MultiwayMemoryComponent{},
			phase == MultiwayMemoryPhase::AfterExport ? export_buffers_ : MultiwayMemoryComponent{});
		if (phase == MultiwayMemoryPhase::AfterCheckpoint)
			checkpoint_buffers_ = {};
		if (phase == MultiwayMemoryPhase::AfterExport)
			export_buffers_ = {};
	}

} // namespace texas::solver::multiway
