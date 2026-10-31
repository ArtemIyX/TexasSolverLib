#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/engine/multiway_solver_detail.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace texas::solver::multiway
{
	MultiwayMemoryComponent MultiwayCoordinatorCheckpoint::memory_snapshot() const noexcept
	{
		MultiwayMemoryComponent result;
		result.add_buffer(public_states.size(), public_states.capacity(), sizeof(MultiwayPublicStateDescriptor));
		for (const auto& state : public_states)
		{
			result.add_buffer(state.incoming_edge.dealt_cards.size(), state.incoming_edge.dealt_cards.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.incoming_edge.transition_board.size(), state.incoming_edge.transition_board.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.board.size(), state.board.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.history.size(), state.history.capacity(), sizeof(MultiwayPublicHistoryEntry));
			result.add_buffer(state.legal_actions.size(), state.legal_actions.capacity(), sizeof(MultiwayActionDescriptor));
		}
		result.add_buffer(storage.shapes.size(), storage.shapes.capacity(), sizeof(MultiwaySparseRowShape));
		result.add_buffer(storage.active_pages.size(), storage.active_pages.capacity(), sizeof(MultiwaySparseStorageCheckpoint::ActivePage));
		result.add_buffer(storage.compact_regrets.size(), storage.compact_regrets.capacity(), sizeof(std::int32_t));
		result.add_buffer(storage.compact_strategy_masses.size(), storage.compact_strategy_masses.capacity(), sizeof(std::uint64_t));
		result.add_buffer(storage.regrets.size(), storage.regrets.capacity(), sizeof(double));
		result.add_buffer(storage.strategy_sums.size(), storage.strategy_sums.capacity(), sizeof(double));
		return result;
	}

	MultiwayCoordinatorCheckpoint MultiwaySolverCoordinator::checkpoint() const
	{
		MultiwayCoordinatorCheckpoint result;
		std::unique_lock<std::shared_mutex> lock;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.snapshot_lock_wait");
			const auto started = std::chrono::steady_clock::now();
			lock = std::unique_lock<std::shared_mutex>(traversal_mutex_);
			result.telemetry.snapshot_lock_wait_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.public_catalog_export");
			const auto started = std::chrono::steady_clock::now();
			public_catalog_.export_descriptors(result.public_states);
			result.telemetry.public_catalog_export_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		result.terminal_visits = diagnostics_.terminal_visits;
		result.leaf_visits = diagnostics_.leaf_visits;
		result.missing_lookup_requests = diagnostics_.missing_lookup_requests;
		result.last_merged_stream_fingerprint = diagnostics_.last_merged_stream_fingerprint;
		result.street_visits = diagnostics_.street_visits;
		if (compact_storage_ != nullptr)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.compact_storage_export");
			const auto started = std::chrono::steady_clock::now();
			compact_storage_->export_checkpoint(result.storage);
			result.telemetry.compact_storage_export_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		else
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.float64_storage_export");
			result.storage.shapes.reserve(storage_.metadata_.size());
			result.storage.regrets.reserve(storage_.regret_.size());
			result.storage.strategy_sums.reserve(storage_.strategy_sum_.size());
			for (const auto& row : storage_.metadata_)
			{
				result.storage.shapes.push_back(row.shape);
				const auto values = row.value_count();
				result.storage.regrets.insert(
					result.storage.regrets.end(),
					storage_.regret_.begin() + static_cast<std::ptrdiff_t>(row.regret_offset),
					storage_.regret_.begin() + static_cast<std::ptrdiff_t>(row.regret_offset + values));
				result.storage.strategy_sums.insert(
					result.storage.strategy_sums.end(),
					storage_.strategy_sum_.begin() + static_cast<std::ptrdiff_t>(row.strategy_sum_offset),
					storage_.strategy_sum_.begin() + static_cast<std::ptrdiff_t>(row.strategy_sum_offset + values));
			}
		}

		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.canonicalization");
			const auto started = std::chrono::steady_clock::now();
			std::unordered_map<std::uint64_t, std::size_t> state_indices;
			state_indices.reserve(result.public_states.size());
			for (std::size_t index = 0U; index < result.public_states.size(); ++index)
			{
				state_indices.emplace(result.public_states[index].id.value, index);
			}
			const auto unknown_depth = std::numeric_limits<std::uint32_t>::max();
			std::vector<std::uint32_t> state_depths(result.public_states.size(), unknown_depth);
			std::vector<std::size_t> ancestry;
			ancestry.reserve(128U);
			for (std::size_t index = 0U; index < result.public_states.size(); ++index)
			{
				if (state_depths[index] != unknown_depth)
					continue;
				ancestry.clear();
				auto current = index;
				while (state_depths[current] == unknown_depth && result.public_states[current].parent_id.value != 0U)
				{
					ancestry.push_back(current);
					current = state_indices.at(result.public_states[current].parent_id.value);
				}
				if (state_depths[current] == unknown_depth)
					state_depths[current] = 0U;
				auto depth = state_depths[current];
				for (auto reverse = ancestry.rbegin(); reverse != ancestry.rend(); ++reverse)
				{
					state_depths[*reverse] = ++depth;
				}
			}
			std::unordered_map<std::uint64_t, std::uint32_t> depth_by_state;
			depth_by_state.reserve(result.public_states.size());
			for (std::size_t index = 0U; index < result.public_states.size(); ++index)
			{
				depth_by_state.emplace(result.public_states[index].id.value, state_depths[index]);
			}
			std::sort(result.public_states.begin(), result.public_states.end(), [&depth_by_state](const MultiwayPublicStateDescriptor& left, const MultiwayPublicStateDescriptor& right) {
				const auto left_depth = depth_by_state.at(left.id.value);
				const auto right_depth = depth_by_state.at(right.id.value);
				return left_depth != right_depth ? left_depth < right_depth : left.id.value < right.id.value;
			});
			// Compact export already emits public-state-major storage rows. Keep
			// the reference backend's canonical copy for Float64 equivalence.
			if (compact_storage_ == nullptr)
			{
				std::vector<std::size_t> offsets(result.storage.shapes.size() + 1U, 0U);
				std::vector<std::size_t> order(result.storage.shapes.size(), 0U);
				for (std::size_t index = 0U; index < result.storage.shapes.size(); ++index)
				{
					offsets[index + 1U] = offsets[index] + static_cast<std::size_t>(result.storage.shapes[index].bucket_count) * result.storage.shapes[index].action_count;
					order[index] = index;
				}
				std::sort(order.begin(), order.end(), [&result](std::size_t left, std::size_t right) {
					const auto& lhs = result.storage.shapes[left].infoset;
					const auto& rhs = result.storage.shapes[right].infoset;
					return lhs.public_state.value != rhs.public_state.value
						? lhs.public_state.value < rhs.public_state.value
						: lhs.seat < rhs.seat;
				});
				MultiwaySparseStorageCheckpoint canonical;
				canonical.shapes.reserve(order.size());
				canonical.regrets.reserve(result.storage.regrets.size());
				canonical.strategy_sums.reserve(result.storage.strategy_sums.size());
				for (const auto index : order)
				{
					canonical.shapes.push_back(result.storage.shapes[index]);
					canonical.regrets.insert(canonical.regrets.end(),
						result.storage.regrets.begin() + static_cast<std::ptrdiff_t>(offsets[index]),
						result.storage.regrets.begin() + static_cast<std::ptrdiff_t>(offsets[index + 1U]));
					canonical.strategy_sums.insert(canonical.strategy_sums.end(),
						result.storage.strategy_sums.begin() + static_cast<std::ptrdiff_t>(offsets[index]),
						result.storage.strategy_sums.begin() + static_cast<std::ptrdiff_t>(offsets[index + 1U]));
				}
				result.storage = std::move(canonical);
			}
			result.telemetry.canonicalization_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		return result;
	}

	void MultiwaySolverCoordinator::restore_checkpoint(const MultiwayCoordinatorCheckpoint& checkpoint)
	{
		if (checkpoint.public_states.empty() || !same_public_state_descriptor(checkpoint.public_states.front(), request_.root().public_state) || checkpoint.storage.regrets.size() != checkpoint.storage.strategy_sums.size() || checkpoint.storage.compact_regrets.size() != checkpoint.storage.compact_strategy_masses.size())
		{
			throw std::invalid_argument("multiway coordinator checkpoint is incomplete");
		}
		std::size_t expected_values = 0U;
		for (const auto& shape : checkpoint.storage.shapes)
		{
			if (shape.bucket_count == 0U || shape.action_count == 0U || expected_values > std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(shape.bucket_count) * shape.action_count)
			{
				throw std::invalid_argument("multiway coordinator checkpoint has invalid row shapes");
			}
			expected_values += static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
		}
		if (expected_values != checkpoint.storage.regrets.size())
		{
			throw std::invalid_argument("multiway coordinator checkpoint row values do not match shapes");
		}
		for (const auto value : checkpoint.storage.regrets)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway coordinator checkpoint has non-finite regrets");
		}
		for (const auto value : checkpoint.storage.strategy_sums)
		{
			if (!std::isfinite(value) || value < 0.0)
			{
				throw std::invalid_argument("multiway coordinator checkpoint has invalid strategy sums");
			}
		}
		{
			std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
			public_catalog_.clear();
			storage_.metadata_.clear();
			storage_.regret_.clear();
			storage_.strategy_sum_.clear();
			diagnostics_ = {};
		}
		for (const auto& state : checkpoint.public_states)
			admit_public_state(state);
		for (const auto& shape : checkpoint.storage.shapes)
			admit_infoset_row(shape);
		if (compact_storage_ != nullptr)
		{
			for (const auto& page : checkpoint.storage.active_pages)
				compact_storage_->admit_page(page.infoset, page.page_index);
			if (!checkpoint.storage.compact_regrets.empty())
			{
				compact_storage_->restore_checkpoint_values(
					checkpoint.storage.compact_regrets,
					checkpoint.storage.compact_strategy_masses);
			}
			else
			{
				std::size_t offset = 0U;
				for (const auto& shape : checkpoint.storage.shapes)
				{
					for (std::uint8_t action = 0; action < shape.action_count; ++action)
					{
						for (std::uint32_t bucket = 0; bucket < shape.bucket_count; ++bucket)
						{
							if (compact_storage_->has_page(shape.infoset, bucket / compact_storage_->page_width()))
								compact_storage_->apply_delta(shape.infoset, bucket, action,
									checkpoint.storage.regrets[offset], checkpoint.storage.strategy_sums[offset]);
							++offset;
						}
					}
				}
			}
		}
		{
			std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
			storage_.regret_ = checkpoint.storage.regrets;
			storage_.strategy_sum_ = checkpoint.storage.strategy_sums;
			diagnostics_.terminal_visits = checkpoint.terminal_visits;
			diagnostics_.leaf_visits = checkpoint.leaf_visits;
			diagnostics_.missing_lookup_requests = checkpoint.missing_lookup_requests;
			diagnostics_.last_merged_stream_fingerprint = checkpoint.last_merged_stream_fingerprint;
			diagnostics_.street_visits = checkpoint.street_visits;
		}
	}
}
