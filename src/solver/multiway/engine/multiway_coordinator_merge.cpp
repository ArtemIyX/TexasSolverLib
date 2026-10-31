#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "multiway_solver_detail.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	namespace
	{

		template <typename Less>
		void tournament_push(
			std::vector<std::size_t>& heap,
			std::size_t& active_count,
			std::size_t stream,
			const Less& less)
		{
			std::size_t position = active_count++;
			while (position > 0U)
			{
				const auto parent = (position - 1U) / 2U;
				if (!less(stream, heap[parent]))
					break;
				heap[position] = heap[parent];
				position = parent;
			}
			heap[position] = stream;
		}

		template <typename Less>
		void tournament_advance(
			std::vector<std::size_t>& heap,
			std::size_t& active_count,
			bool exhausted,
			const Less& less)
		{
			if (exhausted)
			{
				--active_count;
				if (active_count == 0U)
					return;
				heap[0] = heap[active_count];
			}

			std::size_t position = 0U;
			while (true)
			{
				const auto left = position * 2U + 1U;
				if (left >= active_count)
					break;
				const auto right = left + 1U;
				const auto child = right < active_count && less(heap[right], heap[left]) ? right : left;
				if (!less(heap[child], heap[position]))
					break;
				std::swap(heap[position], heap[child]);
				position = child;
			}
		}

		void append_delta_fingerprint(const MultiwayWorkerDelta& delta, std::uint64_t& hash) noexcept
		{
			hash_u64(delta.infoset.public_state.value, hash);
			hash_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(delta.infoset.seat)), hash);
			hash_u64(delta.bucket, hash);
			hash_u64(delta.action, hash);
			hash_u64(delta.trajectory_id, hash);
			std::uint64_t regret_bits = 0U;
			std::uint64_t strategy_bits = 0U;
			std::memcpy(&regret_bits, &delta.regret, sizeof(regret_bits));
			std::memcpy(&strategy_bits, &delta.strategy_sum, sizeof(strategy_bits));
			hash_u64(regret_bits, hash);
			hash_u64(strategy_bits, hash);
		}
	} // namespace

	void MultiwaySolverCoordinator::merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams)
	{
		merge_worker_streams(streams, MultiwayDeltaMergePath::Automatic);
	}

	void MultiwaySolverCoordinator::merge_worker_streams(
		const std::vector<MultiwayWorkerDeltaStream>& streams,
		MultiwayDeltaMergePath path)
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		if (streams.size() != request_.limits().worker_count)
		{
			throw std::invalid_argument("multiway merge requires one stream for every configured worker");
		}
		merge_stream_views_.resize(streams.size());
		for (std::size_t worker = 0U; worker < streams.size(); ++worker)
			merge_stream_views_[worker] = &streams[worker];
		merge_worker_streams_locked(merge_stream_views_, path);
	}

	void MultiwaySolverCoordinator::merge_worker_streams(
		const std::vector<const MultiwayWorkerDeltaStream*>& streams)
	{
		merge_worker_streams(streams, MultiwayDeltaMergePath::Automatic);
	}

	void MultiwaySolverCoordinator::merge_worker_streams(
		const std::vector<const MultiwayWorkerDeltaStream*>& streams,
		MultiwayDeltaMergePath path)
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		merge_worker_streams_locked(streams, path);
	}

	void MultiwaySolverCoordinator::merge_worker_streams_locked(
		const std::vector<const MultiwayWorkerDeltaStream*>& streams,
		MultiwayDeltaMergePath path)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.merge_worker_streams_locked");
		if (streams.size() != request_.limits().worker_count)
		{
			throw std::invalid_argument("multiway merge requires one stream for every configured worker");
		}

		std::size_t delta_count = 0;
		for (std::size_t worker = 0; worker < streams.size(); ++worker)
		{
			const auto* stream = streams[worker];
			if (stream == nullptr || stream->worker_index() != worker || stream->size() > request_.limits().regret_delta_capacity() || !stream->is_fixed_order())
			{
				throw std::invalid_argument("multiway worker streams must be bounded and in fixed worker/row/action order");
			}
			if (stream->size() > std::numeric_limits<std::size_t>::max() - delta_count)
			{
				throw std::overflow_error("multiway worker delta count overflows size_t");
			}
			delta_count += stream->size();
		}

		if (path == MultiwayDeltaMergePath::Automatic)
			path = select_multiway_delta_merge_path(streams.size());
		if (path != MultiwayDeltaMergePath::Reference && path != MultiwayDeltaMergePath::Tournament)
			throw std::invalid_argument("multiway merge path is invalid");

#pragma pack(push, 8)
		struct ExternalMergeAbortGuard
		{
			MultiwayExternalTrainingStorage* storage = nullptr;
			bool active = false;
			~ExternalMergeAbortGuard() noexcept
			{
				if (active)
					try
					{
						storage->abort_batch();
					}
					catch (...)
					{
					}
			}
		};
