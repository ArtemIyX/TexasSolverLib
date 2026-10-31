#pragma once

#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	namespace
	{
		[[maybe_unused]] std::array<std::vector<MultiwayExternalCheckpointManifest::SegmentReference>, 3U>
		load_checkpoint_segment_references(const std::filesystem::path& storage_directory,
			std::array<std::uint64_t, 3U>* component_generations = nullptr,
			std::array<std::uint32_t, 3U>* component_payload_bytes = nullptr,
			std::array<std::uint64_t, 3U>* component_last_batch_numbers = nullptr)
		{
			std::array<std::vector<MultiwayExternalCheckpointManifest::SegmentReference>, 3U> references;
			for (std::size_t component = 0U; component < kComponentNames.size(); ++component)
			{
				const auto path = storage_directory / kComponentNames[component] / "CURRENT";
				std::ifstream input(path, std::ios::binary);
				if (!input)
					throw std::invalid_argument("external checkpoint component manifest is missing");
				std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
					std::istreambuf_iterator<char>());
				constexpr std::size_t fixed_bytes = 48U;
				if (bytes.size() < fixed_bytes || !std::equal(kManifestMagic.begin(), kManifestMagic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != kSchema)
					throw std::invalid_argument("external checkpoint component manifest header is invalid");
				if (component_generations != nullptr)
					(*component_generations)[component] = read_u64(bytes.data() + 16U);
				if (component_payload_bytes != nullptr)
					(*component_payload_bytes)[component] = read_u32(bytes.data() + 12U);
				if (component_last_batch_numbers != nullptr)
					(*component_last_batch_numbers)[component] = read_u64(bytes.data() + 32U);
				const auto run_count = read_u64(bytes.data() + 24U);
				if (run_count > (bytes.size() - fixed_bytes) / 24U || bytes.size() != fixed_bytes + run_count * 24U)
					throw std::invalid_argument("external checkpoint component run count is invalid");
				auto checksum = kHashOffset;
				for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
					texas::core::fingerprint::append_u8(checksum, bytes[index]);
				if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
					throw std::invalid_argument("external checkpoint component manifest checksum is invalid");
				auto& component_refs = references[component];
				component_refs.reserve(static_cast<std::size_t>(run_count));
				std::size_t cursor = 40U;
				std::uint64_t previous_generation = 0U;
				for (std::uint64_t run = 0U; run < run_count; ++run)
				{
					MultiwayExternalCheckpointManifest::SegmentReference reference{};
					reference.generation = read_u64(bytes.data() + cursor);
					cursor += 8U;
					reference.record_count = read_u64(bytes.data() + cursor);
					cursor += 8U;
					reference.content_hash = read_u64(bytes.data() + cursor);
					cursor += 8U;
					if (reference.generation <= previous_generation || reference.record_count == 0U)
						throw std::invalid_argument("external checkpoint component runs are not ordered");
					previous_generation = reference.generation;
					component_refs.push_back(reference);
				}
			}
			return references;
		}

		[[maybe_unused]] std::vector<MultiwayExternalCheckpointManifest::SegmentReference> merge_checkpoint_pins(
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& previous,
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& current)
		{
			auto combined = previous;
			for (const auto& reference : current)
			{
				const auto found = std::lower_bound(combined.begin(), combined.end(), reference.generation,
					[](const auto& item, std::uint64_t generation) { return item.generation < generation; });
				if (found != combined.end() && found->generation == reference.generation)
				{
					if (found->record_count != reference.record_count || found->content_hash != reference.content_hash)
						throw std::invalid_argument("external checkpoint pin generation identity conflicts");
				}
				else
					combined.insert(found, reference);
			}
			return combined;
		}

		[[maybe_unused]] bool checkpoint_pin_contains(
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& pin,
			const MultiwayExternalCheckpointManifest::SegmentReference& reference)
		{
			const auto found = std::lower_bound(pin.begin(), pin.end(), reference.generation,
				[](const auto& item, std::uint64_t generation) { return item.generation < generation; });
			return found != pin.end() && found->generation == reference.generation && found->record_count == reference.record_count && found->content_hash == reference.content_hash;
		}

		[[maybe_unused]] bool verify_checkpoint_segment(const std::filesystem::path& directory,
			const MultiwayExternalCheckpointManifest::SegmentReference& reference,
			std::uint32_t payload_bytes)
		{
			const auto data_path = run_path(directory, reference.generation, ".dat");
			const auto index_path = run_path(directory, reference.generation, ".idx");
			std::error_code error;
			if (!std::filesystem::is_regular_file(data_path, error) || error)
				return false;
			if (!std::filesystem::is_regular_file(index_path, error) || error)
				return false;
			const auto expected_data = record_bytes(reference.record_count, payload_bytes);
			if (std::filesystem::file_size(data_path) != expected_data || std::filesystem::file_size(index_path) != record_bytes(reference.record_count, 0U))
				return false;
			auto hash = kHashOffset;
			append_file_hash(hash, index_path);
			append_file_hash(hash, data_path);
			return texas::core::fingerprint::finish(hash) == reference.content_hash;
		}

		[[maybe_unused]] bool verify_checkpoint_segments(
			const std::filesystem::path& directory,
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& references,
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& pin,
			std::uint32_t payload_bytes)
		{
			constexpr std::size_t kMaximumWorkers = 4U;
			if (references.empty())
				return true;
			const auto worker_count = std::min(kMaximumWorkers, references.size());
			if (worker_count == 1U)
				return checkpoint_pin_contains(pin, references.front()) && verify_checkpoint_segment(directory, references.front(), payload_bytes);

			std::atomic<std::size_t> next_reference{ 0U };
			std::atomic<bool> valid{ true };
			std::array<std::exception_ptr, kMaximumWorkers> worker_errors{};
			std::vector<std::thread> workers;
			workers.reserve(worker_count);
			const auto validate = [&](std::size_t worker_index) {
				while (valid.load(std::memory_order_relaxed))
				{
					const auto index = next_reference.fetch_add(1U, std::memory_order_relaxed);
					if (index >= references.size())
						return;
					try
					{
						if (!checkpoint_pin_contains(pin, references[index]) || !verify_checkpoint_segment(directory, references[index], payload_bytes))
						{
							valid.store(false, std::memory_order_relaxed);
							return;
						}
					}
					catch (...)
					{
						worker_errors[worker_index] = std::current_exception();
						valid.store(false, std::memory_order_relaxed);
						return;
					}
				}
			};
			const auto join_workers = [&]() noexcept {
				for (auto& worker : workers)
					if (worker.joinable())
						worker.join();
			};
			try
			{
				for (std::size_t index = 1U; index < worker_count; ++index)
					workers.emplace_back(validate, index);
			}
			catch (...)
			{
				valid.store(false, std::memory_order_relaxed);
				join_workers();
				throw;
			}
			validate(0U);
			join_workers();
			for (const auto& error : worker_errors)
				if (error)
					std::rethrow_exception(error);
			return valid.load(std::memory_order_relaxed);
		}
	} // namespace
} // namespace texas::solver::multiway
