#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"

#include "core/fingerprint.hpp"
#include "solver/multiway/blueprint/multiway_artifact.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

namespace texas::solver::multiway
{
	namespace
	{
		constexpr std::array<std::uint8_t, 8U> kShardMagic = { 'M', 'W', 'F', 'B', '0', '0', '0', '2' };
		constexpr std::uint64_t kHeaderBytes = 176U;
		constexpr std::uint64_t kSparseIndexLimit = 262'144U;
		constexpr std::size_t kMaxActions = MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW;

		struct SortKey
		{
			std::uint64_t public_state = 0U;
			std::int32_t seat = -1;
			std::uint32_t bucket = 0U;
			std::uint64_t menu = 0U;
		};

		bool key_less(const SortKey& left, const SortKey& right) noexcept
		{
			if (left.public_state != right.public_state) return left.public_state < right.public_state;
			if (left.seat != right.seat) return left.seat < right.seat;
			if (left.bucket != right.bucket) return left.bucket < right.bucket;
			return left.menu < right.menu;
		}

		bool same_infoset(const SortKey& left, MultiwayInfosetId right) noexcept
		{
			return left.public_state == right.public_state.value && left.seat == right.seat;
		}

		bool same_stream_training(
			const MultiwayBlueprintTrainingMetadata& left,
			const MultiwayBlueprintTrainingMetadata& right) noexcept
		{
			return left.batches == right.batches && left.trajectories == right.trajectories &&
				left.deterministic_seed == right.deterministic_seed &&
				left.late_window_start_batch == right.late_window_start_batch &&
				left.schedule_hash == right.schedule_hash &&
				left.pruned_negative_regrets == right.pruned_negative_regrets &&
				left.linear_iteration_weighting == right.linear_iteration_weighting &&
				left.discounting_enabled == right.discounting_enabled &&
				left.negative_regret_pruning_enabled == right.negative_regret_pruning_enabled &&
				left.reserved == right.reserved;
		}

		class MappedReader
		{
		public:
			MappedReader(const std::uint8_t* data, std::size_t size, std::size_t offset = 0U) noexcept
				: data_(data), size_(size), offset_(offset)
			{
			}

			[[nodiscard]] std::size_t offset() const noexcept { return offset_; }
			[[nodiscard]] bool bytes(std::uint8_t* output, std::size_t count) noexcept
			{
				if (offset_ > size_ || count > size_ - offset_) return false;
				if (count != 0U) std::memcpy(output, data_ + offset_, count);
				offset_ += count;
				return true;
			}
			[[nodiscard]] bool u8(std::uint8_t& value) noexcept
			{
				if (offset_ >= size_) return false;
				value = data_[offset_++];
				return true;
			}
			[[nodiscard]] bool u16(std::uint16_t& value) noexcept
			{
				std::uint8_t a = 0U, b = 0U;
				if (!u8(a) || !u8(b)) return false;
				value = static_cast<std::uint16_t>(a) | (static_cast<std::uint16_t>(b) << 8U);
				return true;
			}
			[[nodiscard]] bool u32(std::uint32_t& value) noexcept
			{
				value = 0U;
				for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
				{
					std::uint8_t byte = 0U;
					if (!u8(byte)) return false;
					value |= static_cast<std::uint32_t>(byte) << shift;
				}
				return true;
			}
			[[nodiscard]] bool i32(std::int32_t& value) noexcept
			{
				std::uint32_t raw = 0U;
				if (!u32(raw)) return false;
				value = static_cast<std::int32_t>(raw);
				return true;
			}
			[[nodiscard]] bool u64(std::uint64_t& value) noexcept
			{
				value = 0U;
				for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
				{
					std::uint8_t byte = 0U;
					if (!u8(byte)) return false;
					value |= static_cast<std::uint64_t>(byte) << shift;
				}
				return true;
			}

		private:
			const std::uint8_t* data_ = nullptr;
			std::size_t size_ = 0U;
			std::size_t offset_ = 0U;
		};

		struct MappedFile
		{
			const std::uint8_t* data = nullptr;
			std::size_t size = 0U;
			~MappedFile() noexcept
			{
				if (data == nullptr) return;
#ifdef _WIN32
				UnmapViewOfFile(data);
#else
				munmap(const_cast<std::uint8_t*>(data), size);
#endif
			}
		};

