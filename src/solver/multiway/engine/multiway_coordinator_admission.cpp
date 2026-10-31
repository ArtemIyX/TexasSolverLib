#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_solver_detail.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	void MultiwaySolverCoordinator::validate_prepared_batch_inputs(
		const std::vector<MultiwayPublicStateDescriptor>& public_states,
		const std::vector<std::size_t>& parent_order,
		const std::vector<MultiwaySparseRowShape>& rows,
		const std::vector<std::size_t>* parent_indexes) const
	{
		if (parent_order.size() != public_states.size())
			throw std::invalid_argument("multiway prepared admission order does not match public states");
		if (parent_indexes != nullptr && parent_indexes->size() != public_states.size())
			throw std::invalid_argument("multiway prepared parent indexes do not match public states");
		if (!std::is_sorted(public_states.begin(), public_states.end(), [](const auto& left, const auto& right) {
				return left.id < right.id;
			}))
			throw std::invalid_argument("multiway prepared public states must be sorted by id");
		for (std::size_t index = 1U; index < public_states.size(); ++index)
		{
			if (public_states[index - 1U].id == public_states[index].id)
				throw std::invalid_argument("multiway prepared public states must be unique");
		}
		if (!std::is_sorted(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
				return left.infoset < right.infoset;
			}))
			throw std::invalid_argument("multiway prepared rows must be sorted by infoset");
		for (std::size_t index = 1U; index < rows.size(); ++index)
		{
			if (rows[index - 1U].infoset == rows[index].infoset)
				throw std::invalid_argument("multiway prepared rows must be unique");
		}
	}

	std::size_t MultiwaySolverCoordinator::preflight_prepared_states(
		const std::vector<MultiwayPublicStateDescriptor>& public_states,
		const std::vector<std::size_t>& parent_order,
		const std::vector<MultiwaySparseRowShape>& rows,
		const std::vector<std::size_t>* parent_indexes)
	{
		const auto state_preflight_started = std::chrono::steady_clock::now();
		admission_processed_states_.assign(public_states.size(), 0U);
		admission_new_states_.assign(public_states.size(), 0U);
		admission_existing_nodes_.assign(public_states.size(), nullptr);
		admission_state_fixed_states_.assign(public_states.size(), MultiwayFixedState{});
		admission_new_rows_.assign(rows.size(), 0U);
		admission_new_row_streets_.assign(rows.size(), 0U);
		admission_row_state_indexes_.assign(rows.size(), public_states.size());
		admission_state_menu_keys_.assign(public_states.size(), 0U);
		admission_state_action_counts_.assign(public_states.size(), 0U);
		admission_state_streets_.assign(public_states.size(), 0U);
		admission_state_current_players_.assign(public_states.size(), -1);
		admission_state_next_node_kinds_.assign(public_states.size(), 0U);
		admission_state_menu_handles_.assign(public_states.size(), std::numeric_limits<std::uint32_t>::max());
		auto& processed_states = admission_processed_states_;
		const auto record_state_metadata = [this](std::size_t state_index, const PublicStateValidationMetadata& metadata) {
			admission_state_menu_keys_[state_index] = metadata.canonical_menu_id;
			admission_state_action_counts_[state_index] = metadata.action_count;
			admission_state_streets_[state_index] = metadata.street;
			admission_state_current_players_[state_index] = metadata.current_player;
			admission_state_next_node_kinds_[state_index] = static_cast<std::uint8_t>(metadata.next_node_kind);
		};
		const auto classification_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.classify_public_states");
			if (external_storage_ != nullptr)
				external_storage_->catalog().classify_batch(public_states, admission_new_states_);
			else
				public_catalog_.classify_new_states(public_states, admission_new_states_, admission_existing_nodes_);
		}
		if (admission_diagnostics_ != nullptr)
			admission_diagnostics_->state_catalog_classification_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - classification_started)
					.count());
		const auto& new_states = admission_new_states_;
		std::size_t new_state_count = 0U;
		const auto find_candidate = [&public_states](MultiwayPublicStateId id) {
			const auto it = std::lower_bound(public_states.begin(), public_states.end(), id,
				[](const auto& candidate, MultiwayPublicStateId value) { return candidate.id < value; });
			return it == public_states.end() || it->id != id
				? public_states.size()
				: static_cast<std::size_t>(it - public_states.begin());
		};
		for (const auto state_index : parent_order)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admission.state");
			if (state_index >= public_states.size() || processed_states[state_index] != 0U)
				throw std::invalid_argument("multiway prepared admission order is not a permutation");
			const auto& state = public_states[state_index];
			if (new_states[state_index] == 0U)
			{
				if (admission_diagnostics_ != nullptr)
				{
					++admission_diagnostics_->existing_public_state_checks;
					++admission_diagnostics_->packed_public_state_compatibility_checks;
				}
				MultiwayPublicStateDescriptor existing;
				const bool compatible = external_storage_ != nullptr
					? external_storage_->catalog().reconstruct(state.id, existing) && public_state_candidates_compatible(existing, state)
					: public_catalog_.compatible_with_descriptor(admission_existing_nodes_[state_index], state);
				if (!compatible)
				{
					if (external_storage_ == nullptr && !public_catalog_.reconstruct(state.id, existing))
						throw std::logic_error("multiway public state conflict could not reconstruct catalog state");
					throw_conflicting_public_state_descriptors(existing, state);
				}
				if (external_storage_ == nullptr && admission_existing_nodes_[state_index] == nullptr)
					throw std::logic_error("multiway public state classification lost its catalog node");
				admission_state_fixed_states_[state_index] = make_multiway_fixed_state(state.betting);
				record_state_metadata(state_index, PublicStateValidationMetadata{ state.legal_actions.empty() ? 0U : state.legal_actions.front().action_menu_id, static_cast<std::uint8_t>(state.legal_actions.size()), static_cast<std::uint8_t>(state.betting.street), state.betting.current_player, admission_state_fixed_states_[state_index].next_node_kind() });
				admission_processed_states_[state_index] = 1U;
				continue;
			}

			if (admission_diagnostics_ != nullptr)
				++admission_diagnostics_->new_public_state_validations;
			const auto validation_started = std::chrono::steady_clock::now();
			PublicStateValidationMetadata state_metadata;
			validate_public_state_descriptor(
				state,
				&state_metadata,
				admission_diagnostics_,
				&admission_state_fixed_states_[state_index]);
			record_state_metadata(state_index, state_metadata);
			if (admission_diagnostics_ != nullptr)
				admission_diagnostics_->public_state_validation_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - validation_started)
						.count());
			const auto catalog_size = external_storage_ != nullptr
				? static_cast<std::size_t>(external_storage_->current_manifest().public_state_count)
				: public_catalog_.size();
			if (catalog_size >= request_.limits().max_public_states || new_state_count >= request_.limits().max_public_states - catalog_size)
				throw std::length_error("multiway public-state admission exceeds configured capacity");
			if (state.parent_id.value != 0U)
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admission.parent");
				const auto parent_index = parent_indexes == nullptr
					? find_candidate(state.parent_id)
					: (*parent_indexes)[state_index];
				MultiwayPublicStateDescriptor existing_parent;
				const MultiwayPublicStateDescriptor* parent = nullptr;
				const MultiwayFixedState* parent_fixed_state = nullptr;
				if (parent_index != public_states.size())
				{
					if (parent_index >= public_states.size() || public_states[parent_index].id != state.parent_id)
						throw std::invalid_argument("multiway prepared parent index does not match public state");
					if (processed_states[parent_index] == 0U)
						throw std::invalid_argument("multiway prepared admission order is not parent-first");
					parent = &public_states[parent_index];
					parent_fixed_state = &admission_state_fixed_states_[parent_index];
				}
				else
				{
					const auto found_parent = external_storage_ != nullptr
						? external_storage_->catalog().reconstruct(state.parent_id, existing_parent)
						: public_catalog_.reconstruct(state.parent_id, existing_parent);
					if (found_parent)
					{
						if (admission_diagnostics_ != nullptr)
							++admission_diagnostics_->parent_catalog_resolutions;
						parent = &existing_parent;
					}
				}
				if (parent == nullptr)
					throw std::invalid_argument("multiway public state parent must be admitted first");
				const auto parent_validation_started = std::chrono::steady_clock::now();
				validate_public_state_child_transition(request_.root(), *parent, state, admission_diagnostics_, parent_fixed_state);
				if (admission_diagnostics_ != nullptr)
					admission_diagnostics_->parent_transition_validation_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - parent_validation_started)
							.count());
			}
			else if (state.id != request_.root().public_state.id)
			{
				throw std::invalid_argument("multiway coordinator admits only its immutable root without a parent edge");
			}
			processed_states[state_index] = 1U;
			admission_new_states_[state_index] = 1U;
			++new_state_count;
		}
		if (admission_diagnostics_ != nullptr)
			admission_diagnostics_->state_preflight_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - state_preflight_started)
					.count());

		return new_state_count;
	}

	MultiwaySolverCoordinator::PreparedAdmissionSummary MultiwaySolverCoordinator::preflight_prepared_rows(
		const std::vector<MultiwayPublicStateDescriptor>& public_states,
		const std::vector<MultiwaySparseRowShape>& rows,
		std::size_t new_state_count)
	{
		const auto row_preflight_started = std::chrono::steady_clock::now();
		const auto compact = compact_storage_ != nullptr;
		const auto external = external_storage_ != nullptr;
		const auto current_row_count = external
			? static_cast<std::size_t>(external_storage_->current_manifest().row_count)
			: (compact ? compact_storage_->row_count() : storage_.row_count());
		const auto& new_rows = admission_new_rows_;
		const auto& new_row_streets = admission_new_row_streets_;
		auto& row_state_indexes = admission_row_state_indexes_;
		for (std::size_t row_index = 0U; row_index < rows.size();)
		{
			const auto seat = rows[row_index].infoset.seat;
			std::size_t state_index = 0U;
			while (row_index < rows.size() && rows[row_index].infoset.seat == seat)
			{
				while (state_index < public_states.size() && public_states[state_index].id < rows[row_index].infoset.public_state)
					++state_index;
				if (state_index < public_states.size() && public_states[state_index].id == rows[row_index].infoset.public_state)
					row_state_indexes[row_index] = state_index;
				++row_index;
			}
		}
		std::vector<MultiwayExternalRowMetadata> external_existing_rows;
		if (external)
			external_storage_->compact_storage().lookup_rows(rows, external_existing_rows);
		std::size_t new_row_count = 0U;
		std::size_t new_value_count = 0U;
		for (std::size_t row_index = 0U; row_index < rows.size(); ++row_index)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admission.row");
			const auto& shape = rows[row_index];
			const auto state_index = row_state_indexes[row_index];
			const auto row_state_started = std::chrono::steady_clock::now();
			MultiwayPublicStateDescriptor existing_state;
			const MultiwayPublicStateDescriptor* state = nullptr;
			if (state_index != public_states.size())
			{
				if (admission_diagnostics_ != nullptr)
					++admission_diagnostics_->row_batch_state_resolutions;
				state = &public_states[state_index];
			}
			else if ((external
							 ? external_storage_->catalog().reconstruct(shape.infoset.public_state, existing_state)
							 : public_catalog_.node(shape.infoset.public_state) != nullptr && public_catalog_.reconstruct(shape.infoset.public_state, existing_state)))
			{
				if (admission_diagnostics_ != nullptr)
				{
					++admission_diagnostics_->row_catalog_state_resolutions;
					++admission_diagnostics_->descriptor_reconstructions;
				}
				state = &existing_state;
			}
			if (admission_diagnostics_ != nullptr)
				admission_diagnostics_->row_state_resolution_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - row_state_started)
						.count());
			if (state == nullptr || shape.infoset.seat < 0 || static_cast<std::size_t>(shape.infoset.seat) >= request_.root().seat_order.size())
				throw std::invalid_argument("multiway row must belong to an admitted per-seat public infoset");
			const auto row_shape_started = std::chrono::steady_clock::now();
			MultiwayNextNodeKind next_node_kind = MultiwayNextNodeKind::BettingDecision;
			std::uint8_t action_count = 0U;
			std::uint8_t street = 0U;
			core::PlayerId current_player = -1;
			if (state_index != public_states.size())
			{
				next_node_kind = static_cast<MultiwayNextNodeKind>(admission_state_next_node_kinds_[state_index]);
				action_count = admission_state_action_counts_[state_index];
				street = admission_state_streets_[state_index];
				current_player = admission_state_current_players_[state_index];
			}
			else
			{
				const auto betting_state = make_multiway_fixed_state(state->betting);
				next_node_kind = betting_state.next_node_kind();
				action_count = static_cast<std::uint8_t>(state->legal_actions.size());
				street = static_cast<std::uint8_t>(state->betting.street);
				current_player = state->betting.current_player;
			}
			if (next_node_kind != MultiwayNextNodeKind::BettingDecision || shape.infoset.seat != current_player)
				throw std::invalid_argument("multiway row must belong to its public decision owner");
			if (shape.action_count != action_count)
				throw std::invalid_argument("multiway row action count must match its public action menu");
			if (shape.infoset == request_.root().root_infoset && request_.root().root_bucket >= shape.bucket_count)
				throw std::invalid_argument("multiway root bucket must fit its admitted sparse row");
			const auto* existing = compact ? compact_storage_->metadata(shape.infoset) : storage_.metadata(shape.infoset);
			const auto external_existing = external
				? external_existing_rows[row_index]
				: MultiwayExternalRowMetadata{};
			const bool external_row_exists = external && external_existing.action_count != 0U;
			if (existing != nullptr || external_row_exists)
			{
				const auto existing_buckets = external_row_exists ? external_existing.bucket_count : existing->shape.bucket_count;
				const auto existing_actions = external_row_exists ? external_existing.action_count : existing->shape.action_count;
				if (existing_buckets != shape.bucket_count || existing_actions != shape.action_count)
					throw std::invalid_argument("multiway sparse row shape conflicts");
				continue;
			}
			admission_new_rows_[row_index] = 1U;
			admission_new_row_streets_[row_index] = street;
			++new_row_count;
			const auto values = compact
				? static_cast<std::size_t>(shape.action_count) * std::min<std::uint32_t>(shape.bucket_count, compact_storage_->page_width())
				: checked_value_count(shape);
			if (new_value_count > std::numeric_limits<std::size_t>::max() - values)
				throw std::overflow_error("multiway prepared row value count overflows size_t");
			new_value_count += values;
			if (admission_diagnostics_ != nullptr)
				admission_diagnostics_->row_shape_validation_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - row_shape_started)
						.count());
		}
		if (current_row_count > request_.limits().max_sparse_rows || new_row_count > request_.limits().max_sparse_rows - current_row_count)
			throw std::length_error("multiway sparse row admission exceeds configured capacity");
		const auto current_value_count = external
			? static_cast<std::size_t>(external_storage_->current_manifest().logical_value_count)
			: (compact ? compact_storage_->value_count() : storage_.value_count());
		const auto value_capacity = external
			? request_.limits().max_sparse_values
			: (compact ? compact_storage_->configured_cell_capacity() : storage_.value_capacity_limit());
		if (current_value_count > value_capacity || new_value_count > value_capacity - current_value_count)
			throw std::length_error("multiway prepared row admission exceeds configured value capacity");
		if (admission_diagnostics_ != nullptr)
			admission_diagnostics_->row_preflight_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - row_preflight_started)
					.count());
		PreparedAdmissionSummary summary;
		summary.new_state_count = new_state_count;
		summary.new_row_count = new_row_count;
		summary.new_value_count = new_value_count;
		summary.current_row_count = current_row_count;
		summary.current_value_count = current_value_count;
		summary.compact = compact;
		summary.external = external;
		return summary;
	}

	void MultiwaySolverCoordinator::commit_prepared_batch(
		const std::vector<MultiwayPublicStateDescriptor>& public_states,
		const std::vector<MultiwaySparseRowShape>& rows,
		const PreparedAdmissionSummary& summary)
	{
		const auto new_state_count = summary.new_state_count;
		const auto new_row_count = summary.new_row_count;
		const auto new_value_count = summary.new_value_count;
		const auto current_row_count = summary.current_row_count;
		const auto current_value_count = summary.current_value_count;
		const auto compact = summary.compact;
		const auto external = summary.external;
		const auto& new_states = admission_new_states_;
		const auto& new_rows = admission_new_rows_;
		const auto& new_row_streets = admission_new_row_streets_;
		if (external && new_state_count == 0U && new_row_count == 0U)
			return;
		const auto catalog_commit_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.catalog_commit");
			if (!external)
				public_catalog_.admit_batch(
					public_states,
					new_states,
					admission_diagnostics_ == nullptr ? nullptr : &admission_diagnostics_->catalog_index_insertion_nanoseconds,
					admission_diagnostics_ == nullptr ? nullptr : &admission_diagnostics_->catalog_menu_lookup_nanoseconds,
					admission_diagnostics_ == nullptr ? nullptr : &admission_diagnostics_->catalog_index_merge_nanoseconds,
					admission_diagnostics_,
					&admission_state_menu_keys_,
					&admission_state_menu_handles_);
		}
		diagnostics_.public_states_admitted += static_cast<std::uint64_t>(new_state_count);
		if (admission_diagnostics_ != nullptr)
			admission_diagnostics_->catalog_commit_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - catalog_commit_started)
					.count());

		const auto storage_commit_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.storage_commit");
			if (external)
			{
				const auto batch_number = external_storage_->next_batch_number();
				const auto fingerprint = admission_batch_fingerprint(
					batch_number, public_states, new_states, rows);
				external_storage_->begin_batch(batch_number);
				try
				{
					if (new_state_count != 0U)
						external_storage_->append_discovered_catalog(fingerprint, public_states, new_states);
					std::vector<MultiwaySparseRowShape> admitted_rows;
					admitted_rows.reserve(new_row_count);
					for (std::size_t row_index = 0U; row_index < rows.size(); ++row_index)
						if (new_rows[row_index] != 0U)
							admitted_rows.push_back(rows[row_index]);
					if (!admitted_rows.empty())
						external_storage_->admit_rows(fingerprint, admitted_rows);
					external_storage_->compact_components();
					external_storage_->begin_page_batch();
					const auto next_row_count = static_cast<std::uint64_t>(current_row_count) + new_row_count;
					const auto next_value_count = static_cast<std::uint64_t>(current_value_count) + new_value_count;
					external_storage_->commit_batch(fingerprint, next_row_count, next_value_count);
				}
				catch (...)
				{
					try
					{
						external_storage_->abort_batch();
					}
					catch (...)
					{
					}
					throw;
				}
			}
			else if (compact)
				compact_storage_->admit_rows(rows, new_rows);
			else
				storage_.admit_rows(rows, new_rows);
		}
		const auto storage_commit_elapsed = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - storage_commit_started)
				.count());
		if (admission_diagnostics_ != nullptr)
		{
			admission_diagnostics_->storage_commit_nanoseconds += storage_commit_elapsed;
			admission_diagnostics_->row_admission_nanoseconds += storage_commit_elapsed;
		}
		diagnostics_.sparse_rows_admitted += static_cast<std::uint64_t>(new_row_count);
		for (std::size_t row_index = 0U; row_index < rows.size(); ++row_index)
		{
			if (new_rows[row_index] == 0U)
				continue;
			switch (static_cast<Street>(new_row_streets[row_index]))
			{
				case Street::Preflop:
					++diagnostics_.preflop_rows_admitted;
					break;
				case Street::Flop:
					++diagnostics_.flop_rows_admitted;
					break;
				case Street::Turn:
					++diagnostics_.turn_rows_admitted;
					break;
				case Street::River:
					++diagnostics_.river_rows_admitted;
					break;
				case Street::Showdown:
					break;
			}
		}
	}

	void MultiwaySolverCoordinator::admit_prepared_batch(
		const std::vector<MultiwayPublicStateDescriptor>& public_states,
		const std::vector<std::size_t>& parent_order,
		const std::vector<MultiwaySparseRowShape>& rows,
		const std::vector<std::size_t>* parent_indexes)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admit_prepared_batch");
		validate_prepared_batch_inputs(public_states, parent_order, rows, parent_indexes);
		const auto lock_wait_start = std::chrono::steady_clock::now();
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->lock_acquisitions;
		diagnostics_.coordinator_lock_wait_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - lock_wait_start)
				.count());

		const auto new_state_count = preflight_prepared_states(public_states, parent_order, rows, parent_indexes);
		const auto summary = preflight_prepared_rows(public_states, rows, new_state_count);
		commit_prepared_batch(public_states, rows, summary);
	}
}
