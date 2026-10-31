#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalBatchTransaction::MultiwayExternalBatchTransaction(
		std::filesystem::path directory, bool synchronous_durability)
		: directory_(std::move(directory)), synchronous_durability_(synchronous_durability)
	{
		if (directory_.empty())
			throw std::invalid_argument("external batch transaction directory is empty");
		std::error_code error;
		std::filesystem::create_directories(directory_, error);
		if (error || !std::filesystem::is_directory(directory_, error) || error)
			throw std::runtime_error("external batch transaction directory is unavailable");
		recover_checkpoint_restore(directory_, synchronous_durability_);
		if (!load_multiway_external_generation(directory_, current_))
			current_ = {};
		recover_pending_batch();
	}

	void MultiwayExternalBatchTransaction::begin_batch(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint)
	{
		if (batch_open_ || batch_number == 0U || batch_number <= current_.committed_batch_number || current_.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::invalid_argument("external batch transaction identity is invalid");
		previous_manifest_flags_ = 0U;
		previous_component_generations_ = {};
		previous_component_batch_numbers_ = {};
		const auto wal = directory_ / "TRAINING_BATCH.wal";
		const auto temp = directory_ / "TRAINING_BATCH.tmp";
		if (std::filesystem::exists(wal) || std::filesystem::exists(temp))
			throw std::runtime_error("external batch transaction has an unrecovered journal");
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto component = directory_ / kComponentNames[index];
			std::error_code error;
			std::filesystem::create_directories(component, error);
			if (error)
				throw std::runtime_error("external batch component directory cannot be created");
			const auto info = read_component_manifest(component);
			previous_component_generations_[index] = info.generation;
			previous_component_batch_numbers_[index] = info.last_batch_number;
			const auto backup = directory_ / kBackupNames[index];
			if (std::filesystem::exists(backup))
			{
				std::filesystem::remove(backup, error);
				if (error)
					throw std::runtime_error("stale external component backup cannot be removed");
			}
			const auto current = component / "CURRENT";
			if (info.exists)
			{
				std::filesystem::copy_file(current, backup);
				if (synchronous_durability_)
					sync_file_path(backup);
				previous_manifest_flags_ |= (1U << index);
			}
		}
		if (synchronous_durability_)
			sync_directory_path(directory_);
		try
		{
			write_transaction_wal(directory_, { previous_manifest_flags_, batch_number, merge_fingerprint },
				synchronous_durability_);
		}
		catch (...)
		{
			std::error_code ignored;
			for (const auto* name : kBackupNames)
				std::filesystem::remove(directory_ / name, ignored);
			std::filesystem::remove(temp, ignored);
			std::filesystem::remove(wal, ignored);
			previous_manifest_flags_ = 0U;
			throw;
		}
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	void MultiwayExternalBatchTransaction::set_merge_fingerprint(
		std::uint64_t merge_fingerprint)
	{
		if (!batch_open_ || merge_fingerprint == 0U || (merge_fingerprint_ != 0U && merge_fingerprint_ != merge_fingerprint))
			throw std::invalid_argument("external batch merge fingerprint cannot be finalized");
		if (merge_fingerprint_ == merge_fingerprint)
			return;
		write_transaction_wal(directory_,
			{ previous_manifest_flags_, batch_number_, merge_fingerprint },
			synchronous_durability_, true);
		merge_fingerprint_ = merge_fingerprint;
	}

	void MultiwayExternalBatchTransaction::commit_batch(
		const MultiwayExternalGenerationManifest& manifest)
	{
		if (!batch_open_ || manifest.generation != current_.generation + 1U || manifest.committed_batch_number != batch_number_ || manifest.merge_fingerprint != merge_fingerprint_)
			throw std::invalid_argument("external batch commit does not match its begin record");
		const std::array<std::uint64_t, 3U> expected_generations = {
			manifest.catalog_generation, manifest.row_generation, manifest.page_generation
		};
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto info = read_component_manifest(directory_ / kComponentNames[index]);
			const bool generation_changed = info.generation != previous_component_generations_[index];
			const bool page_component = index == 2U;
			const auto previous_batch = previous_component_batch_numbers_[index];
			const bool batch_number_valid = info.last_batch_number >= previous_batch && (page_component || info.last_batch_number <= batch_number_) && (!generation_changed || info.last_batch_number == previous_batch || (page_component ? info.last_batch_number > previous_batch : info.last_batch_number == batch_number_)) && (generation_changed || info.last_batch_number == previous_batch);
			if (info.generation != expected_generations[index] || !batch_number_valid)
				throw std::invalid_argument("external component generation does not match the batch commit");
		}
		publish_multiway_external_generation(directory_, manifest, synchronous_durability_);
		current_ = manifest;
		batch_open_ = false;
		clear_pending_batch();
	}

	void MultiwayExternalBatchTransaction::abort_batch()
	{
		if (!batch_open_)
			return;
		rollback_pending_batch(previous_manifest_flags_);
		batch_open_ = false;
		batch_number_ = 0U;
		merge_fingerprint_ = 0U;
		previous_manifest_flags_ = 0U;
		if (!load_multiway_external_generation(directory_, current_))
			current_ = {};
	}

	void MultiwayExternalBatchTransaction::recover_pending_batch()
	{
		const auto wal = directory_ / "TRAINING_BATCH.wal";
		if (!std::filesystem::exists(wal))
		{
			const auto temp = directory_ / "TRAINING_BATCH.tmp";
			if (std::filesystem::exists(temp))
			{
				std::error_code error;
				std::filesystem::remove(temp, error);
				if (error)
					throw std::runtime_error("incomplete external transaction journal cannot be removed");
			}
			return;
		}
		const auto pending = read_transaction_wal(wal);
		if (pending.batch_number <= current_.committed_batch_number)
		{
			if (pending.batch_number == current_.committed_batch_number && pending.merge_fingerprint != current_.merge_fingerprint)
				throw std::invalid_argument("external transaction journal conflicts with the committed manifest");
			clear_pending_batch();
			return;
		}
		batch_number_ = pending.batch_number;
		merge_fingerprint_ = pending.merge_fingerprint;
		previous_manifest_flags_ = pending.previous_flags;
		rollback_pending_batch(pending.previous_flags);
	}

	void MultiwayExternalBatchTransaction::rollback_pending_batch(std::uint32_t previous_manifest_flags)
	{
		MultiwayExternalGenerationManifest committed{};
		const auto has_manifest = load_multiway_external_generation(directory_, committed);
		if (has_manifest && committed.committed_batch_number >= batch_number_ && committed.committed_batch_number != 0U)
		{
			if (committed.committed_batch_number == batch_number_ && committed.merge_fingerprint != merge_fingerprint_)
				throw std::invalid_argument("external batch journal conflicts with the published generation");
			current_ = committed;
			clear_pending_batch();
			return;
		}
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto component = directory_ / kComponentNames[index];
			std::error_code error;
			const auto current = component / "CURRENT";
			const auto backup = directory_ / kBackupNames[index];
			if ((previous_manifest_flags & (1U << index)) != 0U)
			{
				if (!std::filesystem::exists(backup))
					throw std::runtime_error("external transaction rollback manifest backup is missing");
				const auto restore_temp = component / "CURRENT.restore.tmp";
				if (std::filesystem::exists(restore_temp))
				{
					std::filesystem::remove(restore_temp, error);
					if (error)
						throw std::runtime_error("external transaction restore temporary cannot be cleared");
				}
				std::filesystem::copy_file(backup, restore_temp);
				if (synchronous_durability_)
					sync_file_path(restore_temp);
				texas::core::publish_atomic_replace(restore_temp, current,
					"external component manifest rollback failed",
					texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			}
			else
			{
				std::filesystem::remove(current, error);
				if (error)
					throw std::runtime_error("new external component manifest cannot be rolled back");
			}
			for (const auto* name : { "BATCH.wal", "CURRENT.tmp", "CURRENT.restore.tmp" })
			{
				std::filesystem::remove(component / name, error);
				if (error)
					throw std::runtime_error("external component transaction temporary cannot be removed");
			}
			if (synchronous_durability_)
				sync_directory_path(component);
		}
		std::error_code error;
		std::filesystem::remove(directory_ / "TRAINING_CURRENT.tmp", error);
		if (error)
			throw std::runtime_error("external training manifest temporary cannot be removed");
		current_ = {};
		(void)load_multiway_external_generation(directory_, current_);
		clear_pending_batch();
	}

	void MultiwayExternalBatchTransaction::clear_pending_batch()
	{
		std::error_code error;
		for (const auto* name : { "TRAINING_BATCH.wal", "TRAINING_BATCH.tmp" })
		{
			std::filesystem::remove(directory_ / name, error);
			if (error)
				throw std::runtime_error("external transaction journal cannot be removed");
		}
		for (const auto* name : kBackupNames)
		{
			std::filesystem::remove(directory_ / name, error);
			if (error)
				throw std::runtime_error("external transaction backup cannot be removed");
		}
		if (synchronous_durability_)
			sync_directory_path(directory_);
		batch_number_ = 0U;
		merge_fingerprint_ = 0U;
		previous_manifest_flags_ = 0U;
	}
} // namespace texas::solver::multiway
