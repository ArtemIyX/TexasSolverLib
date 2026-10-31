#include "multiway_external_checkpoint_detail.hpp"

namespace texas::solver::multiway
{
	void publish_multiway_external_generation(const std::filesystem::path& directory,
		const MultiwayExternalGenerationManifest& manifest, bool synchronous_durability)
	{
		if (directory.empty() || manifest.generation == 0U || manifest.committed_batch_number == 0U || manifest.merge_fingerprint == 0U || ((manifest.row_count == 0U) != (manifest.logical_value_count == 0U)))
			throw std::invalid_argument("external generation manifest identity is invalid");
		std::vector<std::uint8_t> bytes{ 'T', 'X', 'M', 'W', 'G', 'E', 'N', '1' };
		append_u32(bytes, 2U);
		append_u32(bytes, 0U);
		append_u64(bytes, manifest.generation);
		append_u64(bytes, manifest.committed_batch_number);
		append_u64(bytes, manifest.merge_fingerprint);
		append_u64(bytes, manifest.catalog_generation);
		append_u64(bytes, manifest.public_state_count);
		append_u64(bytes, manifest.row_generation);
		append_u64(bytes, manifest.row_count);
		append_u64(bytes, manifest.page_generation);
		append_u64(bytes, manifest.page_count);
		append_u64(bytes, manifest.logical_value_count);
		append_u64(bytes, manifest.content_hash);
		auto checksum = kHashOffset;
		for (const auto byte : bytes)
			texas::core::fingerprint::append_u8(checksum, byte);
		append_u64(bytes, texas::core::fingerprint::finish(checksum));
		const auto temp = directory / "TRAINING_CURRENT.tmp";
		const auto current = directory / "TRAINING_CURRENT";
		if (std::filesystem::exists(temp))
			throw std::runtime_error("external training manifest temporary path already exists");
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("external training manifest cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output)
			throw std::runtime_error("external training manifest write failed");
		output.close();
		if (synchronous_durability)
		{
#ifdef _WIN32
			const auto handle = CreateFileW(temp.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				throw std::runtime_error("external training manifest cannot be opened for flush");
			const auto flushed = FlushFileBuffers(handle);
			CloseHandle(handle);
			if (!flushed)
				throw std::runtime_error("external training manifest flush failed");
#else
			const auto descriptor = open(temp.c_str(), O_RDONLY);
			if (descriptor < 0)
				throw std::runtime_error("external training manifest cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0)
				throw std::runtime_error("external training manifest flush failed");
#endif
		}
		texas::core::publish_atomic_replace(temp, current, "external training manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		if (synchronous_durability)
		{
#ifndef _WIN32
			const auto descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
			if (descriptor < 0)
				throw std::runtime_error("external training directory cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0)
				throw std::runtime_error("external training directory flush failed");
#endif
		}
	}

	bool load_multiway_external_generation(const std::filesystem::path& directory,
		MultiwayExternalGenerationManifest& manifest)
	{
		const auto path = directory / "TRAINING_CURRENT";
		std::error_code error;
		if (!std::filesystem::exists(path, error))
		{
			if (error)
				throw std::runtime_error("external training manifest cannot be inspected");
			return false;
		}
		const auto manifest_size = std::filesystem::file_size(path, error);
		if (error || (manifest_size != 104U && manifest_size != 112U))
			throw std::invalid_argument("external training manifest size is invalid");
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(manifest_size));
		std::ifstream input(path, std::ios::binary);
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		constexpr std::array<std::uint8_t, 8U> magic = { 'T', 'X', 'M', 'W', 'G', 'E', 'N', '1' };
		const auto schema = bytes.size() >= 12U ? read_u32(bytes.data() + 8U) : 0U;
		if (!input || !std::equal(magic.begin(), magic.end(), bytes.begin()) || (schema != 1U && schema != 2U) || (schema == 1U && bytes.size() != 104U) || (schema == 2U && bytes.size() != 112U) || read_u32(bytes.data() + 12U) != 0U)
			throw std::invalid_argument("external training manifest header is invalid");
		auto checksum = kHashOffset;
		for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
			texas::core::fingerprint::append_u8(checksum, bytes[index]);
		if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
			throw std::invalid_argument("external training manifest checksum is invalid");
		MultiwayExternalGenerationManifest decoded{};
		std::size_t cursor = 16U;
		const auto read_field = [&]() { const auto value = read_u64(bytes.data() + cursor); cursor += 8U; return value; };
		decoded.generation = read_field();
		decoded.committed_batch_number = read_field();
		decoded.merge_fingerprint = read_field();
		decoded.catalog_generation = read_field();
		decoded.public_state_count = read_field();
		decoded.row_generation = read_field();
		decoded.row_count = read_field();
		decoded.page_generation = read_field();
		decoded.page_count = read_field();
		if (schema >= 2U)
			decoded.logical_value_count = read_field();
		else if (decoded.row_count != 0U)
			decoded.logical_value_count = std::numeric_limits<std::uint64_t>::max();
		decoded.content_hash = read_field();
		if (cursor != bytes.size() - 8U || decoded.generation == 0U || decoded.committed_batch_number == 0U || decoded.merge_fingerprint == 0U || (schema >= 2U && ((decoded.row_count == 0U) != (decoded.logical_value_count == 0U))))
			throw std::invalid_argument("external training manifest identity is invalid");
		manifest = decoded;
		return true;
	}

	MultiwayExternalCheckpointManifest make_multiway_external_checkpoint_manifest(
		const std::filesystem::path& external_storage_directory,
		std::uint64_t model_identity_hash,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash,
		std::uint64_t schedule_hash,
		std::uint64_t completed_trajectories)
	{
		MultiwayExternalGenerationManifest generation{};
		std::array<std::uint64_t, 3U> component_generations{};
		std::array<std::uint32_t, 3U> component_payload_bytes{};
		std::array<std::uint64_t, 3U> component_last_batch_numbers{};
		if (external_storage_directory.empty() || model_identity_hash == 0U || workflow_identity_hash == 0U || storage_identity_hash == 0U || schedule_hash == 0U || !load_multiway_external_generation(external_storage_directory, generation) || generation.generation == 0U || generation.committed_batch_number == 0U || generation.merge_fingerprint == 0U)
			throw std::invalid_argument("external checkpoint capture identity is invalid");
		auto segments = load_checkpoint_segment_references(external_storage_directory,
			&component_generations, &component_payload_bytes, &component_last_batch_numbers);
		if (component_generations != std::array<std::uint64_t, 3U>{ generation.catalog_generation, generation.row_generation, generation.page_generation })
			throw std::invalid_argument("external checkpoint components do not match the published generation");

		MultiwayExternalCheckpointManifest checkpoint{};
		checkpoint.model_identity_hash = model_identity_hash;
		checkpoint.workflow_identity_hash = workflow_identity_hash;
		checkpoint.storage_identity_hash = storage_identity_hash;
		checkpoint.schedule_hash = schedule_hash;
		checkpoint.completed_trajectories = completed_trajectories;
		checkpoint.wal_generation = generation.committed_batch_number;
		checkpoint.generation = generation;
		checkpoint.component_payload_bytes = component_payload_bytes;
		checkpoint.component_last_batch_numbers = component_last_batch_numbers;
		checkpoint.segments = std::move(segments);
		return checkpoint;
	}

	void save_multiway_external_checkpoint_manifest(
		const std::filesystem::path& path,
		const std::filesystem::path& external_storage_directory,
		const MultiwayExternalCheckpointManifest& manifest,
		bool synchronous_durability,
		MultiwayExternalCheckpointWriteOptions options,
		std::uint64_t disk_reserve_bytes)
	{
		const auto& generation = manifest.generation;
		MultiwayExternalGenerationManifest current_generation{};
		if (path.empty() || external_storage_directory.empty() || !load_multiway_external_generation(external_storage_directory, current_generation))
			throw std::invalid_argument("external checkpoint manifest identity is invalid");
		std::array<std::uint64_t, 3U> component_generations{};
		std::array<std::uint32_t, 3U> component_payload_bytes{};
		std::array<std::uint64_t, 3U> component_last_batch_numbers{};
		const auto segment_references = load_checkpoint_segment_references(
			external_storage_directory, &component_generations,
			&component_payload_bytes, &component_last_batch_numbers);
		if (
			manifest.backend_schema != MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA || manifest.model_identity_hash == 0U || manifest.workflow_identity_hash == 0U || manifest.storage_identity_hash == 0U || manifest.schedule_hash == 0U || manifest.completed_batches >= generation.committed_batch_number || manifest.completed_trajectories < manifest.completed_batches || generation.generation == 0U || generation.committed_batch_number == 0U || manifest.wal_generation != generation.committed_batch_number || manifest.wal_committed_offset != 0U || generation.merge_fingerprint == 0U || generation.generation != current_generation.generation || generation.committed_batch_number != current_generation.committed_batch_number || generation.merge_fingerprint != current_generation.merge_fingerprint || generation.catalog_generation != current_generation.catalog_generation || generation.public_state_count != current_generation.public_state_count || generation.row_generation != current_generation.row_generation || generation.row_count != current_generation.row_count || generation.page_generation != current_generation.page_generation || generation.page_count != current_generation.page_count || generation.logical_value_count != current_generation.logical_value_count || generation.content_hash != current_generation.content_hash || component_generations != std::array<std::uint64_t, 3U>{ generation.catalog_generation, generation.row_generation, generation.page_generation } || manifest.linear_iteration_weighting > 1U || manifest.discounting_enabled > 1U || manifest.negative_regret_pruning_enabled > 1U || std::any_of(manifest.late_window_baseline.begin(), manifest.late_window_baseline.end(), [](double value) { return !std::isfinite(value); }) || ((generation.row_count == 0U) != (generation.logical_value_count == 0U)))
			throw std::invalid_argument("external checkpoint manifest identity is invalid");
		std::vector<std::uint8_t> bytes{ 'T', 'X', 'M', 'W', 'C', 'P', '6', '1' };
		append_u32(bytes, MULTIWAY_EXTERNAL_CHECKPOINT_SCHEMA);
		append_u32(bytes, manifest.backend_schema);
		append_u64(bytes, manifest.model_identity_hash);
		append_u64(bytes, manifest.workflow_identity_hash);
		append_u64(bytes, manifest.storage_identity_hash);
		append_u64(bytes, manifest.schedule_hash);
		append_u64(bytes, manifest.completed_trajectories);
		append_u64(bytes, generation.committed_batch_number);
		append_u64(bytes, 0U);
		append_u64(bytes, generation.generation);
		append_u64(bytes, generation.committed_batch_number);
		append_u64(bytes, generation.merge_fingerprint);
		append_u64(bytes, generation.catalog_generation);
		append_u64(bytes, generation.public_state_count);
		append_u64(bytes, generation.row_generation);
		append_u64(bytes, generation.row_count);
		append_u64(bytes, generation.page_generation);
		append_u64(bytes, generation.page_count);
		append_u64(bytes, generation.logical_value_count);
		append_u64(bytes, generation.content_hash);
		for (std::size_t component = 0U; component < component_generations.size(); ++component)
		{
			append_u32(bytes, component_payload_bytes[component]);
			append_u64(bytes, component_last_batch_numbers[component]);
		}
		for (const auto& component : segment_references)
		{
			append_u64(bytes, component.size());
			for (const auto& segment : component)
			{
				append_u64(bytes, segment.generation);
				append_u64(bytes, segment.record_count);
				append_u64(bytes, segment.content_hash);
			}
		}
		append_u32(bytes, kCheckpointTrainingExtensionMagic);
		append_u32(bytes, 1U);
		append_u64(bytes, manifest.completed_batches);
		append_u64(bytes, manifest.pruned_negative_regrets);
		append_u64(bytes, manifest.late_window_start_batch);
		append_u64(bytes, manifest.terminal_visits);
		append_u64(bytes, manifest.leaf_visits);
		append_u64(bytes, manifest.missing_lookup_requests);
		append_u64(bytes, manifest.preflop_rows);
		append_u64(bytes, manifest.flop_rows);
		append_u64(bytes, manifest.turn_rows);
		append_u64(bytes, manifest.river_rows);
		for (const auto visits : manifest.street_visits)
			append_u64(bytes, visits);
		append_u32(bytes, static_cast<std::uint32_t>(manifest.linear_iteration_weighting) | (static_cast<std::uint32_t>(manifest.discounting_enabled) << 8U) | (static_cast<std::uint32_t>(manifest.negative_regret_pruning_enabled) << 16U));
		append_u64(bytes, manifest.late_window_baseline.size());
		for (const auto value : manifest.late_window_baseline)
			append_f64(bytes, value);
		auto checksum = kHashOffset;
		for (const auto byte : bytes)
			texas::core::fingerprint::append_u8(checksum, byte);
		append_u64(bytes, texas::core::fingerprint::finish(checksum));
		if (disk_reserve_bytes != 0U)
		{
			if (bytes.size() > (std::numeric_limits<std::uint64_t>::max() - disk_reserve_bytes) / 2U)
				throw std::length_error("external checkpoint disk requirement overflows");
			const auto required = disk_reserve_bytes + 2U * static_cast<std::uint64_t>(bytes.size());
			std::error_code disk_error;
			const auto storage_space = std::filesystem::space(external_storage_directory, disk_error);
			if (disk_error || storage_space.available < required)
				throw std::runtime_error("external checkpoint storage disk preflight rejected");
			const auto checkpoint_directory = path.parent_path().empty()
				? std::filesystem::current_path(disk_error)
				: path.parent_path();
			if (disk_error)
				throw std::runtime_error("external checkpoint destination disk telemetry is unavailable");
			const auto checkpoint_space = std::filesystem::space(checkpoint_directory, disk_error);
			if (disk_error || checkpoint_space.available < required)
				throw std::runtime_error("external checkpoint destination disk preflight rejected");
		}
		const auto pin_id = checkpoint_pin_id(path, manifest.storage_identity_hash);
		for (std::size_t component = 0U; component < segment_references.size(); ++component)
		{
			const auto component_directory = external_storage_directory / kComponentNames[component];
			const auto pin_path = checkpoint_pin_path(component_directory, pin_id);
			auto references_to_pin = segment_references[component];
			if (std::filesystem::exists(pin_path))
				references_to_pin = merge_checkpoint_pins(read_checkpoint_pin(pin_path), references_to_pin);
			write_checkpoint_pin(component_directory, pin_id, references_to_pin, synchronous_durability);
		}
		const auto temporary = std::filesystem::path(path.string() + ".tmp");
		if (std::filesystem::exists(temporary))
			throw std::runtime_error("external checkpoint manifest temporary path already exists");
		struct TemporaryFileCleanup
		{
			std::filesystem::path path;
			~TemporaryFileCleanup() noexcept
			{
				std::error_code ignored;
				std::filesystem::remove(path, ignored);
			}
		} cleanup{ temporary };
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("external checkpoint manifest cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output)
			throw std::runtime_error("external checkpoint manifest write failed");
		output.close();
		if (synchronous_durability)
			sync_file_path(temporary);
		if (options.fail_before_publication)
			throw std::runtime_error("injected external checkpoint manifest publication failure");
		texas::core::publish_atomic_replace(temporary, path,
			"external checkpoint manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		if (synchronous_durability)
			sync_directory_path(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
		for (std::size_t component = 0U; component < segment_references.size(); ++component)
			write_checkpoint_pin(external_storage_directory / kComponentNames[component], pin_id,
				segment_references[component], synchronous_durability);
	}

	void release_multiway_external_checkpoint_manifest_pins(
		const std::filesystem::path& checkpoint_path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t storage_identity_hash,
		bool synchronous_durability)
	{
		if (checkpoint_path.empty() || external_storage_directory.empty() || storage_identity_hash == 0U)
			throw std::invalid_argument("external checkpoint pin release identity is invalid");
		std::error_code error;
		if (std::filesystem::exists(checkpoint_path, error) || error)
			throw std::invalid_argument("external checkpoint pins cannot be released while the checkpoint exists");
		const auto pin_id = checkpoint_pin_id(checkpoint_path, storage_identity_hash);
		for (const auto* component : kComponentNames)
		{
			const auto directory = external_storage_directory / component;
			std::filesystem::remove(checkpoint_pin_path(directory, pin_id), error);
			if (error)
				throw std::runtime_error("external checkpoint pin cannot be released");
			if (synchronous_durability)
				sync_directory_path(directory);
		}
	}

	MultiwayExternalCheckpointManifest load_multiway_external_checkpoint_manifest(
		const std::filesystem::path& path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t expected_model_identity_hash,
		std::uint64_t expected_workflow_identity_hash,
		std::uint64_t expected_storage_identity_hash,
		std::uint64_t expected_schedule_hash)
	{
		constexpr std::size_t kMinimumSerializedBytes = 228U;
		std::error_code error;
		const auto length = std::filesystem::file_size(path, error);
		if (error || length < kMinimumSerializedBytes || length > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external checkpoint manifest size is invalid");
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
		std::ifstream input(path, std::ios::binary);
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		constexpr std::array<std::uint8_t, 8U> magic = { 'T', 'X', 'M', 'W', 'C', 'P', '6', '1' };
		if (!input || !std::equal(magic.begin(), magic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != MULTIWAY_EXTERNAL_CHECKPOINT_SCHEMA || read_u32(bytes.data() + 12U) != MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA)
			throw std::invalid_argument("external checkpoint manifest header is invalid");
		auto checksum = kHashOffset;
		for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
			texas::core::fingerprint::append_u8(checksum, bytes[index]);
		if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
			throw std::invalid_argument("external checkpoint manifest checksum is invalid");
		MultiwayExternalCheckpointManifest decoded{};
		decoded.backend_schema = read_u32(bytes.data() + 12U);
		std::size_t cursor = 16U;
		const auto read_field = [&]() { const auto value = read_u64(bytes.data() + cursor); cursor += 8U; return value; };
		decoded.model_identity_hash = read_field();
		decoded.workflow_identity_hash = read_field();
		decoded.storage_identity_hash = read_field();
		decoded.schedule_hash = read_field();
		decoded.completed_trajectories = read_field();
		decoded.wal_generation = read_field();
		decoded.wal_committed_offset = read_field();
		auto& generation = decoded.generation;
		generation.generation = read_field();
		generation.committed_batch_number = read_field();
		generation.merge_fingerprint = read_field();
		generation.catalog_generation = read_field();
		generation.public_state_count = read_field();
		generation.row_generation = read_field();
		generation.row_count = read_field();
		generation.page_generation = read_field();
		generation.page_count = read_field();
		generation.logical_value_count = read_field();
		generation.content_hash = read_field();
		for (std::size_t component = 0U; component < decoded.component_payload_bytes.size(); ++component)
		{
			if (cursor > bytes.size() - 8U || bytes.size() - 8U - cursor < 12U)
				throw std::invalid_argument("external checkpoint component metadata is truncated");
			decoded.component_payload_bytes[component] = read_u32(bytes.data() + cursor);
			cursor += 4U;
			decoded.component_last_batch_numbers[component] = read_field();
			if (decoded.component_payload_bytes[component] == 0U || decoded.component_last_batch_numbers[component] == 0U)
				throw std::invalid_argument("external checkpoint component metadata is invalid");
		}
		for (auto& component : decoded.segments)
		{
			if (cursor > bytes.size() - 8U || bytes.size() - 8U - cursor < 8U)
				throw std::invalid_argument("external checkpoint segment list is truncated");
			const auto count = read_field();
			if (count > (bytes.size() - 8U - cursor) / 24U)
				throw std::invalid_argument("external checkpoint segment count is invalid");
			component.reserve(static_cast<std::size_t>(count));
			std::uint64_t previous_generation = 0U;
			for (std::uint64_t index = 0U; index < count; ++index)
			{
				MultiwayExternalCheckpointManifest::SegmentReference reference{};
				reference.generation = read_field();
				reference.record_count = read_field();
				reference.content_hash = read_field();
				if (reference.generation <= previous_generation || reference.record_count == 0U)
					throw std::invalid_argument("external checkpoint segment ordering is invalid");
				previous_generation = reference.generation;
				component.push_back(reference);
			}
		}
		if (cursor != bytes.size() - 8U)
		{
			constexpr std::size_t kTrainingExtensionFixedBytes = 4U + 4U + 14U * 8U + 4U + 8U;
			if (bytes.size() - 8U - cursor < kTrainingExtensionFixedBytes || read_u32(bytes.data() + cursor) != kCheckpointTrainingExtensionMagic || read_u32(bytes.data() + cursor + 4U) != 1U)
				throw std::invalid_argument("external checkpoint training metadata extension is invalid");
			cursor += 8U;
			decoded.completed_batches = read_field();
			decoded.pruned_negative_regrets = read_field();
			decoded.late_window_start_batch = read_field();
			decoded.terminal_visits = read_field();
			decoded.leaf_visits = read_field();
			decoded.missing_lookup_requests = read_field();
			decoded.preflop_rows = read_field();
			decoded.flop_rows = read_field();
			decoded.turn_rows = read_field();
			decoded.river_rows = read_field();
			for (auto& visits : decoded.street_visits)
				visits = read_field();
			const auto flags = read_u32(bytes.data() + cursor);
			cursor += 4U;
			if ((flags & 0xff000000U) != 0U || (flags & 0xffU) > 1U || ((flags >> 8U) & 0xffU) > 1U || ((flags >> 16U) & 0xffU) > 1U)
				throw std::invalid_argument("external checkpoint training flags are invalid");
			decoded.linear_iteration_weighting = static_cast<std::uint8_t>(flags & 0xffU);
			decoded.discounting_enabled = static_cast<std::uint8_t>((flags >> 8U) & 0xffU);
			decoded.negative_regret_pruning_enabled = static_cast<std::uint8_t>((flags >> 16U) & 0xffU);
			const auto baseline_count = read_field();
			if (baseline_count > (bytes.size() - 8U - cursor) / 8U)
				throw std::invalid_argument("external checkpoint late-window baseline count is invalid");
			decoded.late_window_baseline.reserve(static_cast<std::size_t>(baseline_count));
			for (std::uint64_t index = 0U; index < baseline_count; ++index)
			{
				const auto value = read_f64(bytes.data() + cursor);
				cursor += 8U;
				if (!std::isfinite(value))
					throw std::invalid_argument("external checkpoint late-window baseline is non-finite");
				decoded.late_window_baseline.push_back(value);
			}
		}
		if (external_storage_directory.empty())
			throw std::invalid_argument("external checkpoint storage directory is missing");
		const auto pin_id = checkpoint_pin_id(path, expected_storage_identity_hash);
		for (std::size_t component = 0U; component < decoded.segments.size(); ++component)
		{
			const auto pin_path = checkpoint_pin_path(
				external_storage_directory / kComponentNames[component], pin_id);
			const auto pin = read_checkpoint_pin(pin_path);
			if (!verify_checkpoint_segments(
					external_storage_directory / kComponentNames[component],
					decoded.segments[component], pin, decoded.component_payload_bytes[component]))
				throw std::invalid_argument("external checkpoint segment is not retained");
		}
		if (cursor != bytes.size() - 8U || generation.generation == 0U || generation.committed_batch_number == 0U || generation.merge_fingerprint == 0U || ((generation.row_count == 0U) != (generation.logical_value_count == 0U)) || decoded.wal_generation != generation.committed_batch_number || decoded.wal_committed_offset != 0U || decoded.component_payload_bytes[0] == 0U || decoded.component_payload_bytes[1] == 0U || decoded.component_payload_bytes[2] == 0U || expected_model_identity_hash == 0U || expected_workflow_identity_hash == 0U || expected_storage_identity_hash == 0U || expected_schedule_hash == 0U || decoded.model_identity_hash != expected_model_identity_hash || decoded.workflow_identity_hash != expected_workflow_identity_hash || decoded.storage_identity_hash != expected_storage_identity_hash || decoded.schedule_hash != expected_schedule_hash)
			throw std::invalid_argument("external checkpoint manifest identity or generation mismatch");
		return decoded;
	}

	MultiwayExternalCheckpointManifest restore_multiway_external_checkpoint(
		const std::filesystem::path& checkpoint_path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t expected_model_identity_hash,
		std::uint64_t expected_workflow_identity_hash,
		std::uint64_t expected_storage_identity_hash,
		std::uint64_t expected_schedule_hash,
		bool synchronous_durability,
		MultiwayExternalCheckpointRestoreOptions options)
	{
		if (checkpoint_path.empty() || external_storage_directory.empty() || options.fail_after_component_publications > kComponentNames.size())
			throw std::invalid_argument("external checkpoint restore failure point is invalid");
		recover_checkpoint_restore(external_storage_directory, synchronous_durability);
		auto checkpoint = load_multiway_external_checkpoint_manifest(checkpoint_path,
			external_storage_directory, expected_model_identity_hash,
			expected_workflow_identity_hash, expected_storage_identity_hash,
			expected_schedule_hash);
		if (std::filesystem::exists(external_storage_directory / "TRAINING_BATCH.wal") || std::filesystem::exists(external_storage_directory / "TRAINING_BATCH.tmp"))
			throw std::runtime_error("external checkpoint restore requires a committed batch boundary");
		std::uint32_t previous_flags = 0U;
		bool journal_published = false;
		try
		{
			for (std::size_t component = 0U; component < kComponentNames.size(); ++component)
			{
				const auto component_directory = external_storage_directory / kComponentNames[component];
				std::filesystem::create_directories(component_directory);
				const auto current = component_directory / "CURRENT";
				if (std::filesystem::exists(current))
				{
					const auto backup = external_storage_directory / kCheckpointRestoreBackupNames[component];
					if (std::filesystem::exists(backup))
						throw std::runtime_error("external checkpoint restore has a stale component backup");
					std::filesystem::copy_file(current, backup);
					if (synchronous_durability)
						sync_file_path(backup);
					previous_flags |= 1U << component;
				}
			}
			const auto current_generation = external_storage_directory / "TRAINING_CURRENT";
			if (std::filesystem::exists(current_generation))
			{
				const auto backup = external_storage_directory / kCheckpointRestoreBackupNames[3];
				if (std::filesystem::exists(backup))
					throw std::runtime_error("external checkpoint restore has a stale generation backup");
				std::filesystem::copy_file(current_generation, backup);
				if (synchronous_durability)
					sync_file_path(backup);
				previous_flags |= 8U;
			}
			if (synchronous_durability)
				sync_directory_path(external_storage_directory);
			std::array<std::vector<std::uint8_t>, 3U> target_component_manifests;
			std::array<std::uint64_t, 3U> target_component_hashes{};
			for (std::size_t component = 0U; component < kComponentNames.size(); ++component)
			{
				const auto target_component_generation = component == 0U
					? checkpoint.generation.catalog_generation
					: (component == 1U ? checkpoint.generation.row_generation : checkpoint.generation.page_generation);
				target_component_manifests[component] = encode_checkpoint_component_manifest(
					checkpoint.component_payload_bytes[component], target_component_generation,
					checkpoint.component_last_batch_numbers[component], checkpoint.segments[component]);
				auto hash = kHashOffset;
				for (const auto byte : target_component_manifests[component])
					texas::core::fingerprint::append_u8(hash, byte);
				target_component_hashes[component] = texas::core::fingerprint::finish(hash);
			}
			write_checkpoint_restore_wal(external_storage_directory,
				{ previous_flags, checkpoint.generation, target_component_hashes }, synchronous_durability);
			journal_published = true;
			for (std::size_t component = 0U; component < kComponentNames.size(); ++component)
			{
				publish_checkpoint_component_manifest(
					external_storage_directory / kComponentNames[component],
					target_component_manifests[component], synchronous_durability);
				if (options.fail_after_component_publications == component + 1U)
					throw std::runtime_error("injected external checkpoint component publication failure");
			}
			if (options.fail_before_aggregate_publication)
				throw std::runtime_error("injected external checkpoint generation publication failure");
			publish_multiway_external_generation(external_storage_directory,
				checkpoint.generation, synchronous_durability);
			recover_checkpoint_restore(external_storage_directory, synchronous_durability);
			return checkpoint;
		}
		catch (...)
		{
			if (options.leave_pending_journal_for_recovery_test)
				throw;
			if (journal_published || std::filesystem::exists(external_storage_directory / "CHECKPOINT_RESTORE.wal"))
				recover_checkpoint_restore(external_storage_directory, synchronous_durability);
			else
				cleanup_checkpoint_restore(external_storage_directory);
			throw;
		}
	}
} // namespace texas::solver::multiway