		std::unique_ptr<MappedFile> map_readonly(const std::filesystem::path& path)
		{
			auto mapped = std::make_unique<MappedFile>();
#ifdef _WIN32
			const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				throw std::runtime_error("multiway blueprint shard cannot be opened for mapping");
			LARGE_INTEGER file_size{};
			if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
				static_cast<unsigned long long>(file_size.QuadPart) > std::numeric_limits<std::size_t>::max())
			{
				CloseHandle(file);
				throw std::length_error("multiway blueprint shard mapping size is invalid");
			}
			const auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0U, 0U, nullptr);
			CloseHandle(file);
			if (mapping == nullptr)
				throw std::runtime_error("multiway blueprint shard mapping cannot be created");
			mapped->data = static_cast<const std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0U, 0U, 0U));
			CloseHandle(mapping);
			mapped->size = static_cast<std::size_t>(file_size.QuadPart);
#else
			const auto file = open(path.c_str(), O_RDONLY);
			if (file < 0)
				throw std::runtime_error("multiway blueprint shard cannot be opened for mapping");
			struct stat status{};
			if (fstat(file, &status) != 0 || status.st_size <= 0 ||
				static_cast<unsigned long long>(status.st_size) > std::numeric_limits<std::size_t>::max())
			{
				close(file);
				throw std::length_error("multiway blueprint shard mapping size is invalid");
			}
			mapped->size = static_cast<std::size_t>(status.st_size);
			const auto* view = mmap(nullptr, mapped->size, PROT_READ, MAP_PRIVATE, file, 0);
			close(file);
			mapped->data = view == MAP_FAILED ? nullptr : static_cast<const std::uint8_t*>(view);
