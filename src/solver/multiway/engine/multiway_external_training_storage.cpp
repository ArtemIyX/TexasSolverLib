#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalTrainingStorage::MultiwayExternalTrainingStorage(
		std::filesystem::path directory, MultiwayExternalStorageConfig catalog_config,
		MultiwayExternalStorageConfig row_config, MultiwayExternalStorageConfig page_config,
		std::uint32_t page_width, std::uint64_t row_page_cache_bytes,
		std::size_t maximum_ancestry_depth, bool synchronous_durability,
		std::uint64_t maximum_page_id)
		: directory_(std::move(directory)), catalog_config_(std::move(catalog_config)), row_config_(std::move(row_config)), page_config_(std::move(page_config)), page_width_(page_width), row_page_cache_bytes_(row_page_cache_bytes), maximum_page_id_(maximum_page_id), maximum_ancestry_depth_(maximum_ancestry_depth), synchronous_durability_(synchronous_durability), transaction_(directory_, synchronous_durability)
	{
		if (directory_.empty() || page_width_ == 0U || row_page_cache_bytes_ == 0U || maximum_ancestry_depth_ == 0U || catalog_config_.directory.lexically_normal() != (directory_ / "catalog").lexically_normal() || row_config_.directory.lexically_normal() != (directory_ / "rows").lexically_normal() || page_config_.directory.lexically_normal() != (directory_ / "pages").lexically_normal() || catalog_config_.synchronous_durability != synchronous_durability_ || row_config_.synchronous_durability != synchronous_durability_ || page_config_.synchronous_durability != synchronous_durability_)
			throw std::invalid_argument("external training storage component paths or bounds are invalid");
		if (catalog_config_.disk_amplification_limit != row_config_.disk_amplification_limit || catalog_config_.disk_amplification_limit != page_config_.disk_amplification_limit)
			throw std::invalid_argument("external training components have different disk amplification limits");
		if (catalog_config_.disk_amplification_limit != 0U)
		{
			catalog_config_.disk_accounting_root = directory_;
			row_config_.disk_accounting_root = directory_;
			page_config_.disk_accounting_root = directory_;
			if (synchronous_durability_)
			{
				catalog_config_.aggregate_transaction_covers_append_recovery = true;
				row_config_.aggregate_transaction_covers_append_recovery = true;
				page_config_.aggregate_transaction_covers_append_recovery = true;
			}
		}
		open_components();
	}

	void MultiwayExternalTrainingStorage::open_components()
	{
		catalog_ = std::make_unique<MultiwayExternalPublicCatalog>(
			catalog_config_, maximum_ancestry_depth_);
		compact_ = std::make_unique<MultiwayExternalCompactStorage>(
			row_config_, page_config_, page_width_, row_page_cache_bytes_, maximum_page_id_);
	}

	void MultiwayExternalTrainingStorage::set_publication_observer(
		MultiwayExternalPublicationObserver observer, void* context)
	{
		if (batch_number_ != 0U)
			throw std::logic_error("publication observer requires a clean training boundary");
		catalog_config_.publication_observer = observer;
		catalog_config_.publication_observer_context = context;
		catalog_config_.publication_component = 0U;
		row_config_.publication_observer = observer;
		row_config_.publication_observer_context = context;
		row_config_.publication_component = 1U;
		page_config_.publication_observer = observer;
		page_config_.publication_observer_context = context;
		page_config_.publication_component = 2U;
		catalog_->set_publication_observer(observer, context);
		compact_->set_publication_observer(observer, context);
	}

	void MultiwayExternalTrainingStorage::begin_batch(std::uint64_t batch_number)
	{
		if (batch_number_ != 0U || batch_number == 0U)
			throw std::logic_error("external training storage batch state is invalid");
		batch_base_generations_ = { catalog_->generation(), compact_->row_generation(), compact_->page_generation() };
		transaction_.begin_batch(batch_number, 0U);
		batch_number_ = batch_number;
	}

	void MultiwayExternalTrainingStorage::initialize_root(
		const MultiwayPublicStateDescriptor& root)
	{
		if (root.id.value == 0U)
			throw std::invalid_argument("external training root identity is invalid");
		const auto& manifest = transaction_.current_manifest();
		MultiwayPublicStateDescriptor existing;
		if (manifest.generation != 0U)
		{
			if (manifest.public_state_count != catalog_->record_count() || manifest.row_generation != compact_->row_generation() || manifest.page_generation != compact_->page_generation() || compact_->next_page_id() == 0U || manifest.page_count != compact_->next_page_id() - 1U || !catalog_->reconstruct(root.id, existing) || !MultiwaySolverCoordinator::public_state_candidates_compatible(existing, root))
				throw std::invalid_argument("external training root does not match its committed catalog generation");
			return;
		}
		if (catalog_->record_count() != 0U || compact_->row_generation() != 0U || compact_->page_generation() != 0U || compact_->next_page_id() != 1U)
			throw std::invalid_argument("external training components exist without an aggregate generation");
		constexpr std::uint64_t root_seed_fingerprint = 0x524f4f5453454544ULL;
		begin_batch(1U);
		try
		{
			append_catalog(root_seed_fingerprint,
				{ MultiwayPublicCatalog::pack_node(root) }, { root });
			begin_page_batch();
			commit_batch(root_seed_fingerprint, 0U, 0U);
		}
		catch (...)
		{
			try
			{
				abort_batch();
			}
			catch (...)
			{
			}
			throw;
		}
	}

	std::uint64_t MultiwayExternalTrainingStorage::next_batch_number() const
	{
		const auto committed = transaction_.current_manifest().committed_batch_number;
		if (committed == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external training batch number overflows");
		return committed + 1U;
	}

	void MultiwayExternalTrainingStorage::append_catalog(std::uint64_t component_fingerprint,
		const std::vector<MultiwayPackedPublicNode>& nodes,
		const std::vector<MultiwayPublicStateDescriptor>& descriptors)
	{
		if (batch_number_ == 0U || component_fingerprint == 0U || nodes.size() != descriptors.size())
			throw std::invalid_argument("external training catalog batch is invalid");
		if (!nodes.empty())
			catalog_->append_batch(batch_number_, component_fingerprint, nodes, descriptors);
	}

	void MultiwayExternalTrainingStorage::append_discovered_catalog(
		std::uint64_t component_fingerprint,
		const std::vector<MultiwayPublicStateDescriptor>& states,
		const std::vector<std::uint8_t>& new_state_flags)
	{
		if (batch_number_ == 0U || component_fingerprint == 0U || states.size() != new_state_flags.size())
			throw std::invalid_argument("external training catalog discovery batch is invalid");
		catalog_->append_discovered_batch(batch_number_, component_fingerprint,
			states, new_state_flags);
	}

	void MultiwayExternalTrainingStorage::admit_rows(std::uint64_t component_fingerprint,
		const std::vector<MultiwaySparseRowShape>& rows)
	{
		if (batch_number_ == 0U || component_fingerprint == 0U)
			throw std::invalid_argument("external training row batch is invalid");
		compact_->admit_rows(batch_number_, component_fingerprint, rows);
	}

	void MultiwayExternalTrainingStorage::begin_page_batch()
	{
		if (batch_number_ == 0U || page_batch_open_)
			throw std::logic_error("external training page batch state is invalid");
		compact_->begin_batch(batch_number_, 0U);
		page_batch_open_ = true;
	}

	void MultiwayExternalTrainingStorage::compact_components()
	{
		if (batch_number_ == 0U || page_batch_open_)
			throw std::logic_error("external training compaction requires a clean pre-page batch boundary");
		if (catalog_config_.disk_amplification_limit != 0U)
		{
			catalog_->compact_batch_runs(batch_base_generations_[0]);
			compact_->compact_batch_runs(batch_base_generations_[1], batch_base_generations_[2]);
		}
		else
		{
			catalog_->compact(true);
			compact_->compact(true);
		}
	}

	void MultiwayExternalTrainingStorage::commit_batch(
		std::uint64_t merge_fingerprint, std::uint64_t row_count,
		std::uint64_t logical_value_count)
	{
		const auto commit_started = std::chrono::steady_clock::now();
		if (batch_number_ == 0U || !page_batch_open_ || merge_fingerprint == 0U)
			throw std::logic_error("external training batch is not ready to commit");
		compact_->set_merge_fingerprint(merge_fingerprint);
		compact_->commit_batch();
		if (catalog_config_.disk_amplification_limit != 0U)
		{
			catalog_->compact_batch_runs(batch_base_generations_[0]);
			compact_->compact_batch_runs(batch_base_generations_[1], batch_base_generations_[2]);
		}
		if (catalog_config_.disk_amplification_limit != 0U)
		{
			const auto disk_reserve_bytes = std::max({ catalog_config_.disk_reserve_bytes,
				row_config_.disk_reserve_bytes, page_config_.disk_reserve_bytes });
			// The batch WAL rewrite and aggregate manifest publication are separate
			// phases; the 112-byte manifest temporary is their larger overlap.
			check_aggregate_commit_disk_budget(directory_,
				catalog_config_.disk_amplification_limit, disk_reserve_bytes,
				kGenerationManifestBytes);
		}
		transaction_.set_merge_fingerprint(merge_fingerprint);
		const auto& current = transaction_.current_manifest();
		if (current.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external training generation overflows");
		if (current.logical_value_count == std::numeric_limits<std::uint64_t>::max())
			throw std::invalid_argument("legacy external generation has no exact value count");
		if (row_count < current.row_count || logical_value_count < current.logical_value_count || ((row_count == 0U) != (logical_value_count == 0U)))
			throw std::invalid_argument("external training row or value counts are inconsistent");
		MultiwayExternalGenerationManifest next{};
		next.generation = current.generation + 1U;
		next.committed_batch_number = batch_number_;
		next.merge_fingerprint = merge_fingerprint;
		next.catalog_generation = catalog_->generation();
		next.public_state_count = catalog_->record_count();
		next.row_generation = compact_->row_generation();
		next.row_count = row_count;
		next.page_generation = compact_->page_generation();
		next.page_count = compact_->next_page_id() - 1U;
		next.logical_value_count = logical_value_count;
		next.content_hash = content_hash();
		transaction_.commit_batch(next);
		aggregate_commit_nanoseconds_ = multiway_memory_saturating_add(
			aggregate_commit_nanoseconds_, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - commit_started).count()));
		batch_number_ = 0U;
		page_batch_open_ = false;
		if (catalog_config_.disk_amplification_limit != 0U)
		{
			catalog_->reclaim_orphans();
			compact_->reclaim_orphans();
		}
	}

	std::uint64_t MultiwayExternalTrainingStorage::content_hash() const
	{
		auto hash = kHashOffset;
		texas::core::fingerprint::append_u64(hash, catalog_->content_hash());
		texas::core::fingerprint::append_u64(hash, compact_->content_hash());
		return texas::core::fingerprint::finish(hash);
	}

	MultiwayExternalStorageTelemetry MultiwayExternalTrainingStorage::telemetry(bool include_disk_usage) const
	{
		MultiwayExternalStorageTelemetry result{};
		result.catalog = catalog_->stats();
		result.rows = compact_->row_index_stats();
		result.pages = compact_->page_store_stats();
		result.training_generation = transaction_.current_manifest().generation;
		result.committed_batch = transaction_.current_manifest().committed_batch_number;
		result.aggregate_commit_nanoseconds = aggregate_commit_nanoseconds_;
		add_storage_stats(result.stats, result.catalog);
		add_storage_stats(result.stats, result.rows);
		add_storage_stats(result.stats, result.pages);
		if (!include_disk_usage)
			return result;
		const auto catalog_usage = catalog_->disk_usage();
		const auto compact_usage = compact_->disk_usage();
		result.disk_usage.live_segment_bytes = checked_add(catalog_usage.live_segment_bytes,
			compact_usage.live_segment_bytes);
		result.disk_usage.retained_segment_bytes = checked_add(catalog_usage.retained_segment_bytes,
			compact_usage.retained_segment_bytes);
		return result;
	}

	void MultiwayExternalTrainingStorage::abort_batch()
	{
		if (batch_number_ == 0U)
			return;
		if (compact_ != nullptr)
			compact_->abort_batch();
		catalog_.reset();
		compact_.reset();
		transaction_.abort_batch();
		batch_number_ = 0U;
		page_batch_open_ = false;
		open_components();
	}
} // namespace texas::solver::multiway