#pragma pack(pop)
		ExternalMergeAbortGuard external_abort;
		if (external_storage_ != nullptr)
		{
			external_storage_->begin_batch(external_storage_->next_batch_number());
			external_abort.storage = external_storage_.get();
			external_abort.active = true;
			external_storage_->compact_components();
			external_storage_->begin_page_batch();
		}

		const auto stream_fingerprint = merge_worker_stream_cells(streams, path, delta_count);
		for (const auto& cell : pending_merge_cells_)
		{
			storage_.regret_[cell.index] = cell.regret;
			storage_.strategy_sum_[cell.index] = cell.strategy_sum;
		}
		const auto merged_stream_fingerprint = stream_fingerprint == 0U ? 1U : stream_fingerprint;
		if (external_storage_ != nullptr)
		{
			const auto& current = external_storage_->current_manifest();
			external_storage_->commit_batch(
				merged_stream_fingerprint,
				current.row_count, current.logical_value_count);
			external_abort.active = false;
		}
		diagnostics_.worker_delta_entries_merged += delta_count;
		diagnostics_.last_merged_stream_fingerprint = merged_stream_fingerprint;
	}
	std::uint64_t MultiwaySolverCoordinator::merge_worker_stream_cells(
		const std::vector<const MultiwayWorkerDeltaStream*>& streams,
		MultiwayDeltaMergePath path, std::size_t delta_count)
	{
		pending_merge_cells_.clear();
		merge_stream_cursors_.resize(streams.size());
		std::fill(merge_stream_cursors_.begin(), merge_stream_cursors_.end(), 0U);
		std::size_t active_tournament_streams = 0U;
		const auto tournament_less = [&](std::size_t left, std::size_t right) {
			const auto& left_delta = streams[left]->deltas()[merge_stream_cursors_[left]];
			const auto& right_delta = streams[right]->deltas()[merge_stream_cursors_[right]];
			if (delta_less(left_delta, right_delta))
				return true;
			if (delta_less(right_delta, left_delta))
				return false;
			return left < right;
		};
		if (path == MultiwayDeltaMergePath::Tournament)
		{
			merge_tournament_.resize(streams.size());
			for (std::size_t worker = 0U; worker < streams.size(); ++worker)
			{
				if (!streams[worker]->deltas().empty())
					tournament_push(merge_tournament_, active_tournament_streams, worker, tournament_less);
			}
		}
		std::uint64_t stream_fingerprint = 1469598103934665603ULL;
		hash_u64(MULTIWAY_MERGE_ORDER_VERSION, stream_fingerprint);
		hash_u64(delta_count, stream_fingerprint);
		bool have_cell = false;
		MultiwayInfosetId cell_infoset{};
		std::uint32_t cell_bucket = 0U;
		std::uint8_t cell_action = 0U;
		double cell_regret = 0.0;
		double cell_strategy_sum = 0.0;
		const MultiwaySparseRowMetadata* compact_cell_row = nullptr;
		MultiwayExternalRowBinding external_row;
		MultiwayInfosetId external_row_infoset{};
		bool have_external_row = false;
		MultiwayExternalCompactStorage::PinnedBucket external_pinned;
		const auto pin_external_bucket = [&](const MultiwayExternalRowBinding& row, std::uint32_t bucket) {
			if (external_pinned.owner != nullptr)
			{
				external_storage_->compact_storage().unpin_bucket(external_pinned);
				external_storage_->compact_storage().flush_page_batch_if_full();
			}
			auto pinned = external_storage_->compact_storage().pin_bucket(row, bucket, true);
			external_pinned.owner = pinned.owner;
			external_pinned.page = pinned.page;
			external_pinned.local_bucket = pinned.local_bucket;
			external_pinned.writable = pinned.writable;
			pinned.owner = nullptr;
			pinned.page = {};
		};
		std::uint64_t previous_trajectory = 0U;
		bool have_trajectory = false;
		const auto flush_cell = [&] {
			if (!have_cell || compact_storage_ != nullptr || external_storage_ != nullptr)
				return;
			const auto* row = storage_.metadata(cell_infoset);
			if (row == nullptr || cell_bucket >= row->shape.bucket_count || cell_action >= row->shape.action_count)
			{
				throw std::invalid_argument("multiway delta does not match an admitted row cell");
			}
			const auto index = row->regret_offset + static_cast<std::size_t>(cell_action) * row->shape.bucket_count + cell_bucket;
			pending_merge_cells_.push_back({ index, cell_regret, cell_strategy_sum });
		};
		std::size_t merged_count = 0U;
		while (merged_count < delta_count)
		{
			const MultiwayWorkerDelta* next = nullptr;
			std::size_t next_worker = 0U;
			if (path == MultiwayDeltaMergePath::Tournament)
			{
				if (active_tournament_streams != 0U)
				{
					next_worker = merge_tournament_[0];
					next = &streams[next_worker]->deltas()[merge_stream_cursors_[next_worker]];
				}
			}
			else
			{
				for (std::size_t worker = 0U; worker < streams.size(); ++worker)
				{
					const auto cursor = merge_stream_cursors_[worker];
					const auto& deltas = streams[worker]->deltas();
					if (cursor < deltas.size() && (next == nullptr || delta_less(deltas[cursor], *next)))
					{
						next = &deltas[cursor];
						next_worker = worker;
					}
				}
			}
			if (next == nullptr)
				throw std::logic_error("multiway k-way merge lost a worker delta");
			const auto& delta = *next;
			append_delta_fingerprint(delta, stream_fingerprint);
			if (!delta_is_finite(delta))
				throw std::invalid_argument("multiway delta must be finite");
			const bool same_cell = have_cell && delta.infoset == cell_infoset && delta.bucket == cell_bucket && delta.action == cell_action;
			if (!same_cell)
			{
				flush_cell();
				have_cell = true;
				cell_infoset = delta.infoset;
				cell_bucket = delta.bucket;
				cell_action = delta.action;
				if (external_storage_ != nullptr)
				{
					if (!have_external_row || external_row_infoset != delta.infoset)
					{
						if (!external_storage_->compact_storage().lookup_row(delta.infoset, external_row))
							throw std::invalid_argument("multiway delta does not match an admitted external row");
						external_row_infoset = delta.infoset;
						have_external_row = true;
					}
					if (delta.bucket >= external_row.bucket_count() || delta.action >= external_row.action_count())
						throw std::invalid_argument("multiway delta does not match an admitted external row cell");
					const auto page_id = external_row.first_page_id() + (delta.bucket / request_.limits().compact_page_width);
					if (external_pinned.owner == nullptr || external_pinned.page.page_id != page_id)
						pin_external_bucket(external_row, delta.bucket);
					else
						external_pinned.local_bucket = delta.bucket % request_.limits().compact_page_width;
				}
				else if (compact_storage_ == nullptr)
				{
					const auto* row = storage_.metadata(delta.infoset);
					if (row == nullptr || delta.bucket >= row->shape.bucket_count || delta.action >= row->shape.action_count)
					{
						throw std::invalid_argument("multiway delta does not match an admitted row cell");
					}
					cell_regret = storage_.regret_[row->regret_offset + static_cast<std::size_t>(delta.action) * row->shape.bucket_count + delta.bucket];
					cell_strategy_sum = storage_.strategy_sum_[row->strategy_sum_offset + static_cast<std::size_t>(delta.action) * row->shape.bucket_count + delta.bucket];
				}
				else
				{
					compact_cell_row = compact_storage_->metadata(delta.infoset);
					if (compact_cell_row == nullptr || delta.bucket >= compact_cell_row->shape.bucket_count || delta.action >= compact_cell_row->shape.action_count)
					{
						throw std::invalid_argument("multiway delta does not match an admitted row cell");
					}
					cell_regret = 0.0;
					cell_strategy_sum = 0.0;
				}
				have_trajectory = false;
			}
			if (have_trajectory && delta.trajectory_id == previous_trajectory)
			{
				throw std::invalid_argument("multiway merge received duplicate trajectory updates for one cell");
			}
			previous_trajectory = delta.trajectory_id;
			have_trajectory = true;
			if (external_storage_ != nullptr)
			{
				external_storage_->compact_storage().apply_delta(
					external_pinned, delta.action, delta.regret, delta.strategy_sum);
			}
			else if (compact_storage_ != nullptr)
			{
				compact_storage_->apply_delta(*compact_cell_row, delta.bucket, delta.action,
					delta.regret, delta.strategy_sum);
			}
			else
			{
				const auto next_regret = cell_regret + delta.regret;
				const auto next_strategy_sum = cell_strategy_sum + delta.strategy_sum;
				if (!std::isfinite(next_regret) || !std::isfinite(next_strategy_sum) || (delta.regret != 0.0 && next_regret == cell_regret) || (delta.strategy_sum != 0.0 && next_strategy_sum == cell_strategy_sum))
				{
					throw std::overflow_error("multiway sparse merge would lose a nonzero Float64 update");
				}
				cell_regret = next_regret;
				cell_strategy_sum = next_strategy_sum;
			}
			++merge_stream_cursors_[next_worker];
			if (path == MultiwayDeltaMergePath::Tournament)
				tournament_advance(
					merge_tournament_,
					active_tournament_streams,
					merge_stream_cursors_[next_worker] == streams[next_worker]->size(),
					tournament_less);
			++merged_count;
		}
		flush_cell();
		if (external_pinned.owner != nullptr)
			external_storage_->compact_storage().unpin_bucket(external_pinned);

		return stream_fingerprint;
	}
} // namespace texas::solver::multiway