#endif
			if (mapped->data == nullptr)
				throw std::runtime_error("multiway blueprint shard cannot be mapped");
			return mapped;
		}

		bool read_key(MappedReader& reader, SortKey& key) noexcept
		{
			return reader.u64(key.public_state) && reader.i32(key.seat) &&
				reader.u32(key.bucket) && reader.u64(key.menu);
		}

		bool read_row(
			MappedReader& reader,
			SortKey& key,
			std::array<MultiwayQuantizedRootAction, kMaxActions>& actions,
			std::uint32_t& action_count) noexcept
		{
			if (!read_key(reader, key) || !reader.u32(action_count) || action_count == 0U || action_count > kMaxActions)
				return false;
			for (std::uint32_t index = 0U; index < action_count; ++index)
			{
				auto& action = actions[index];
				std::uint8_t kind = 0U;
				if (!reader.u8(kind) || !reader.u32(action.action.action_index) ||
					!reader.i32(action.action.target_street_contribution) ||
					!reader.u64(action.action.action_menu_id) || !reader.u16(action.probability))
					return false;
				action.action.action = static_cast<MultiwayAction>(kind);
			}
			return true;
		}

		bool valid_row(
			const SortKey& key,
			const std::array<MultiwayQuantizedRootAction, kMaxActions>& actions,
			std::uint32_t action_count) noexcept
		{
			if (key.public_state == 0U || key.seat < 0 || key.menu == 0U || action_count == 0U)
				return false;
			std::uint32_t total = 0U;
			for (std::uint32_t index = 0U; index < action_count; ++index)
			{
				if (actions[index].action.action_index != index ||
					actions[index].action.action_menu_id != key.menu || actions[index].probability == 0U ||
					actions[index].probability > 65535U - total)
					return false;
				total += actions[index].probability;
			}
			return total == 65535U;
		}

		MultiwayFullBlueprintRowView make_view(
			const SortKey& key,
			const std::array<MultiwayQuantizedRootAction, kMaxActions>& actions,
			std::uint32_t action_count) noexcept
		{
			MultiwayFullBlueprintRowView view;
			view.infoset = { { key.public_state }, key.seat };
			view.bucket = key.bucket;
			view.action_menu_id = key.menu;
			view.actions = actions.data();
			view.action_count = action_count;
			return view;
		}

	} // namespace

	struct MultiwayMappedShardedBlueprintProvider::Impl
	{
		struct SparseIndexEntry
		{
			std::uint64_t row = 0U;
			std::uint64_t offset = 0U;
			SortKey key{};
		};
		struct Shard
		{
			std::unique_ptr<MappedFile> mapping;
			std::vector<SparseIndexEntry> index;
			SortKey first_key{};
			SortKey last_key{};
			std::uint64_t row_count = 0U;
		};
		std::vector<Shard> shards;
		MultiwayModelIdentity identity{};
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t mapped_bytes = 0U;
		std::uint64_t index_bytes = 0U;
	};

	MultiwayMappedShardedBlueprintProvider::MultiwayMappedShardedBlueprintProvider(
		std::unique_ptr<Impl> impl) noexcept
		: impl_(std::move(impl))
	{
	}

	MultiwayMappedShardedBlueprintProvider::~MultiwayMappedShardedBlueprintProvider() = default;

	std::unique_ptr<MultiwayMappedShardedBlueprintProvider> MultiwayMappedShardedBlueprintProvider::load_verified(
		const std::filesystem::path& manifest_path,
		const MultiwayModelIdentity& expected_identity)
	{
		const auto manifest = MultiwayFullBlueprintArtifacts::load_sharded_manifest(manifest_path, expected_identity);
		auto impl = std::make_unique<Impl>();
		impl->identity = manifest.identity;
		impl->policy_content_hash = manifest.policy_content_hash;
		impl->shards.reserve(manifest.shards.size());
		const auto shard_count = static_cast<std::uint64_t>(manifest.shards.size());
		const auto index_slots = kSparseIndexLimit > shard_count ? kSparseIndexLimit - shard_count : 1U;
		const auto stride = std::max<std::uint64_t>(1U,
			manifest.row_count / index_slots + (manifest.row_count % index_slots != 0U ? 1U : 0U));
		MultiwayFullBlueprintPolicyContentHasher policy_hasher;
		policy_hasher.begin(manifest.row_count);
		std::uint64_t total_actions = 0U;
		SortKey previous{};
		bool has_previous = false;
		std::error_code root_error;
		const auto root_candidate = manifest_path.parent_path().empty()
			? std::filesystem::current_path(root_error)
			: manifest_path.parent_path();
		const auto canonical_root = std::filesystem::weakly_canonical(root_candidate, root_error);
		if (root_error)
			throw std::runtime_error("multiway mapped blueprint root cannot be resolved");
		for (const auto& reference : manifest.shards)
		{
			const auto shard_path = manifest_path.parent_path() / std::filesystem::u8path(reference.relative_path);
			const auto canonical_shard = std::filesystem::weakly_canonical(shard_path, root_error);
			if (root_error)
				throw std::runtime_error("multiway mapped blueprint shard path cannot be resolved");
			const auto relative_shard = canonical_shard.lexically_relative(canonical_root);
			if (relative_shard.empty() || relative_shard.is_absolute() ||
				std::any_of(relative_shard.begin(), relative_shard.end(), [](const auto& part) {
					return part == "..";
				}))
				throw std::invalid_argument("multiway mapped blueprint shard escapes its manifest directory");
			auto mapped = map_readonly(shard_path);
			if (mapped->size != reference.serialized_bytes || mapped->size > manifest.maximum_serialized_bytes ||
				mapped->size < kHeaderBytes + 8U)
				throw std::invalid_argument("multiway mapped blueprint shard has invalid size");
			MappedReader reader(mapped->data, mapped->size);
			std::array<std::uint8_t, kShardMagic.size()> magic{};
			if (!reader.bytes(magic.data(), magic.size()) || magic != kShardMagic)
				throw std::invalid_argument("multiway mapped blueprint shard schema is invalid");
			std::uint32_t schema = 0U;
			if (!reader.u32(schema) || schema != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION)
				throw std::invalid_argument("multiway mapped blueprint shard schema is unsupported");
			MultiwayModelIdentity identity;
			bool identity_ok = true;
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
				if (identity_ok) identity_ok = reader.u64(field);
			});
			MultiwayBlueprintTrainingMetadata training;
			if (!identity_ok || identity != manifest.identity ||
				!reader.u64(training.batches) || !reader.u64(training.trajectories) ||
				!reader.u64(training.deterministic_seed) || !reader.u64(training.late_window_start_batch) ||
				!reader.u64(training.schedule_hash) || !reader.u64(training.pruned_negative_regrets) ||
				!reader.u8(training.linear_iteration_weighting) || !reader.u8(training.discounting_enabled) ||
				!reader.u8(training.negative_regret_pruning_enabled) || !reader.u8(training.reserved) ||
				!same_stream_training(training, manifest.training))
				throw std::invalid_argument("multiway mapped blueprint shard identity or metadata mismatch");
			std::uint64_t row_count = 0U;
			if (!reader.u64(row_count) || row_count != reference.range.row_count)
				throw std::invalid_argument("multiway mapped blueprint shard row count mismatch");

			Impl::Shard shard;
			shard.mapping = std::move(mapped);
			shard.row_count = row_count;
			const auto estimated_index_count = row_count / stride + (row_count % stride != 0U ? 1U : 0U);
			if (estimated_index_count > shard.index.max_size())
				throw std::length_error("multiway mapped blueprint sparse index is too large");
			shard.index.reserve(static_cast<std::size_t>(estimated_index_count));
			MultiwayFullBlueprintPayloadHasher payload_hasher;
			payload_hasher.begin(schema, identity, training, row_count);
			std::uint64_t shard_actions = 0U;
			for (std::uint64_t row_index = 0U; row_index < row_count; ++row_index)
			{
				const auto row_offset = reader.offset();
				SortKey key;
				std::array<MultiwayQuantizedRootAction, kMaxActions> actions{};
				std::uint32_t action_count = 0U;
				if (!read_row(reader, key, actions, action_count) || !valid_row(key, actions, action_count))
					throw std::invalid_argument("multiway mapped blueprint shard row is invalid");
				if (has_previous && !key_less(previous, key))
					throw std::invalid_argument("multiway mapped blueprint shards are not globally ordered");
				previous = key;
				has_previous = true;
				if (row_index % stride == 0U)
					shard.index.push_back({ row_index, row_offset, key });
				if (row_index == 0U) shard.first_key = key;
				shard.last_key = key;
				const auto view = make_view(key, actions, action_count);
				payload_hasher.append_row(view);
				policy_hasher.append_row(view);
				if (action_count > std::numeric_limits<std::uint64_t>::max() - shard_actions)
					throw std::length_error("multiway mapped shard action count overflows");
				shard_actions += action_count;
			}
			std::uint64_t payload_hash = 0U;
			if (!reader.u64(payload_hash) || reader.offset() != shard.mapping->size ||
				payload_hash == 0U || payload_hash != payload_hasher.finish() ||
				payload_hash != reference.payload_hash || shard_actions != reference.action_count)
				throw std::invalid_argument("multiway mapped blueprint shard integrity mismatch");
			if (shard.index.empty() || shard.index.front().offset != kHeaderBytes)
				throw std::invalid_argument("multiway mapped blueprint shard index is incomplete");
			if (shard.mapping->size > std::numeric_limits<std::uint64_t>::max() - impl->mapped_bytes ||
				shard.index.capacity() > std::numeric_limits<std::uint64_t>::max() / sizeof(Impl::SparseIndexEntry))
				throw std::length_error("multiway mapped blueprint memory accounting overflows");
			const auto shard_index_bytes = static_cast<std::uint64_t>(shard.index.capacity()) * sizeof(Impl::SparseIndexEntry);
			constexpr auto maximum_index_bytes = kSparseIndexLimit * sizeof(Impl::SparseIndexEntry);
			if (shard_index_bytes > maximum_index_bytes - impl->index_bytes ||
				shard_actions > std::numeric_limits<std::uint64_t>::max() - total_actions)
				throw std::length_error("multiway mapped blueprint memory accounting overflows");
			impl->mapped_bytes += shard.mapping->size;
			impl->index_bytes += shard_index_bytes;
			total_actions += shard_actions;
			impl->shards.push_back(std::move(shard));
		}
		if (total_actions != manifest.action_count || policy_hasher.finish() != manifest.policy_content_hash)
			throw std::invalid_argument("multiway mapped blueprint aggregate integrity mismatch");
		return std::unique_ptr<MultiwayMappedShardedBlueprintProvider>(
			new MultiwayMappedShardedBlueprintProvider(std::move(impl)));
	}

	MultiwayBlueprintLookupStatus MultiwayMappedShardedBlueprintProvider::strategy_into(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		const MultiwayActionDescriptor* legal_actions,
		std::size_t action_count,
		Probability* output) const noexcept
	{
		if (impl_ == nullptr || legal_actions == nullptr || output == nullptr || action_count == 0U ||
			impl_->shards.empty())
			return MultiwayBlueprintLookupStatus::Missing;
		const SortKey target{ infoset.public_state.value, infoset.seat, bucket,
			legal_actions[0].action_menu_id };
		const auto shard_it = std::lower_bound(impl_->shards.begin(), impl_->shards.end(), target,
			[](const Impl::Shard& shard, const SortKey& key) noexcept { return key_less(shard.last_key, key); });
		bool infoset_present = false;
		bool bucket_present = false;
		const auto search_shard = [&](const Impl::Shard& shard) noexcept
		{
			const auto index_it = std::upper_bound(shard.index.begin(), shard.index.end(), target,
				[](const SortKey& key, const Impl::SparseIndexEntry& entry) noexcept {
					return key_less(key, entry.key);
				});
			const auto& index = index_it == shard.index.begin() ? shard.index.front() : *(index_it - 1);
			MappedReader reader(shard.mapping->data, shard.mapping->size, static_cast<std::size_t>(index.offset));
			for (std::uint64_t row_index = index.row; row_index < shard.row_count; ++row_index)
			{
				SortKey key;
				std::array<MultiwayQuantizedRootAction, kMaxActions> actions{};
				std::uint32_t row_action_count = 0U;
				if (!read_row(reader, key, actions, row_action_count))
					return false;
				if (same_infoset(key, infoset))
				{
					infoset_present = true;
					if (key.bucket == bucket) bucket_present = true;
				}
				if (!key_less(key, target))
				{
					if (key.public_state != target.public_state || key.seat != target.seat ||
						key.bucket != target.bucket || key.menu != target.menu)
						return true;
					if (row_action_count != action_count)
					{
						bucket_present = true;
						return true;
					}
					for (std::size_t action_index = 0U; action_index < action_count; ++action_index)
					{
						if (actions[action_index].action != legal_actions[action_index])
						{
						bucket_present = true;
						return true;
					}
					}
					constexpr auto total = static_cast<Probability>(std::numeric_limits<std::uint16_t>::max());
					for (std::size_t action_index = 0U; action_index < action_count; ++action_index)
						output[action_index] = static_cast<Probability>(actions[action_index].probability) / total;
					return false;
				}
				if (key.public_state > target.public_state ||
					(key.public_state == target.public_state && key.seat > target.seat))
					break;
			}
			return true;
		};
		const auto center = shard_it == impl_->shards.end()
			? impl_->shards.size() - 1U
			: static_cast<std::size_t>(shard_it - impl_->shards.begin());
		std::size_t begin = center;
		std::size_t end = center;
		if (begin != 0U && same_infoset(impl_->shards[begin - 1U].last_key, infoset))
			--begin;
		if (end + 1U < impl_->shards.size() && same_infoset(impl_->shards[end + 1U].first_key, infoset))
			++end;
		for (auto shard_index = begin; shard_index <= end; ++shard_index)
		{
			if (!search_shard(impl_->shards[shard_index]))
				return MultiwayBlueprintLookupStatus::Hit;
		}
		return bucket_present ? MultiwayBlueprintLookupStatus::IncompatibleMenu :
			(infoset_present ? MultiwayBlueprintLookupStatus::MissingBucket : MultiwayBlueprintLookupStatus::Missing);
	}

	std::uint64_t MultiwayMappedShardedBlueprintProvider::mapped_virtual_bytes() const noexcept
	{
		return impl_ == nullptr ? 0U : impl_->mapped_bytes;
	}

	std::uint64_t MultiwayMappedShardedBlueprintProvider::sparse_index_bytes() const noexcept
	{
		return impl_ == nullptr ? 0U : impl_->index_bytes;
	}

	const MultiwayModelIdentity& MultiwayMappedShardedBlueprintProvider::identity() const noexcept
	{
		static const MultiwayModelIdentity empty{};
		return impl_ == nullptr ? empty : impl_->identity;
	}

	std::uint64_t MultiwayMappedShardedBlueprintProvider::policy_content_hash() const noexcept
	{
		return impl_ == nullptr ? 0U : impl_->policy_content_hash;
	}

} // namespace texas::solver::multiway
