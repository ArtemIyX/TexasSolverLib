#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_solver_detail.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "core/fingerprint.hpp"
#include "games/multiway_fixed.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace texas::solver::multiway
{
	std::size_t MultiwaySolverCoordinator::pending_merge_cell_size() noexcept
	{
		return sizeof(PendingMergeCell);
	}
	MultiwayDeltaMergePath select_multiway_delta_merge_path(std::size_t stream_count) noexcept
	{
		return stream_count > MULTIWAY_DELTA_TOURNAMENT_STREAM_THRESHOLD
			? MultiwayDeltaMergePath::Tournament
			: MultiwayDeltaMergePath::Reference;
	}

	namespace
	{

		std::size_t checked_sparse_value_capacity(const MultiwaySolveRequest& request)
		{
			if (!request.root().root_uses_exact_private_hand)
				return request.limits().max_sparse_values;
			const auto exact_row_count = std::min(
				request.limits().max_sparse_rows,
				request.limits().max_public_states);
			if (exact_row_count > std::numeric_limits<std::size_t>::max() / MULTIWAY_HOLE_COMBINATION_COUNT / MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			{
				throw std::overflow_error("multiway sparse value capacity overflows size_t");
			}
			const auto exact_values = exact_row_count * MULTIWAY_HOLE_COMBINATION_COUNT * MULTIWAY_MAX_ABSTRACTED_ACTIONS;
			if (exact_values > std::numeric_limits<std::size_t>::max() - request.limits().max_sparse_values)
			{
				throw std::overflow_error("multiway sparse value capacity overflows size_t");
			}
			return exact_values + request.limits().max_sparse_values;
		}

		MultiwayExternalStorageConfig external_component_config(
			const MultiwaySolverLimits::ExternalStorage& external,
			const std::filesystem::path& component, std::uint64_t cache_bytes)
		{
			MultiwayExternalStorageConfig config;
			config.directory = external.directory / component;
			config.cache_bytes = cache_bytes;
			config.maximum_wal_bytes = external.maximum_wal_bytes;
			config.segment_target_bytes = external.segment_target_bytes;
			config.compaction_fan_in = external.compaction_fan_in;
			config.compaction_scratch_bytes = external.compaction_scratch_bytes;
			config.disk_reserve_bytes = external.disk_reserve_bytes;
			config.synchronous_durability = external.synchronous_durability;
			config.disk_amplification_limit = external.disk_amplification_limit;
			return config;
		}

		std::uint64_t external_regret_batch_fingerprint(
			std::uint64_t operation, const double* values, std::size_t count) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			texas::core::fingerprint::append_u64(hash, operation);
			for (std::size_t index = 0U; index < count; ++index)
			{
				std::uint64_t bits = 0U;
				static_assert(sizeof(bits) == sizeof(values[index]), "double width must be 64 bits");
				std::memcpy(&bits, &values[index], sizeof(bits));
				texas::core::fingerprint::append_u64(hash, bits);
			}
			const auto result = texas::core::fingerprint::finish(hash);
			return result == 0U ? 1U : result;
		}

		template <typename Operation>
		auto commit_external_regret_batch(
			MultiwayExternalTrainingStorage& storage,
			std::uint64_t fingerprint,
			Operation&& operation)
		{
			const auto& current = storage.current_manifest();
			const auto row_count = current.row_count;
			const auto logical_value_count = current.logical_value_count;
			storage.begin_batch(storage.next_batch_number());
			try
			{
				storage.begin_page_batch();
				auto result = operation(storage.compact_storage());
				storage.commit_batch(fingerprint, row_count, logical_value_count);
				return result;
			}
			catch (...)
			{
				try
				{
					storage.abort_batch();
				}
				catch (...)
				{
				}
				throw;
			}
		}

	} // namespace

	void MultiwayRootSnapshot::validate() const
	{
		validate_public_state_descriptor(public_state);
		private_ranges.validate();
		rake_policy.validate();

		if (public_state.parent_id.value != 0 || public_state.incoming_edge.kind != MultiwayPublicParentEdgeKind::None)
		{
			throw std::invalid_argument("multiway root must not carry a parent edge");
		}

		const auto seat_count = public_state.betting.stacks.size();
		if (seat_count < 2U || seat_count > 6U || private_ranges.ranges.size() != seat_count)
		{
			throw std::invalid_argument("multiway root requires matching two-through-six seat state and ranges");
		}
		if (action_abstraction_version == 0 || leaf_model_version == 0)
		{
			throw std::invalid_argument("multiway root requires stable non-zero public and version identities");
		}
		if (root_infoset.public_state != public_state.id || root_infoset.seat < 0 || static_cast<std::size_t>(root_infoset.seat) >= seat_count || root_infoset.seat != public_state.betting.current_player)
		{
			throw std::invalid_argument("multiway root infoset must identify the acting root seat");
		}
		if (seat_order.size() != seat_count || next_street_first_seat < 0 || static_cast<std::size_t>(next_street_first_seat) >= seat_count || odd_chip_first_seat < 0 || static_cast<std::size_t>(odd_chip_first_seat) >= seat_count || !valid_odd_chip_rule(odd_chip_rule))
		{
			throw std::invalid_argument("multiway root has invalid seat or odd-chip order");
		}
		std::vector<bool> seen(seat_count, false);
		for (const auto seat : seat_order)
		{
			if (seat < 0 || static_cast<std::size_t>(seat) >= seat_count || seen[static_cast<std::size_t>(seat)])
			{
				throw std::invalid_argument("multiway root seat order must be a permutation");
			}
			seen[static_cast<std::size_t>(seat)] = true;
		}
		if (next_street_first_seat != seat_order.front())
		{
			throw std::invalid_argument("multiway root next-street seat must lead the canonical seat order");
		}
		for (std::size_t index = 1; index < seat_order.size(); ++index)
		{
			const auto expected = static_cast<PlayerId>((seat_order[index - 1U] + 1) % seat_count);
			if (seat_order[index] != expected)
			{
				throw std::invalid_argument("multiway root seat order must follow the canonical seat cycle");
			}
		}
		if (!same_card_set(public_state.board, private_ranges.board) || (value_units != MultiwayValueUnits::Chips && value_units != MultiwayValueUnits::BigBlinds))
		{
			throw std::invalid_argument("multiway root has inconsistent board or value units");
		}
	}

	void MultiwaySolverLimits::validate() const
	{
		const auto regret_capacity = regret_delta_capacity();
		const auto continuation_capacity = continuation_delta_capacity();
		if (worker_count == 0U || trajectories_per_batch == 0U || max_public_states == 0U || max_sparse_rows == 0U || max_sparse_values == 0U || regret_capacity == 0U || continuation_capacity == 0U)
		{
			throw std::invalid_argument("multiway solver limits require non-zero bounded capacities");
		}
		if (regret_capacity > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(worker_count) || continuation_capacity > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(worker_count))
		{
			throw std::overflow_error("multiway aggregate worker delta capacity overflows size_t");
		}
		const auto public_capacity = discovery_public_state_capacity();
		const auto row_capacity = discovery_row_capacity();
		if ((public_capacity != 0U && public_capacity > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(worker_count)) || (row_capacity != 0U && row_capacity > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(worker_count)))
		{
			throw std::overflow_error("multiway aggregate discovery capacity overflows size_t");
		}
		if (max_batches == 0U || compact_page_width == 0U)
		{
			throw std::invalid_argument("multiway solver limits require non-zero batch and compact page budgets");
		}
		if (storage_backend != StorageBackend::Float64Reference && storage_backend != StorageBackend::CompactInt32 && storage_backend != StorageBackend::ExternalCompactInt32)
			throw std::invalid_argument("multiway solver storage backend is invalid");
		if (storage_backend == StorageBackend::ExternalCompactInt32 && (external_storage.directory.empty() || external_storage.public_catalog_cache_bytes == 0U || external_storage.row_page_cache_bytes == 0U || external_storage.maximum_wal_bytes == 0U || external_storage.segment_target_bytes == 0U || external_storage.compaction_fan_in < 2U || external_storage.compaction_scratch_bytes == 0U || external_storage.disk_reserve_bytes == 0U))
			throw std::invalid_argument("external compact storage requires a directory and positive bounded resources");
	}

	MultiwaySolveRequest::MultiwaySolveRequest(
		MultiwayRootSnapshot root,
		MultiwayCFRConfig cfr_config,
		MultiwaySolverLimits limits)
		: root_(std::move(root)), cfr_config_(cfr_config), limits_(limits), private_range_feasibility_(preflight_multiway_private_range_feasibility(root_.private_ranges))
	{
		root_.validate();
		if (private_range_feasibility_.status == MultiwayPrivateRangeFeasibilityStatus::Infeasible)
		{
			throw std::invalid_argument("multiway solve request has no compatible private deal");
		}
		if (private_range_feasibility_.status == MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted)
		{
			throw std::runtime_error("multiway solve request private-range feasibility preflight exhausted its budget");
		}
		cfr_config_.validate();
		limits_.validate();
		if (cfr_config_.player_count != root_.seat_order.size())
		{
			throw std::invalid_argument("multiway CFR configuration must match the root seat count");
		}
		compiled_private_ranges_.emplace(root_.private_ranges);
	}

	std::size_t MultiwaySparseRowMetadata::value_count() const noexcept
	{
		return static_cast<std::size_t>(shape.bucket_count) * static_cast<std::size_t>(shape.action_count);
	}

	MultiwaySparseRowStorage::MultiwaySparseRowStorage(std::size_t max_rows, std::size_t max_values)
		: max_rows_(max_rows), max_values_(max_values) {}

	bool MultiwaySparseRowStorage::has_row(MultiwayInfosetId infoset) const noexcept
	{
		return metadata(infoset) != nullptr;
	}

	const MultiwaySparseRowMetadata* MultiwaySparseRowStorage::metadata(MultiwayInfosetId infoset) const noexcept
	{
		const auto found = std::lower_bound(
			metadata_.begin(), metadata_.end(), infoset,
			[](const MultiwaySparseRowMetadata& row, MultiwayInfosetId key) {
				return row.shape.infoset < key;
			});
		if (found == metadata_.end() || !(found->shape.infoset == infoset))
			return nullptr;
		return &*found;
	}

	std::vector<Probability> MultiwaySparseRowStorage::average_strategy(
		MultiwayInfosetId infoset,
		std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
		{
			throw std::out_of_range("multiway average strategy row or bucket is unavailable");
		}

		std::vector<Probability> result(row->shape.action_count, 0.0);
		average_strategy_into(infoset, bucket, result.data(), result.size());
		return result;
	}

	void MultiwaySparseRowStorage::average_strategy_into(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		Probability* output,
		std::size_t output_size) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || output == nullptr || output_size != row->shape.action_count)
		{
			throw std::out_of_range("multiway average strategy output does not match its row");
		}

		double total = 0.0;
		for (std::size_t action = 0; action < output_size; ++action)
		{
			const auto index = row->strategy_sum_offset + action * row->shape.bucket_count + bucket;
			output[action] = strategy_sum_[index] > 0.0 ? strategy_sum_[index] : 0.0;
			total += output[action];
		}
		if (total == 0.0)
		{
			const auto uniform = 1.0 / static_cast<double>(output_size);
			std::fill(output, output + output_size, uniform);
			return;
		}
		for (std::size_t action = 0U; action < output_size; ++action)
			output[action] /= total;
	}

	std::vector<Probability> MultiwaySparseRowStorage::regret_matched_strategy(
		MultiwayInfosetId infoset,
		std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
		{
			throw std::out_of_range("multiway regret row or bucket is unavailable");
		}
		std::vector<Probability> strategy(row->shape.action_count, 0.0);
		regret_matched_strategy_into(infoset, bucket, strategy.data(), strategy.size());
		return strategy;
	}

	bool MultiwaySparseRowStorage::action_below_regret(
		MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
		double threshold) const noexcept
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count || !std::isfinite(threshold))
			return false;
		return regret_[row->regret_offset + static_cast<std::size_t>(action) * row->shape.bucket_count + bucket] < threshold;
	}

	void MultiwaySparseRowStorage::regret_matched_strategy_into(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		Probability* output,
		std::size_t output_size) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || output == nullptr || output_size != row->shape.action_count)
		{
			throw std::out_of_range("multiway regret strategy output does not match its row");
		}
		multiway_regret_matching_action_major_into(
			regret_.data() + row->regret_offset + bucket,
			(output_size - 1U) * row->shape.bucket_count + 1U,
			output_size,
			row->shape.bucket_count,
			output);
	}

	std::vector<double> MultiwaySparseRowStorage::strategy_sums(
		MultiwayInfosetId infoset,
		std::uint32_t bucket) const
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count)
		{
			throw std::out_of_range("multiway strategy sum row or bucket is unavailable");
		}
		std::vector<double> result(row->shape.action_count, 0.0);
		for (std::size_t action = 0; action < result.size(); ++action)
		{
			result[action] = strategy_sum_[row->strategy_sum_offset + action * row->shape.bucket_count + bucket];
		}
		return result;
	}

	void MultiwaySparseRowStorage::scale_regrets(double factor)
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
		{
			throw std::invalid_argument("multiway regret discount must be finite and in (0, 1]");
		}
		for (auto& regret : regret_)
			regret *= factor;
	}

	std::size_t MultiwaySparseRowStorage::prune_negative_regrets(
		double threshold, double regret_floor) noexcept
	{
		std::size_t pruned = 0;
		for (auto& regret : regret_)
		{
			if (regret < threshold)
			{
				regret = std::max(regret_floor, threshold);
				++pruned;
			}
		}
		return pruned;
	}

	void MultiwaySparseRowStorage::admit_row(const MultiwaySparseRowShape& shape)
	{
		if (shape.infoset.public_state.value == 0 || shape.infoset.seat < 0)
		{
			throw std::invalid_argument("multiway sparse row requires a stable per-seat infoset id");
		}
		if (const auto* existing = metadata(shape.infoset))
		{
			if (existing->shape.bucket_count != shape.bucket_count || existing->shape.action_count != shape.action_count)
			{
				throw std::invalid_argument("multiway sparse row infoset was admitted with a conflicting shape");
			}
			return;
		}
		const auto values = checked_value_count(shape);
		if (metadata_.size() >= max_rows_ || regret_.size() > max_values_ || values > max_values_ - regret_.size())
		{
			throw std::length_error("multiway sparse row admission exceeds configured capacity");
		}

		MultiwaySparseRowMetadata row;
		row.shape = shape;
		row.regret_offset = regret_.size();
		row.strategy_sum_offset = strategy_sum_.size();
		const auto insertion = std::lower_bound(
			metadata_.begin(), metadata_.end(), shape.infoset,
			[](const MultiwaySparseRowMetadata& existing, MultiwayInfosetId key) {
				return existing.shape.infoset < key;
			});
		const auto old_regret_size = regret_.size();
		const auto old_strategy_size = strategy_sum_.size();
		regret_.resize(old_regret_size + values, 0.0);
		try
		{
			strategy_sum_.resize(old_strategy_size + values, 0.0);
			metadata_.insert(insertion, row);
		}
		catch (...)
		{
			strategy_sum_.resize(old_strategy_size);
			regret_.resize(old_regret_size);
			throw;
		}
	}

	void MultiwaySparseRowStorage::admit_rows(
		const std::vector<MultiwaySparseRowShape>& shapes,
		const std::vector<std::uint8_t>& new_row_flags)
	{
		if (shapes.size() != new_row_flags.size())
			throw std::invalid_argument("multiway sparse row batch flags do not match shapes");
		if (!std::is_sorted(shapes.begin(), shapes.end(), [](const auto& left, const auto& right) {
				return left.infoset < right.infoset;
			}))
			throw std::invalid_argument("multiway sparse row batch shapes must be sorted");
		for (std::size_t index = 1U; index < shapes.size(); ++index)
			if (shapes[index - 1U].infoset == shapes[index].infoset)
				throw std::invalid_argument("multiway sparse row batch shapes must be unique");
		std::size_t new_count = 0U;
		std::size_t new_values = 0U;
		for (std::size_t index = 0U; index < shapes.size(); ++index)
		{
			if (new_row_flags[index] == 0U)
				continue;
			const auto& shape = shapes[index];
			if (shape.infoset.public_state.value == 0U || shape.infoset.seat < 0)
				throw std::invalid_argument("multiway sparse row requires a stable per-seat infoset id");
			const auto values = checked_value_count(shape);
			if (new_values > std::numeric_limits<std::size_t>::max() - values)
				throw std::overflow_error("multiway sparse row batch value count overflows size_t");
			new_values += values;
			++new_count;
		}
		if (metadata_.size() > max_rows_ || new_count > max_rows_ - metadata_.size())
			throw std::length_error("multiway sparse row admission exceeds configured capacity");
		if (regret_.size() > max_values_ || new_values > max_values_ - regret_.size())
			throw std::length_error("multiway sparse row admission exceeds configured value capacity");
		if (new_count == 0U)
			return;

		std::vector<MultiwaySparseRowMetadata> pending;
		pending.reserve(new_count);
		std::size_t value_offset = regret_.size();
		std::size_t existing_index = 0U;
		for (std::size_t index = 0U; index < shapes.size(); ++index)
		{
			if (new_row_flags[index] == 0U)
				continue;
			while (existing_index < metadata_.size() && metadata_[existing_index].shape.infoset < shapes[index].infoset)
				++existing_index;
			if (existing_index < metadata_.size() && metadata_[existing_index].shape.infoset == shapes[index].infoset)
				throw std::invalid_argument("multiway sparse row batch contains an admitted row");
			const auto values = checked_value_count(shapes[index]);
			pending.push_back({ shapes[index], value_offset, value_offset });
			value_offset += values;
		}

		metadata_.reserve(metadata_.size() + pending.size());
		regret_.reserve(regret_.size() + new_values);
		strategy_sum_.reserve(strategy_sum_.size() + new_values);
		std::vector<MultiwaySparseRowMetadata> merged;
		merged.reserve(metadata_.size() + pending.size());
		existing_index = 0U;
		std::size_t pending_index = 0U;
		while (existing_index < metadata_.size() || pending_index < pending.size())
		{
			if (pending_index == pending.size() || (existing_index < metadata_.size() && metadata_[existing_index].shape.infoset < pending[pending_index].shape.infoset))
				merged.push_back(metadata_[existing_index++]);
			else
				merged.push_back(pending[pending_index++]);
		}

		const auto old_regret_size = regret_.size();
		const auto old_strategy_size = strategy_sum_.size();
		try
		{
			regret_.resize(old_regret_size + new_values, 0.0);
			strategy_sum_.resize(old_strategy_size + new_values, 0.0);
			metadata_.swap(merged);
		}
		catch (...)
		{
			strategy_sum_.resize(old_strategy_size);
			regret_.resize(old_regret_size);
			throw;
		}
	}

	MultiwaySparseStorageMemorySnapshot MultiwaySparseRowStorage::memory_snapshot() const noexcept
	{
		MultiwaySparseStorageMemorySnapshot result;
		result.row_metadata.add_buffer(metadata_.size(), metadata_.capacity(), sizeof(MultiwaySparseRowMetadata));
		result.regret_cells.add_buffer(regret_.size(), regret_.capacity(), sizeof(double));
		result.strategy_cells.add_buffer(strategy_sum_.size(), strategy_sum_.capacity(), sizeof(double));
		result.total = result.row_metadata;
		result.total.accumulate(result.regret_cells);
		result.total.accumulate(result.strategy_cells);
		result.total.grouping_only = true;
		return result;
	}

	void MultiwaySparseRowStorage::apply_delta(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		std::uint8_t action,
		double regret,
		double strategy_sum)
	{
		const auto* row = metadata(infoset);
		if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count || !std::isfinite(regret) || !std::isfinite(strategy_sum))
		{
			throw std::invalid_argument("multiway delta does not match an admitted finite row cell");
		}
		const auto index = row->regret_offset + static_cast<std::size_t>(action) * row->shape.bucket_count + bucket;
		const auto updated_regret = static_cast<double>(regret_[index]) + regret;
		const auto updated_strategy = static_cast<double>(strategy_sum_[index]) + strategy_sum;
		if (!std::isfinite(updated_regret) || !std::isfinite(updated_strategy) || (regret != 0.0 && updated_regret == regret_[index]) || (strategy_sum != 0.0 && updated_strategy == strategy_sum_[index]))
		{
			throw std::overflow_error("multiway sparse delta would lose a nonzero Float64 update");
		}
		regret_[index] = updated_regret;
		strategy_sum_[index] = updated_strategy;
	}

	MultiwayWorkerDeltaStream::MultiwayWorkerDeltaStream(std::size_t worker_index, std::size_t capacity)
		: worker_index_(worker_index), capacity_(capacity)
	{
		deltas_.reserve(capacity_);
	}

	bool MultiwayWorkerDeltaStream::try_append(const MultiwayWorkerDelta& delta) noexcept
	{
		if (deltas_.size() >= capacity_)
		{
			capacity_exhausted_ = true;
			return false;
		}
		if (!delta_is_finite(delta))
			return false;
		deltas_.push_back(delta);
		return true;
	}

	void MultiwayWorkerDeltaStream::rewind(std::size_t size) noexcept
	{
		if (size < deltas_.size())
			deltas_.resize(size);
	}

	void MultiwayWorkerDeltaStream::sort_fixed_order() noexcept
	{
		std::sort(deltas_.begin(), deltas_.end(), delta_less);
	}

	bool MultiwayWorkerDeltaStream::is_fixed_order() const noexcept
	{
		return std::is_sorted(deltas_.begin(), deltas_.end(), delta_less);
	}

	MultiwayMemoryComponent MultiwayWorkerDeltaStream::memory_snapshot() const noexcept
	{
		MultiwayMemoryComponent result;
		result.add_buffer(deltas_.size(), deltas_.capacity(), sizeof(MultiwayWorkerDelta));
		return result;
	}

	MultiwaySolveResult::MultiwaySolveResult(
		MultiwayRootPolicy root_policy,
		std::vector<Value> root_values,
		MultiwaySolveDiagnostics diagnostics)
		: root_policy_(std::move(root_policy)), root_values_(std::move(root_values)), diagnostics_(std::move(diagnostics)) {}

	MultiwaySolverCoordinator::MultiwaySolverCoordinator(const MultiwaySolveRequest& request)
		: request_(request), storage_(request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::Float64Reference ? request.limits().max_sparse_rows : 0U, request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::Float64Reference ? checked_sparse_value_capacity(request) : 0U), public_catalog_(request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32 ? 0U : request.limits().max_public_states)
	{
		if (request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::CompactInt32)
		{
			compact_storage_ = std::make_unique<MultiwayCompactStorage>(
				request.limits().max_sparse_rows, checked_sparse_value_capacity(request), request.limits().compact_page_width);
		}
		if (request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			constexpr std::uint64_t kExpandedRowIndexCacheThresholdBytes = 80U * 1024U * 1024U;
			constexpr std::uint64_t kExpandedRowIndexCacheBytes = 16U * 1024U * 1024U;
			constexpr std::uint64_t kDefaultRowIndexCacheBytes = 512U;
			const auto& external = request.limits().external_storage;
			const auto row_index_cache_bytes = external.row_page_cache_bytes >= kExpandedRowIndexCacheThresholdBytes
				? kExpandedRowIndexCacheBytes
				: kDefaultRowIndexCacheBytes;
			if (external.row_page_cache_bytes <= row_index_cache_bytes)
				throw std::invalid_argument("external row/page cache is below the bounded row-index requirement");
			auto catalog_config = external_component_config(external, "catalog",
				external.public_catalog_cache_bytes);
			auto row_config = external_component_config(external, "rows",
				row_index_cache_bytes);
			auto page_config = external_component_config(external, "pages",
				external.row_page_cache_bytes - row_index_cache_bytes);
			const auto ancestry_depth = std::min<std::size_t>(
				request.limits().max_public_states, 4096U);
			const auto maximum_page_id = multiway_external_page_lookup_max_page_id(
				static_cast<std::uint64_t>(request.limits().max_sparse_rows),
				static_cast<std::uint64_t>(checked_sparse_value_capacity(request)),
				request.limits().compact_page_width);
			external_storage_ = std::make_unique<MultiwayExternalTrainingStorage>(
				external.directory, std::move(catalog_config), std::move(row_config),
				std::move(page_config), request.limits().compact_page_width,
				external.row_page_cache_bytes - row_index_cache_bytes,
				ancestry_depth, external.synchronous_durability, maximum_page_id);
		}
		merge_stream_views_.reserve(request.limits().worker_count);
		merge_stream_cursors_.reserve(request.limits().worker_count);
		merge_tournament_.reserve(request.limits().worker_count);
		// Only the Float64 reference merge buffers merged cells. Compact backends
		// update rows in place and must not reserve graph-sized pending scratch.
		if (request.limits().storage_backend == MultiwaySolverLimits::StorageBackend::Float64Reference)
			pending_merge_cells_.reserve(request.limits().max_sparse_values);
		admission_processed_states_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_new_states_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_new_rows_.reserve(std::min<std::size_t>(256U, request.limits().max_sparse_rows));
		admission_new_row_streets_.reserve(std::min<std::size_t>(256U, request.limits().max_sparse_rows));
		admission_row_state_indexes_.reserve(std::min<std::size_t>(256U, request.limits().max_sparse_rows));
		admission_state_menu_keys_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_state_action_counts_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_state_streets_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_state_current_players_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_state_next_node_kinds_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		admission_state_menu_handles_.reserve(std::min<std::size_t>(256U, request.limits().max_public_states));
		if (external_storage_ != nullptr)
			external_storage_->initialize_root(request_.root().public_state);
		else
			admit_public_state(request_.root().public_state);
	}

	MultiwayCoordinatorMemorySnapshot MultiwaySolverCoordinator::memory_snapshot() const noexcept
	{
		MultiwayCoordinatorMemorySnapshot result;
		result.merge_scratch.add_buffer(merge_stream_views_.size(), merge_stream_views_.capacity(), sizeof(const MultiwayWorkerDeltaStream*));
		result.merge_scratch.add_buffer(merge_stream_cursors_.size(), merge_stream_cursors_.capacity(), sizeof(std::size_t));
		result.merge_scratch.add_buffer(merge_tournament_.size(), merge_tournament_.capacity(), sizeof(std::size_t));
		result.merge_scratch.add_buffer(pending_merge_cells_.size(), pending_merge_cells_.capacity(), sizeof(PendingMergeCell));
		result.admission_scratch.add_buffer(admission_processed_states_.size(), admission_processed_states_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_new_states_.size(), admission_new_states_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_existing_nodes_.size(), admission_existing_nodes_.capacity(), sizeof(const MultiwayPackedPublicNode*));
		result.admission_scratch.add_buffer(admission_state_fixed_states_.size(), admission_state_fixed_states_.capacity(), sizeof(games::multiway::MultiwayFixedState));
		result.admission_scratch.add_buffer(admission_new_rows_.size(), admission_new_rows_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_new_row_streets_.size(), admission_new_row_streets_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_row_state_indexes_.size(), admission_row_state_indexes_.capacity(), sizeof(std::size_t));
		result.admission_scratch.add_buffer(admission_state_menu_keys_.size(), admission_state_menu_keys_.capacity(), sizeof(std::uint64_t));
		result.admission_scratch.add_buffer(admission_state_action_counts_.size(), admission_state_action_counts_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_state_streets_.size(), admission_state_streets_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_state_current_players_.size(), admission_state_current_players_.capacity(), sizeof(core::PlayerId));
		result.admission_scratch.add_buffer(admission_state_next_node_kinds_.size(), admission_state_next_node_kinds_.capacity(), sizeof(std::uint8_t));
		result.admission_scratch.add_buffer(admission_state_menu_handles_.size(), admission_state_menu_handles_.capacity(), sizeof(std::uint32_t));
		result.total = result.merge_scratch;
		result.total.accumulate(result.admission_scratch);
		result.total.grouping_only = true;
		return result;
	}

	void MultiwaySolverCoordinator::admit_public_state(const MultiwayPublicStateDescriptor& state)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admit_public_state");
		if (external_storage_ != nullptr)
		{
			admit_prepared_batch({ state }, { 0U }, {});
			return;
		}
		{
			std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
			if (admission_diagnostics_ != nullptr)
				++admission_diagnostics_->lock_acquisitions;
			if (public_catalog_.contains(state.id))
			{
				const auto* existing = public_state(state.id);
				if (!public_state_candidates_compatible(*existing, state))
				{
					throw std::invalid_argument("multiway public state id was admitted with conflicting data");
				}
				return;
			}
		}
		const auto lock_wait_start = std::chrono::steady_clock::now();
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->lock_acquisitions;
		diagnostics_.coordinator_lock_wait_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - lock_wait_start)
				.count());
		validate_public_state_descriptor(state);
		if (public_catalog_.contains(state.id))
		{
			const auto* existing = public_state(state.id);
			if (!public_state_candidates_compatible(*existing, state))
				throw_conflicting_public_state_descriptors(*existing, state);
			return;
		}
		if (public_catalog_.size() >= request_.limits().max_public_states)
		{
			throw std::length_error("multiway public-state admission exceeds configured capacity");
		}
		if (state.parent_id.value != 0)
		{
			const auto* parent = public_state(state.parent_id);
			if (parent == nullptr)
			{
				throw std::invalid_argument("multiway public state parent must be admitted first");
			}
			validate_public_state_child_transition(request_.root(), *parent, state, nullptr);
		}
		else if (state.id != request_.root().public_state.id)
		{
			throw std::invalid_argument("multiway coordinator admits only its immutable root without a parent edge");
		}
		public_catalog_.admit(
			state,
			admission_diagnostics_ == nullptr
				? nullptr
				: &admission_diagnostics_->catalog_index_insertion_nanoseconds,
			admission_diagnostics_);
		++diagnostics_.public_states_admitted;
	}

	bool MultiwaySolverCoordinator::public_state_candidates_compatible(
		const MultiwayPublicStateDescriptor& left,
		const MultiwayPublicStateDescriptor& right) noexcept
	{
		return same_public_state_descriptor(left, right) || (same_public_state_payload(left, right) && equivalent_chance_transition_edge(left, right));
	}

	void MultiwaySolverCoordinator::admit_infoset_row(const MultiwaySparseRowShape& shape)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.admit_infoset_row");
		if (external_storage_ != nullptr)
		{
			admit_prepared_batch({}, {}, { shape });
			return;
		}
		{
			std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
			if (admission_diagnostics_ != nullptr)
				++admission_diagnostics_->lock_acquisitions;
			const auto* existing = compact_storage_ != nullptr
				? compact_storage_->metadata(shape.infoset)
				: storage_.metadata(shape.infoset);
			if (existing != nullptr)
			{
				if (existing->shape.bucket_count != shape.bucket_count || existing->shape.action_count != shape.action_count)
				{
					throw std::invalid_argument("multiway sparse row shape conflicts");
				}
				return;
			}
		}
		const auto lock_wait_start = std::chrono::steady_clock::now();
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->lock_acquisitions;
		diagnostics_.coordinator_lock_wait_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - lock_wait_start)
				.count());
		const auto compact = compact_storage_ != nullptr;
		const auto existed = compact ? compact_storage_->has_row(shape.infoset) : storage_.has_row(shape.infoset);
		const auto row_count = compact ? compact_storage_->row_count() : storage_.row_count();
		if (!existed && row_count >= request_.limits().max_sparse_rows)
		{
			throw std::length_error("multiway sparse row admission exceeds configured capacity");
		}
		const auto* state = public_state(shape.infoset.public_state);
		if (state == nullptr || shape.infoset.seat < 0 || static_cast<std::size_t>(shape.infoset.seat) >= request_.root().seat_order.size())
		{
			throw std::invalid_argument("multiway row must belong to an admitted per-seat public infoset");
		}
		const auto betting_state = make_multiway_fixed_state(state->betting);
		if (betting_state.next_node_kind() != MultiwayNextNodeKind::BettingDecision || shape.infoset.seat != state->betting.current_player)
		{
			throw std::invalid_argument("multiway row must belong to its public decision owner");
		}
		if (shape.action_count != state->legal_actions.size())
		{
			throw std::invalid_argument("multiway row action count must match its public action menu");
		}
		if (shape.infoset == request_.root().root_infoset && request_.root().root_bucket >= shape.bucket_count)
		{
			throw std::invalid_argument("multiway root bucket must fit its admitted sparse row");
		}
		switch (state->betting.street)
		{
			case Street::Preflop:
				++diagnostics_.street_visits[0U];
				break;
			case Street::Flop:
				++diagnostics_.street_visits[1U];
				break;
			case Street::Turn:
				++diagnostics_.street_visits[2U];
				break;
			case Street::River:
				++diagnostics_.street_visits[3U];
				break;
			case Street::Showdown:
				break;
		}
		const auto row_admission_started = std::chrono::steady_clock::now();
		if (compact)
			compact_storage_->admit_row(shape);
		else
			storage_.admit_row(shape);
		if (admission_diagnostics_ != nullptr)
			admission_diagnostics_->row_admission_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - row_admission_started)
					.count());
		if (!existed)
		{
			++diagnostics_.sparse_rows_admitted;
			switch (state->board.size())
			{
				case 0U:
					++diagnostics_.preflop_rows_admitted;
					break;
				case 3U:
					++diagnostics_.flop_rows_admitted;
					break;
				case 4U:
					++diagnostics_.turn_rows_admitted;
					break;
				case 5U:
					++diagnostics_.river_rows_admitted;
					break;
				default:
					break;
			}
		}
	}

	bool MultiwaySolverCoordinator::has_infoset_row(MultiwayInfosetId infoset) const
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		return has_infoset_row_epoch(infoset);
	}

	bool MultiwaySolverCoordinator::has_infoset_row_epoch(MultiwayInfosetId infoset) const
	{
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding binding;
			return external_storage_->compact_storage().lookup_row(infoset, binding);
		}
		return compact_storage_ != nullptr ? compact_storage_->has_row(infoset) : storage_.has_row(infoset);
	}

	void MultiwaySolverCoordinator::regret_matched_strategy_into(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		Probability* output,
		std::size_t output_size) const
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		if (compact_storage_ != nullptr)
		{
			compact_storage_->regret_matched_strategy_into(infoset, bucket, output, output_size);
			return;
		}
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding row;
			if (!external_storage_->compact_storage().lookup_row(infoset, row))
				throw std::out_of_range("multiway external strategy row is unavailable");
			external_storage_->compact_storage().regret_matched_strategy_into(row, bucket, output, output_size);
			return;
		}
		storage_.regret_matched_strategy_into(infoset, bucket, output, output_size);
	}

	std::size_t MultiwaySparseRowStorage::scale_and_prune_regrets(
		double factor, double threshold, double regret_floor) noexcept
	{
		if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
			return 0U;
		std::size_t count = 0U;
		for (auto& regret : regret_)
		{
			regret *= factor;
			if (regret < threshold)
			{
				regret = std::max(regret_floor, threshold);
				++count;
			}
		}
		return count;
	}

	bool MultiwaySolverCoordinator::action_below_regret(
		MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
		double threshold) const
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding row;
			if (!external_storage_->compact_storage().lookup_row(infoset, row))
				return false;
			return external_storage_->compact_storage().action_below_regret(row, bucket, action, threshold);
		}
		return compact_storage_ != nullptr
			? compact_storage_->action_below_regret(infoset, bucket, action, threshold)
			: storage_.action_below_regret(infoset, bucket, action, threshold);
	}

	MultiwayRootPolicy MultiwaySolverCoordinator::export_root_policy() const
	{
		const auto& root = request_.root();
		MultiwayRootPolicy result;
		result.public_state = root.public_state.id;
		result.infoset = root.root_infoset;
		result.bucket = root.root_bucket;

		std::vector<Probability> probabilities;
		if (compact_storage_ != nullptr && compact_storage_->has_row(root.root_infoset))
		{
			probabilities = compact_storage_->average_strategy(root.root_infoset, root.root_bucket);
		}
		else if (storage_.has_row(root.root_infoset))
		{
			probabilities = storage_.average_strategy(root.root_infoset, root.root_bucket);
		}
		else
		{
			const auto uniform = 1.0 / static_cast<double>(root.public_state.legal_actions.size());
			probabilities.assign(root.public_state.legal_actions.size(), uniform);
		}
		result.actions.reserve(probabilities.size());
		for (std::size_t action = 0; action < probabilities.size(); ++action)
		{
			result.actions.push_back({ root.public_state.legal_actions[action], probabilities[action] });
		}
		return result;
	}

	MultiwayRootPolicy MultiwaySolverCoordinator::export_root_current_policy() const
	{
		const auto& root = request_.root();
		MultiwayRootPolicy result;
		result.public_state = root.public_state.id;
		result.infoset = root.root_infoset;
		result.bucket = root.root_bucket;
		std::vector<Probability> probabilities;
		if (compact_storage_ != nullptr && compact_storage_->has_row(root.root_infoset))
		{
			probabilities = compact_storage_->regret_matched_strategy(root.root_infoset, root.root_bucket);
		}
		else if (storage_.has_row(root.root_infoset))
		{
			probabilities = storage_.regret_matched_strategy(root.root_infoset, root.root_bucket);
		}
		else
		{
			probabilities.assign(root.public_state.legal_actions.size(),
				1.0 / static_cast<double>(root.public_state.legal_actions.size()));
		}
		result.actions.reserve(probabilities.size());
		for (std::size_t action = 0; action < probabilities.size(); ++action)
		{
			result.actions.push_back({ root.public_state.legal_actions[action], probabilities[action] });
		}
		return result;
	}

	std::vector<double> MultiwaySolverCoordinator::export_root_strategy_sums() const
	{
		const auto& root = request_.root();
		if (!storage_.has_row(root.root_infoset))
		{
			return std::vector<double>(root.public_state.legal_actions.size(), 0.0);
		}
		return storage_.strategy_sums(root.root_infoset, root.root_bucket);
	}

	MultiwayRootPolicy MultiwaySolverCoordinator::export_root_policy_since(
		const std::vector<double>& baseline_strategy_sums) const
	{
		const auto& root = request_.root();
		const auto sums = export_root_strategy_sums();
		if (baseline_strategy_sums.size() != sums.size())
		{
			throw std::invalid_argument("multiway root policy baseline has an incompatible action count");
		}
		MultiwayRootPolicy result;
		result.public_state = root.public_state.id;
		result.infoset = root.root_infoset;
		result.bucket = root.root_bucket;
		double total = 0.0;
		std::vector<double> values(sums.size(), 0.0);
		for (std::size_t action = 0; action < sums.size(); ++action)
		{
			values[action] = sums[action] - baseline_strategy_sums[action];
			if (!std::isfinite(values[action]) || values[action] < -1e-12)
			{
				throw std::logic_error("multiway root policy baseline exceeds accumulated strategy mass");
			}
			if (values[action] < 0.0)
				values[action] = 0.0;
			total += values[action];
		}
		if (total == 0.0)
		{
			values.assign(values.size(), 1.0 / static_cast<double>(values.size()));
		}
		else
		{
			for (auto& value : values)
				value /= total;
		}
		result.actions.reserve(values.size());
		for (std::size_t action = 0; action < values.size(); ++action)
		{
			result.actions.push_back({ root.public_state.legal_actions[action], values[action] });
		}
		return result;
	}

	void MultiwaySolverCoordinator::scale_regrets(double factor)
	{
		if (external_storage_ != nullptr)
		{
			std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
			const double values[] = { factor };
			const auto fingerprint = external_regret_batch_fingerprint(0x5343414c45524547ULL, values, 1U);
			(void)commit_external_regret_batch(*external_storage_, fingerprint,
				[factor](MultiwayExternalCompactStorage& storage) {
					storage.scale_regrets(factor);
					return 0U;
				});
			return;
		}
		if (compact_storage_ != nullptr)
		{
			compact_storage_->scale_regrets(factor);
			return;
		}
		storage_.scale_regrets(factor);
	}

	std::size_t MultiwaySolverCoordinator::scale_and_prune_regrets(
		double factor, double threshold, double regret_floor)
	{
		if (external_storage_ != nullptr)
		{
			std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
			const double values[] = { factor, threshold, regret_floor };
			const auto fingerprint = external_regret_batch_fingerprint(0x5343414c4550524eULL, values, 3U);
			return commit_external_regret_batch(*external_storage_, fingerprint,
				[factor, threshold, regret_floor](MultiwayExternalCompactStorage& storage) {
					return storage.scale_and_prune_regrets(factor, threshold, regret_floor);
				});
		}
		if (compact_storage_ != nullptr)
		{
			return compact_storage_->scale_and_prune_regrets(factor, threshold, regret_floor);
		}
		return storage_.scale_and_prune_regrets(factor, threshold, regret_floor);
	}

	void MultiwaySolverCoordinator::set_external_publication_observer(
		MultiwayExternalPublicationObserver observer, void* context)
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		if (external_storage_ == nullptr)
			throw std::logic_error("publication observer requires external storage");
		external_storage_->set_publication_observer(observer, context);
	}

	MultiwaySolverCoordinator::~MultiwaySolverCoordinator() = default;

	void MultiwaySolverCoordinator::record_terminal_visit() noexcept
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		++diagnostics_.terminal_visits;
	}

	void MultiwaySolverCoordinator::record_leaf_visit() noexcept
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		++diagnostics_.leaf_visits;
	}

	void MultiwaySolverCoordinator::record_missing_lookup() noexcept
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		++diagnostics_.missing_lookup_requests;
	}

	void MultiwaySolverCoordinator::merge_traversal_counters(
		std::uint64_t terminal_visits,
		std::uint64_t leaf_visits,
		std::uint64_t missing_lookup_requests) noexcept
	{
		std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
		diagnostics_.terminal_visits += terminal_visits;
		diagnostics_.leaf_visits += leaf_visits;
		diagnostics_.missing_lookup_requests += missing_lookup_requests;
	}

	void MultiwaySolverCoordinator::regret_matched_strategy_into_epoch(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		Probability* output,
		std::size_t output_size) const
	{
		if (compact_storage_ != nullptr)
		{
			compact_storage_->regret_matched_strategy_into(infoset, bucket, output, output_size);
			return;
		}
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding row;
			if (!external_storage_->compact_storage().lookup_row(infoset, row))
				throw std::out_of_range("multiway external strategy row is unavailable");
			external_storage_->compact_storage().regret_matched_strategy_into(row, bucket, output, output_size);
			return;
		}
		storage_.regret_matched_strategy_into(infoset, bucket, output, output_size);
	}

	bool MultiwaySolverCoordinator::action_below_regret_epoch(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		std::uint8_t action,
		double threshold) const
	{
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding row;
			if (!external_storage_->compact_storage().lookup_row(infoset, row))
				return false;
			return external_storage_->compact_storage().action_below_regret(row, bucket, action, threshold);
		}
		return compact_storage_ != nullptr
			? compact_storage_->action_below_regret(infoset, bucket, action, threshold)
			: storage_.action_below_regret(infoset, bucket, action, threshold);
	}

	void MultiwaySolverCoordinator::actions_below_regret_locked(
		MultiwayInfosetId infoset, std::uint32_t bucket, double threshold,
		bool* output, std::size_t output_size) const
	{
		if (output == nullptr || output_size == 0U || output_size > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("multiway regret threshold output is invalid");
		if (external_storage_ != nullptr)
		{
			MultiwayExternalRowBinding row;
			if (!external_storage_->compact_storage().lookup_row(infoset, row))
			{
				std::fill(output, output + output_size, false);
				return;
			}
			if (output_size != row.action_count())
				throw std::invalid_argument("multiway external regret threshold output size mismatch");
			auto pinned = external_storage_->compact_storage().pin_bucket(row, bucket, false);
			for (std::size_t action = 0U; action < output_size; ++action)
				output[action] = external_storage_->compact_storage().action_below_regret(
					pinned, static_cast<std::uint8_t>(action), threshold);
			external_storage_->compact_storage().unpin_bucket(pinned);
			return;
		}
		for (std::size_t action = 0U; action < output_size; ++action)
			output[action] = compact_storage_ != nullptr
				? compact_storage_->action_below_regret(infoset, bucket,
					  static_cast<std::uint8_t>(action), threshold)
				: storage_.action_below_regret(infoset, bucket,
					  static_cast<std::uint8_t>(action), threshold);
	}

	void MultiwaySolverCoordinator::actions_below_regret_epoch(
		MultiwayInfosetId infoset, std::uint32_t bucket, double threshold,
		bool* output, std::size_t output_size) const
	{
		actions_below_regret_locked(infoset, bucket, threshold, output, output_size);
	}

	void MultiwaySolverCoordinator::actions_below_regret(
		MultiwayInfosetId infoset, std::uint32_t bucket, double threshold,
		bool* output, std::size_t output_size) const
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		actions_below_regret_locked(infoset, bucket, threshold, output, output_size);
	}

	std::size_t MultiwaySolverCoordinator::prune_negative_regrets(
		double threshold, double regret_floor)
	{
		if (external_storage_ != nullptr)
		{
			std::unique_lock<std::shared_mutex> lock(traversal_mutex_);
			const double values[] = { threshold, regret_floor };
			const auto fingerprint = external_regret_batch_fingerprint(0x5052554e45524547ULL, values, 2U);
			return commit_external_regret_batch(*external_storage_, fingerprint,
				[threshold, regret_floor](MultiwayExternalCompactStorage& storage) {
					return storage.prune_negative_regrets(threshold, regret_floor);
				});
		}
		if (compact_storage_ != nullptr)
			return compact_storage_->prune_negative_regrets(threshold, regret_floor);
		return storage_.prune_negative_regrets(threshold, regret_floor);
	}

	const MultiwayPublicStateDescriptor* MultiwaySolverCoordinator::public_state(
		MultiwayPublicStateId id) const
	{
		thread_local MultiwayPublicStateDescriptor scratch;
		const auto found = external_storage_ != nullptr
			? external_storage_->catalog().reconstruct(id, scratch)
			: public_catalog_.reconstruct(id, scratch);
		if (!found)
			return nullptr;
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->descriptor_reconstructions;
		return &scratch;
	}

	const MultiwayPublicStateDescriptor* MultiwaySolverCoordinator::find_public_state(
		MultiwayPublicStateId id) const noexcept
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->lock_acquisitions;
		try
		{
			return public_state(id);
		}
		catch (...)
		{
			return nullptr;
		}
	}

	bool MultiwaySolverCoordinator::has_public_state(MultiwayPublicStateId id) const
	{
		std::shared_lock<std::shared_mutex> lock(traversal_mutex_);
		if (admission_diagnostics_ != nullptr)
			++admission_diagnostics_->lock_acquisitions;
		return has_public_state_epoch(id);
	}

	bool MultiwaySolverCoordinator::has_public_state_epoch(MultiwayPublicStateId id) const
	{
		return external_storage_ != nullptr
			? external_storage_->catalog().contains(id)
			: public_catalog_.contains(id);
	}

	MultiwaySolverCoordinator::ExportLease MultiwaySolverCoordinator::acquire_export_lease() const
	{
		return ExportLease(*this);
	}

	MultiwaySolverCoordinator::ExportLease MultiwaySolverCoordinator::acquire_checkpoint_lease() const
	{
		return ExportLease(*this);
	}

} // namespace texas::solver::multiway
