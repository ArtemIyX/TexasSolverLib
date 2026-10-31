#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer_detail.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{
	namespace
	{
		constexpr std::uint64_t kQuantizedProbabilityMax = std::numeric_limits<std::uint16_t>::max();

		class OptionalExportTimer final
		{
		public:
			OptionalExportTimer(bool enabled, std::uint64_t& target) noexcept
				: enabled_(enabled), target_(target), started_(enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
			~OptionalExportTimer() noexcept
			{
				if (!enabled_)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started_)
						.count());
				target_ = elapsed > std::numeric_limits<std::uint64_t>::max() - target_
					? std::numeric_limits<std::uint64_t>::max()
					: target_ + elapsed;
			}

		private:
			bool enabled_;
			std::uint64_t& target_;
			std::chrono::steady_clock::time_point started_;
		};

		MultiwayExportStorageCounters export_counter_delta(
			const MultiwayExternalStorageStats& before,
			const MultiwayExternalStorageStats& after) noexcept
		{
			return { after.lookups - before.lookups, after.cache_hits - before.cache_hits,
				after.cache_misses - before.cache_misses, after.disk_reads - before.disk_reads,
				after.read_nanoseconds - before.read_nanoseconds,
				after.page_cache_hits - before.page_cache_hits,
				after.page_cache_misses - before.page_cache_misses };
		}

		void copy_page_profile(MultiwayExternalExportProfile& target,
			const MultiwayExternalPageProfile& source) noexcept
		{
			target.page_locate_nanoseconds = source.locate_nanoseconds;
			target.page_cache_find_nanoseconds = source.cache_find_nanoseconds;
			target.page_victim_nanoseconds = source.victim_nanoseconds;
			target.page_store_lookup_nanoseconds = source.store_lookup_nanoseconds;
			target.page_decode_nanoseconds = source.decode_nanoseconds;
			target.page_zero_nanoseconds = source.zero_nanoseconds;
			target.page_decoded_count = source.decoded_pages;
			target.page_zero_count = source.zero_pages;
			target.page_index_opens = source.record_lookup.index_opens;
			target.page_index_reads = source.record_lookup.index_reads;
			target.page_run_range_skips = source.record_lookup.run_range_skips;
			target.page_membership_misses = source.record_lookup.membership_misses;
			target.page_data_opens = source.record_lookup.data_opens;
			target.page_data_reads = source.record_lookup.data_reads;
			target.page_requested_read_bytes = source.record_lookup.requested_bytes;
			target.page_index_open_nanoseconds = source.record_lookup.index_open_nanoseconds;
			target.page_index_search_nanoseconds = source.record_lookup.index_search_nanoseconds;
			target.page_data_open_nanoseconds = source.record_lookup.data_open_nanoseconds;
			target.page_data_read_nanoseconds = source.record_lookup.data_read_nanoseconds;
		}

		void copy_writer_profile(MultiwayExternalExportProfile& target,
			const MultiwayFullBlueprintWriterProfile& source) noexcept
		{
			target.writer_sampled_rows = source.sampled_rows;
			target.writer_sampled_validation_nanoseconds = source.sampled_validation_nanoseconds;
			target.writer_sampled_payload_hash_nanoseconds = source.sampled_payload_hash_nanoseconds;
			target.writer_sampled_policy_hash_nanoseconds = source.sampled_policy_hash_nanoseconds;
			target.writer_sampled_encoding_nanoseconds = source.sampled_encoding_nanoseconds;
			target.writer_flushes = source.flushes;
		}

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
				const auto visit_row = [](void* opaque, const MultiwayExternalRowBinding& binding) {
					auto& current = *static_cast<Context*>(opaque);
					MultiwaySparseRowShape shape{};
					shape.infoset = { binding.public_state(), static_cast<core::PlayerId>(binding.seat()) };
					shape.bucket_count = binding.bucket_count();
					shape.action_count = binding.action_count();
					std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
					std::size_t action_count = 0U;
					if (!current.storage->catalog().lookup_menu(shape.infoset.public_state, actions.data(), actions.size(), action_count))
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
				if (!external->compact_storage().visit_ordered_rows(visit_row, &context) || plan.shape_count != external->current_manifest().row_count || plan.action_count != external->current_manifest().logical_value_count)
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

	struct MultiwayBlueprintTrainer::ExportRunContext
	{
		struct ExportFilterGuard
		{
			const MultiwayExternalCompactStorage* storage = nullptr;
			~ExportFilterGuard()
			{
				if (storage != nullptr)
					storage->end_export_filter();
			}
		};

		ExportRunContext(MultiwaySolverCoordinator::ExportLease&& active_lease,
			std::chrono::steady_clock::time_point started)
			: export_started(started), lease(std::move(active_lease))
		{
		}

		std::chrono::steady_clock::time_point export_started{};
		MultiwaySolverCoordinator::ExportLease lease;
		MultiwayBlueprintTrainingMetadata training{};
		std::vector<std::uint32_t> order;
		MultiwayFullBlueprintExportPlan plan{};
		MultiwayFullBlueprintStreamPlan stream_plan{};
		bool sharded_output = false;
		MultiwayFullBlueprintShardLayout shard_layout{};
		std::vector<std::uint64_t> shard_action_counts;
		MultiwayFullBlueprintShardedSizeEstimate sharded_size{};
		std::uint64_t shard_temporary_peak_bytes = 0U;
		std::array<Probability, MULTIWAY_MAX_ABSTRACTED_ACTIONS> strategy{};
		std::array<MultiwayQuantizedRootAction, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
		std::uint64_t sharding_metadata_bytes = 0U;
		bool profile_enabled = false;
		bool use_export_filter = false;
		std::uint64_t filter_bytes = 0U;
		std::uint64_t profile_bytes = 0U;
		std::uint64_t peak_scratch_bytes = 0U;
		MultiwayExportTelemetry telemetry{};
		std::uint64_t disk_output_bytes = 0U;
		MultiwayProcessMemorySnapshot before_export{};
		MultiwayProcessMemorySnapshot after_export{};
		MultiwayExternalPageProfile page_profile{};
		MultiwayExternalStorageStats catalog_before{}, rows_before{}, pages_before{};
		ExportFilterGuard filter_guard;
		MultiwayFullBlueprintWriterProfile writer_profile{};
		MultiwayFullBlueprintStreamOptions stream_options{};
		std::unique_ptr<MultiwayFullBlueprintStreamWriter> writer;
		std::unique_ptr<MultiwayFullBlueprintShardedStreamWriter> sharded_writer;
		MultiwayFullBlueprintStreamResult stream_result{};
		std::uint64_t result_bytes = 0U;
		std::uint64_t result_manifest_bytes = 0U;
		std::uint64_t result_shard_count = 0U;
	};


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

	MultiwayFullBlueprintArtifact MultiwayBlueprintTrainer::export_full_policy() const
	{
		MultiwayFullBlueprintArtifact artifact;
		artifact.identity = identity_;
		artifact.training = trainer_detail::make_metadata(status_, schedule_, deterministic_seed_);
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

	void MultiwayBlueprintTrainer::prepare_full_policy_export_plan(
		ExportRunContext& context, const std::filesystem::path& path) const
	{
		auto& lease = context.lease;
		auto& training = context.training;
		auto& order = context.order;
		auto& plan = context.plan;
		auto& stream_plan = context.stream_plan;
		auto& sharded_output = context.sharded_output;
		auto& shard_layout = context.shard_layout;
		auto& shard_action_counts = context.shard_action_counts;
		auto& sharded_size = context.sharded_size;
		auto& shard_temporary_peak_bytes = context.shard_temporary_peak_bytes;
		auto& strategy = context.strategy;
		auto& actions = context.actions;
		auto& sharding_metadata_bytes = context.sharding_metadata_bytes;
		auto& profile_enabled = context.profile_enabled;
		auto& use_export_filter = context.use_export_filter;
		auto& filter_bytes = context.filter_bytes;
		auto& profile_bytes = context.profile_bytes;
		auto& peak_scratch_bytes = context.peak_scratch_bytes;
		auto& telemetry = context.telemetry;
		training = trainer_detail::make_metadata(status_, schedule_, deterministic_seed_);
		plan = make_export_plan(lease, identity_, training, order);
		stream_plan = make_multiway_full_blueprint_stream_plan(
			plan.identity, plan.training, plan.row_count, plan.action_count);
		sharded_output = plan.row_count != 0U && plan.serialized_bytes > working_set_monitor_.single_file_blueprint_limit_bytes;
		if (sharded_output)
		{
			shard_layout = make_multiway_full_blueprint_shard_layout(
				stream_plan, working_set_monitor_.shard_max_serialized_bytes);
			shard_action_counts.resize(shard_layout.ranges.size(), 0U);
			std::uint64_t next_row = 0U;
			const auto add_shape = [&](const MultiwaySparseRowShape& shape) {
				for (std::uint32_t bucket = 0U; bucket < shape.bucket_count; ++bucket)
				{
					const auto shard_index = static_cast<std::size_t>(
						next_row / shard_layout.maximum_rows_per_shard);
					shard_action_counts[shard_index] = checked_export_add(
						shard_action_counts[shard_index], shape.action_count);
					++next_row;
				}
			};
			const auto* compact_for_plan = lease.compact_storage();
			const auto* external_for_plan = lease.external_storage();
			if (external_for_plan != nullptr)
			{
				struct ShardPlanContext
				{
					const decltype(add_shape)* add_shape_callback;
				};
				ShardPlanContext shard_context{ &add_shape };
				const auto count_external_row = [](void* opaque, const MultiwayExternalRowBinding& binding) {
					auto& current = *static_cast<ShardPlanContext*>(opaque);
					MultiwaySparseRowShape shape{};
					shape.infoset = { binding.public_state(), static_cast<core::PlayerId>(binding.seat()) };
					shape.bucket_count = binding.bucket_count();
					shape.action_count = binding.action_count();
					(*current.add_shape_callback)(shape);
					return true;
				};
				if (!external_for_plan->compact_storage().visit_ordered_rows(count_external_row, &shard_context))
					throw std::runtime_error("external blueprint shard planning stopped unexpectedly");
			}
			else
			{
				const auto& export_rows = compact_for_plan != nullptr
					? compact_for_plan->rows()
					: lease.storage().rows();
				if (compact_for_plan != nullptr)
				{
					for (const auto& metadata : export_rows)
						add_shape(metadata.shape);
				}
				else
				{
					for (const auto index : order)
						add_shape(export_rows[index].shape);
				}
			}
			if (next_row != plan.row_count)
				throw std::invalid_argument("multiway blueprint shard planning row count changed");
			sharded_size = estimate_multiway_full_blueprint_sharded_size(
				stream_plan, shard_layout, path);
			for (std::size_t index = 0U; index < shard_layout.ranges.size(); ++index)
			{
				const auto range = shard_layout.ranges[index];
				const auto shard_plan = make_multiway_full_blueprint_stream_plan(
					plan.identity, plan.training, range.row_count, shard_action_counts[index]);
				shard_temporary_peak_bytes = std::max(shard_temporary_peak_bytes,
					shard_plan.serialized_bytes);
			}
		}
		sharding_metadata_bytes = sharded_output
			? checked_export_add(
				  checked_export_multiply(static_cast<std::uint64_t>(shard_layout.ranges.capacity()),
					  sizeof(MultiwayFullBlueprintShardRange)),
				  checked_export_multiply(static_cast<std::uint64_t>(shard_action_counts.capacity()),
					  sizeof(std::uint64_t)))
			: 0U;
		profile_enabled = working_set_monitor_.profile_external_export && lease.external_storage() != nullptr;
		// Includes the fixed aggregate, callback context, and nested timer frames.
		use_export_filter = lease.external_storage() != nullptr && working_set_monitor_.external_export_membership_filter;
		filter_bytes = use_export_filter
			? MultiwayExternalRecordStore::export_filter_bytes + 131072U
			: 0U;
		profile_bytes = sizeof(MultiwayExternalExportProfile) + 4096U + filter_bytes;
		peak_scratch_bytes = checked_export_add(profile_bytes, checked_export_add(checked_export_add(checked_export_add(plan.order_index_bytes, sizeof(strategy)), sizeof(actions)), sharding_metadata_bytes));
		telemetry = {};
		telemetry.external_profile.enabled = profile_enabled;
		telemetry.attempted = true;
		telemetry.sharded_output = sharded_output;
		telemetry.shard_count = sharded_output ? shard_layout.ranges.size() : 0U;
		telemetry.plan = {
			MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION,
			plan.shape_count,
			plan.row_count,
			plan.action_count,
			plan.serialized_bytes,
			plan.order_index_bytes,
			plan.compact_direct_order
		};
		telemetry.plan.sharding_metadata_bytes = sharding_metadata_bytes;

	}

	void MultiwayBlueprintTrainer::enforce_full_policy_export_gates(
		ExportRunContext& context, const std::filesystem::path& path) const
	{
		auto& lease = context.lease;
		auto& plan = context.plan;
		auto& sharded_output = context.sharded_output;
		auto& sharded_size = context.sharded_size;
		auto& shard_temporary_peak_bytes = context.shard_temporary_peak_bytes;
		auto& profile_bytes = context.profile_bytes;
		auto& telemetry = context.telemetry;
		auto& disk_output_bytes = context.disk_output_bytes;
		auto& before_export = context.before_export;
		std::uint64_t available_disk_bytes = 0U;
		bool disk_space_available = false;
		std::error_code directory_error;
		const auto output_directory = path.parent_path().empty()
			? std::filesystem::current_path(directory_error)
			: path.parent_path();
		if (!directory_error)
		{
			if (working_set_monitor_.export_disk_space_observer != nullptr)
			{
				disk_space_available = working_set_monitor_.export_disk_space_observer(
					working_set_monitor_.export_observer_context,
					output_directory,
					available_disk_bytes);
			}
			else
			{
				std::error_code disk_error;
				if (std::filesystem::is_directory(output_directory, disk_error) && !disk_error)
				{
					const auto space = std::filesystem::space(output_directory, disk_error);
					if (!disk_error)
					{
						available_disk_bytes = space.available;
						disk_space_available = true;
					}
				}
			}
		}
		const auto external_disk_reserve = coordinator_->limits().storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32
			? coordinator_->limits().external_storage.disk_reserve_bytes
			: 0U;
		disk_output_bytes = sharded_output
			? sharded_size.total_bytes
			: plan.serialized_bytes;
		telemetry.manifest_bytes = sharded_output ? sharded_size.manifest_bytes : 0U;
		telemetry.disk_gate = evaluate_multiway_export_disk_gate(
			disk_output_bytes, sharded_output ? shard_temporary_peak_bytes : disk_output_bytes,
			external_disk_reserve, disk_space_available, available_disk_bytes);
		if (telemetry.disk_gate.blocked())
		{
			status_.failed = true;
			status_.export_telemetry = telemetry;
			throw std::runtime_error("multiway blueprint export disk preflight rejected");
		}
		auto export_gate = working_set_monitor_.export_gate;
		export_gate.process.exporter_headroom_bytes = checked_export_add(
			export_gate.process.exporter_headroom_bytes, profile_bytes);
		if (export_gate.process.mapped_residency_allowance_bytes == 0U)
			export_gate.process.mapped_residency_allowance_bytes = status_.memory_ledger.mapped_resident_allowance_bytes;
		telemetry.scratch = make_multiway_export_scratch_estimate(telemetry.plan, export_gate.process);
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
		before_export = working_set_monitor_.export_process_observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.export_process_observer(working_set_monitor_.export_observer_context);
		telemetry.process_estimate = make_multiway_export_process_estimate(
			telemetry.plan, before_export, export_gate.process);
		status_.memory_ledger.export_scratch_bytes = telemetry.scratch.total_bytes;
		status_.memory_ledger.export_output_bytes = disk_output_bytes;
		status_.memory_ledger.export_process_peak_bytes = telemetry.process_estimate.estimated_process_export_peak_bytes;
		record_export_memory_snapshot(MultiwayMemoryPhase::BeforeExport, before_export, export_buffer_component(context));
		telemetry.process_gate = evaluate_multiway_export_process_gate(
			telemetry.plan, before_export, export_gate.process);
		auto system_cache_plan = telemetry.plan;
		system_cache_plan.serialized_bytes = disk_output_bytes;
		telemetry.system_gate = evaluate_multiway_export_system_gate(
			system_cache_plan, telemetry.scratch, telemetry.system_before, export_gate.system);
		status_.memory_ledger.export_system_cache_allowance_bytes = disk_output_bytes;
		if (telemetry.process_gate.blocked() || telemetry.system_gate.blocked())
		{
			status_.failed = true;
			status_.export_telemetry = telemetry;
			const auto reason = telemetry.process_gate.blocked()
				? multiway_export_process_rejection_reason_name(telemetry.process_gate.rejection_reason)
				: multiway_export_system_rejection_reason_name(telemetry.system_gate.rejection_reason);
			throw std::runtime_error(std::string("multiway blueprint export memory gate rejected: ") + reason);
		}

	}

	MultiwayMemoryComponent MultiwayBlueprintTrainer::export_buffer_component(
		const ExportRunContext& context) const noexcept
	{
		MultiwayMemoryComponent component;
		component.add_buffer(1U, 1U, static_cast<std::size_t>(context.peak_scratch_bytes));
		return component;
	}

	void MultiwayBlueprintTrainer::setup_full_policy_export_writer(
		ExportRunContext& context, const std::filesystem::path& path) const
	{
		auto& lease = context.lease;
		if (context.use_export_filter)
		{
			const auto& storage = lease.external_storage()->compact_storage();
			storage.begin_export_filter();
			context.filter_guard.storage = &storage;
		}
		context.stream_options.profile = context.profile_enabled ? &context.writer_profile : nullptr;
		if (context.sharded_output)
			context.sharded_writer = std::make_unique<MultiwayFullBlueprintShardedStreamWriter>(
				path, context.stream_plan, std::move(context.shard_layout),
				std::move(context.shard_action_counts), context.stream_options);
		else
			context.writer = std::make_unique<MultiwayFullBlueprintStreamWriter>(
				path, context.stream_plan, context.stream_options);
	}

	void MultiwayBlueprintTrainer::write_full_policy_export_rows(ExportRunContext& context) const
	{
		if (context.lease.external_storage() != nullptr)
			write_external_full_policy_export_rows(context);
		else
			write_memory_full_policy_export_rows(context);
	}

	void MultiwayBlueprintTrainer::write_external_full_policy_export_rows(ExportRunContext& context) const
	{
		auto& lease = context.lease;
		const auto* external = lease.external_storage();
		const auto profile_enabled = context.profile_enabled;
		auto& writer = context.writer;
		auto& sharded_writer = context.sharded_writer;
		auto& strategy = context.strategy;
		auto& actions = context.actions;
		auto& telemetry = context.telemetry;
		auto& page_profile = context.page_profile;
		auto& writer_profile = context.writer_profile;
		auto& catalog_before = context.catalog_before;
		auto& rows_before = context.rows_before;
		auto& pages_before = context.pages_before;
		if (external != nullptr)
		{
			if (profile_enabled)
			{
				catalog_before = external->catalog().stats();
				rows_before = external->compact_storage().row_index_stats();
				pages_before = external->compact_storage().page_store_stats();
			}
			struct ExternalRowWriteContext
			{
				const MultiwayExternalTrainingStorage* storage;
				MultiwayFullBlueprintStreamWriter* writer;
				MultiwayFullBlueprintShardedStreamWriter* sharded_writer;
				Probability* strategy;
				MultiwayQuantizedRootAction* actions;
				MultiwayExternalExportProfile* profile;
				MultiwayExternalPageProfile* page_profile;
				MultiwayFullBlueprintWriterProfile* writer_profile;
				MultiwayExternalExportProgressObserver progress;
				void* progress_context;
				std::uint64_t progress_interval;
				std::chrono::steady_clock::time_point stream_started;
				std::uint64_t row_sequence = 0U;
			};
			ExternalRowWriteContext external_context{ external, writer.get(), sharded_writer.get(), strategy.data(), actions.data(),
				&telemetry.external_profile, profile_enabled ? &page_profile : nullptr,
				profile_enabled ? &writer_profile : nullptr,
				working_set_monitor_.export_progress_observer,
				working_set_monitor_.export_progress_context, working_set_monitor_.export_progress_page_interval,
				profile_enabled && working_set_monitor_.export_progress_observer != nullptr && working_set_monitor_.export_progress_page_interval != 0U
					? std::chrono::steady_clock::now()
					: std::chrono::steady_clock::time_point{} };
			const auto write_external_row = [](void* opaque, const MultiwayExternalRowBinding& binding) {
				auto& current = *static_cast<ExternalRowWriteContext*>(opaque);
				auto& profile = *current.profile;
				OptionalExportTimer visitor_timer(profile.enabled, profile.visitor_nanoseconds);
				MultiwaySparseRowShape shape{};
				shape.infoset = { binding.public_state(), static_cast<core::PlayerId>(binding.seat()) };
				shape.bucket_count = binding.bucket_count();
				shape.action_count = binding.action_count();
				std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> menu_actions{};
				std::size_t action_count = 0U;
				{
					OptionalExportTimer menu_timer(profile.enabled, profile.menu_nanoseconds);
					if (!current.storage->catalog().lookup_menu(shape.infoset.public_state, menu_actions.data(),
							menu_actions.size(), action_count))
						throw std::invalid_argument("external blueprint row has no published action menu");
				}
				const MultiwayPackedMenuView menu{ menu_actions.data(),
					static_cast<std::uint8_t>(action_count), menu_actions[0].action_menu_id };
				validate_export_menu(shape, menu);
				std::uint32_t first_bucket = 0U;
				while (first_bucket < shape.bucket_count)
				{
					auto pinned = [&]() {
						OptionalExportTimer pin_timer(profile.enabled, profile.pin_nanoseconds);
						return current.storage->compact_storage().pin_bucket_profiled(
							binding, first_bucket, false, current.page_profile);
					}();
					try
					{
						const auto page_bucket_count = pinned.page.bucket_count;
						if (pinned.local_bucket != 0U || page_bucket_count == 0U || page_bucket_count > shape.bucket_count - first_bucket)
							throw std::runtime_error("external blueprint page bucket range is invalid");
						for (std::uint32_t local_bucket = 0U; local_bucket < page_bucket_count; ++local_bucket)
						{
							const auto bucket = first_bucket + local_bucket;
							pinned.local_bucket = local_bucket;
							const bool sample = profile.enabled && ((current.row_sequence++ & 255U) == 0U);
							if (sample)
								++profile.sampled_rows;
							if (sample)
							{
								OptionalExportTimer timer(sample, profile.sampled_average_nanoseconds);
								current.storage->compact_storage().average_strategy_into(
									pinned, current.strategy, shape.action_count);
							}
							else
								current.storage->compact_storage().average_strategy_into(
									pinned, current.strategy, shape.action_count);
							if (sample)
							{
								OptionalExportTimer timer(sample, profile.sampled_quantize_nanoseconds);
								quantize_compact_strategy(current.strategy, shape.action_count, menu,
									current.actions);
							}
							else
								quantize_compact_strategy(current.strategy, shape.action_count, menu,
									current.actions);
							MultiwayFullBlueprintRowView row;
							row.infoset = shape.infoset;
							row.bucket = bucket;
							row.action_menu_id = menu.action_menu_id;
							row.actions = current.actions;
							row.action_count = shape.action_count;
							const auto write = [&]() {
								if (current.sharded_writer != nullptr)
									current.sharded_writer->write_row(row);
								else
									current.writer->write_row(row);
							};
							if (sample)
							{
								OptionalExportTimer timer(sample, profile.sampled_writer_nanoseconds);
								write();
							}
							else
								write();
						}
						first_bucket += page_bucket_count;
						if (profile.enabled)
						{
							++profile.pages;
							profile.rows += page_bucket_count;
							profile.actions += static_cast<std::uint64_t>(page_bucket_count) * shape.action_count;
						}
					}
					catch (...)
					{
						current.storage->compact_storage().unpin_bucket(pinned);
						throw;
					}
					{
						OptionalExportTimer unpin_timer(profile.enabled, profile.unpin_nanoseconds);
						current.storage->compact_storage().unpin_bucket(pinned);
					}
					if (profile.enabled && current.progress != nullptr && current.progress_interval != 0U && profile.pages % current.progress_interval == 0U)
					{
						copy_page_profile(profile, *current.page_profile);
						copy_writer_profile(profile, *current.writer_profile);
						const auto bytes = current.sharded_writer != nullptr
							? current.sharded_writer->progress()
							: current.writer->progress();
						profile.encoded_bytes = bytes.encoded_bytes;
						profile.stream_write_bytes = bytes.stream_write_bytes;
						const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - current.stream_started)
								.count());
						current.progress(profile, elapsed, current.progress_context);
					}
				}
				if (profile.enabled)
					++profile.shapes;
				return true;
			};
			{
				OptionalExportTimer visit_timer(telemetry.external_profile.enabled,
					telemetry.external_profile.ordered_visit_nanoseconds);
				if (!external->compact_storage().visit_ordered_rows(write_external_row, &external_context))
					throw std::runtime_error("external blueprint ordered export stopped unexpectedly");
			}
			if (profile_enabled)
			{
				copy_page_profile(telemetry.external_profile, page_profile);
				const auto bytes = sharded_writer != nullptr ? sharded_writer->progress() : writer->progress();
				telemetry.external_profile.encoded_bytes = bytes.encoded_bytes;
				telemetry.external_profile.stream_write_bytes = bytes.stream_write_bytes;
				telemetry.external_profile.catalog = export_counter_delta(
					catalog_before, external->catalog().stats());
				telemetry.external_profile.row_index = export_counter_delta(
					rows_before, external->compact_storage().row_index_stats());
				telemetry.external_profile.page_store = export_counter_delta(
					pages_before, external->compact_storage().page_store_stats());
			}
		}

	}

	void MultiwayBlueprintTrainer::write_memory_full_policy_export_rows(ExportRunContext& context) const
	{
		auto& lease = context.lease;
		const auto sharded_output = context.sharded_output;
		auto& writer = context.writer;
		auto& sharded_writer = context.sharded_writer;
		auto& strategy = context.strategy;
		auto& actions = context.actions;
		auto& order = context.order;
		const auto write_row = [&](const MultiwayFullBlueprintRowView& row) {
			if (sharded_output)
				sharded_writer->write_row(row);
			else
				writer->write_row(row);
		};
		const auto* compact = lease.compact_storage();
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
				write_row(row);
			}
		};
		if (compact != nullptr)
		{
			for (const auto& metadata : rows)
				emit_shape(metadata.shape);
		}
		else
		{
			for (const auto index : order)
				emit_shape(rows[index].shape);
		}
	}

	MultiwayFullBlueprintExportResult MultiwayBlueprintTrainer::finish_full_policy_export(ExportRunContext& context) const
	{
		auto& telemetry = context.telemetry;
		auto& disk_output_bytes = context.disk_output_bytes;
		auto& stream_result = context.stream_result;
		auto& result_bytes = context.result_bytes;
		auto& result_manifest_bytes = context.result_manifest_bytes;
		auto& result_shard_count = context.result_shard_count;
		auto& writer = context.writer;
		auto& sharded_writer = context.sharded_writer;
		const auto profile_enabled = context.profile_enabled;
		auto& writer_profile = context.writer_profile;
		auto& before_export = context.before_export;
		auto& after_export = context.after_export;
		const auto export_started = context.export_started;
		const auto sharded_output = context.sharded_output;
		const auto& plan = context.plan;
		const auto peak_scratch_bytes = context.peak_scratch_bytes;
		{
			OptionalExportTimer finish_timer(telemetry.external_profile.enabled,
				telemetry.external_profile.finish_nanoseconds);
			if (sharded_output)
			{
				const auto shard_result = sharded_writer->finish();
				stream_result.row_count = shard_result.row_count;
				stream_result.action_count = shard_result.action_count;
				stream_result.serialized_bytes = shard_result.serialized_bytes;
				stream_result.policy_content_hash = shard_result.policy_content_hash;
				stream_result.publication_mode = shard_result.publication_mode;
				result_bytes = shard_result.serialized_bytes;
				result_manifest_bytes = shard_result.manifest_bytes;
				result_shard_count = shard_result.shard_count;
			}
			else
			{
				stream_result = writer->finish();
				result_bytes = stream_result.serialized_bytes;
			}
		}
		if (profile_enabled)
		{
			copy_writer_profile(telemetry.external_profile, writer_profile);
		}
		after_export = working_set_monitor_.export_process_observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.export_process_observer(working_set_monitor_.export_observer_context);
		telemetry.system_after = working_set_monitor_.export_system_observer == nullptr
			? capture_multiway_system_memory_snapshot()
			: working_set_monitor_.export_system_observer(working_set_monitor_.export_observer_context);
		telemetry.observed_delta = observe_multiway_export_memory_delta(before_export, after_export);
		status_.memory_ledger.export_system_cache_allowance_bytes = disk_output_bytes;
		status_.memory_ledger.export_observed_before_working_set_bytes = telemetry.observed_delta.before_working_set_bytes;
		status_.memory_ledger.export_observed_after_working_set_bytes = telemetry.observed_delta.after_working_set_bytes;
		telemetry.result_bytes = result_bytes;
		telemetry.payload_hash = stream_result.payload_hash;
		telemetry.policy_content_hash = stream_result.policy_content_hash;
		telemetry.duration_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - export_started)
				.count());
		telemetry.publication_mode = stream_result.publication_mode;
		telemetry.sharded_output = sharded_output;
		telemetry.shard_count = result_shard_count;
		telemetry.manifest_bytes = result_manifest_bytes;
		telemetry.succeeded = true;
		status_.export_telemetry = telemetry;
		record_export_memory_snapshot(MultiwayMemoryPhase::AfterExport, after_export, export_buffer_component(context));
		MultiwayFullBlueprintExportResult result;
		result.plan = plan;
		result.payload_hash = stream_result.payload_hash;
		result.policy_content_hash = stream_result.policy_content_hash;
		result.peak_scratch_bytes = peak_scratch_bytes;
		result.serialized_bytes = result_bytes;
		result.shard_count = result_shard_count;
		result.manifest_bytes = result_manifest_bytes;
		result.sharded = sharded_output;
		result.publication_mode = stream_result.publication_mode;
		return result;
	}

	MultiwayFullBlueprintExportResult MultiwayBlueprintTrainer::save_full_policy_atomic(
		const std::filesystem::path& path) const
	{
		const auto export_started = std::chrono::steady_clock::now();
		trainer_detail::MultiwayDurationRecorder export_duration(status_.export_wall_nanoseconds);
		ExportRunContext context(coordinator_->acquire_export_lease(), export_started);
		prepare_full_policy_export_plan(context, path);
		enforce_full_policy_export_gates(context, path);
		setup_full_policy_export_writer(context, path);
		write_full_policy_export_rows(context);
		return finish_full_policy_export(context);
	}

	MultiwayBlueprintSnapshot MultiwayBlueprintTrainingSession::export_policy(
		MultiwayBlueprintPolicyKind policy_kind) const
	{
		return trainer_->publish(policy_kind);
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
}
