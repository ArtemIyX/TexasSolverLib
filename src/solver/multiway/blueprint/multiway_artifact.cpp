#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace texas::solver::multiway
{
	namespace
	{

		namespace portable = texas::core::portable;

		constexpr std::array<char, 8> kManifestMagic = { 'M', 'W', 'M', 'F', '0', '0', '0', '4' };
		constexpr std::array<char, 8> kFullBlueprintMagic = { 'M', 'W', 'F', 'B', '0', '0', '0', '2' };
		constexpr std::array<char, 8> kShardedBlueprintMagic = { 'M', 'W', 'B', 'S', '0', '0', '0', '1' };
		constexpr std::uint64_t kFullBlueprintOperatingBytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr std::size_t kMaximumActionsPerRow = MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW;
		constexpr std::size_t kStreamBufferBytes = 64U * 1024U;
		constexpr std::uint64_t kSerializedHeaderBytes = 176U;
		constexpr std::uint64_t kSerializedRowFixedBytes = 28U;
		constexpr std::uint64_t kSerializedActionBytes = 19U;
		constexpr std::uint64_t kSerializedTrailerBytes = 8U;
		using texas::core::fingerprint::append_u64;
		using texas::core::fingerprint::finish;

		std::uint64_t checked_add(
			std::uint64_t left,
			std::uint64_t right,
			const char* message)
		{
			if (right > std::numeric_limits<std::uint64_t>::max() - left)
				throw std::length_error(message);
			return left + right;
		}

		std::uint64_t checked_multiply(
			std::uint64_t left,
			std::uint64_t right,
			const char* message)
		{
			if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
				throw std::length_error(message);
			return left * right;
		}

		std::string fixed_width_u64(std::uint64_t value)
		{
			auto digits = std::to_string(value);
			if (digits.size() > 20U)
				throw std::length_error("multiway shard generation component is too large");
			return std::string(20U - digits.size(), '0') + digits;
		}

		void write_identity(std::ofstream& out, const MultiwayModelIdentity& identity)
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				portable::write_u64(out, field);
			});
		}

		MultiwayModelIdentity read_identity(std::ifstream& in)
		{
			MultiwayModelIdentity identity;
			bool valid = true;
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
				if (valid)
					valid = portable::read_u64(in, field);
			});
			if (!valid)
			{
				throw std::runtime_error("multiway artifact is truncated");
			}
			return identity;
		}

		MultiwayBlueprintTrainingMetadata read_training(std::ifstream& in)
		{
			MultiwayBlueprintTrainingMetadata training;
			if (!portable::read_u64(in, training.batches) || !portable::read_u64(in, training.trajectories) || !portable::read_u64(in, training.deterministic_seed) || !portable::read_u64(in, training.late_window_start_batch) || !portable::read_u64(in, training.schedule_hash) || !portable::read_u64(in, training.pruned_negative_regrets) || !portable::read_u8(in, training.linear_iteration_weighting) || !portable::read_u8(in, training.discounting_enabled) || !portable::read_u8(in, training.negative_regret_pruning_enabled) || !portable::read_u8(in, training.reserved))
			{
				throw std::runtime_error("multiway artifact is truncated");
			}
			return training;
		}

		MultiwayInfosetId read_infoset(std::ifstream& in)
		{
			MultiwayInfosetId infoset;
			if (!portable::read_u64(in, infoset.public_state.value) || !portable::read_i32(in, infoset.seat))
			{
				throw std::runtime_error("multiway artifact is truncated");
			}
			return infoset;
		}

		MultiwayQuantizedRootAction read_action(std::ifstream& in)
		{
			MultiwayQuantizedRootAction action;
			std::uint8_t kind = 0;
			if (!portable::read_u8(in, kind) || !portable::read_u32(in, action.action.action_index) || !portable::read_i32(in, action.action.target_street_contribution) || !portable::read_u64(in, action.action.action_menu_id) || !portable::read_u16(in, action.probability))
			{
				throw std::runtime_error("multiway artifact is truncated");
			}
			action.action.action = static_cast<MultiwayAction>(kind);
			return action;
		}

		void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept;

		void append_training(
			std::uint64_t& hash,
			const MultiwayBlueprintTrainingMetadata& training) noexcept;

		void write_training(std::ofstream& out, const MultiwayBlueprintTrainingMetadata& training)
		{
			portable::write_u64(out, training.batches);
			portable::write_u64(out, training.trajectories);
			portable::write_u64(out, training.deterministic_seed);
			portable::write_u64(out, training.late_window_start_batch);
			portable::write_u64(out, training.schedule_hash);
			portable::write_u64(out, training.pruned_negative_regrets);
			portable::write_u8(out, training.linear_iteration_weighting);
			portable::write_u8(out, training.discounting_enabled);
			portable::write_u8(out, training.negative_regret_pruning_enabled);
			portable::write_u8(out, training.reserved);
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

		std::uint64_t shard_manifest_hash(const MultiwayFullBlueprintShardManifest& manifest) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, manifest.schema_version);
			append_identity(hash, manifest.identity);
			append_training(hash, manifest.training);
			append_u64(hash, manifest.training.reserved);
			append_u64(hash, manifest.row_count);
			append_u64(hash, manifest.action_count);
			append_u64(hash, manifest.maximum_serialized_bytes);
			append_u64(hash, manifest.policy_content_hash);
			append_u64(hash, manifest.shards.size());
			for (const auto& shard : manifest.shards)
			{
				append_u64(hash, shard.relative_path.size());
				for (const auto character : shard.relative_path)
					texas::core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
				append_u64(hash, shard.range.first_row);
				append_u64(hash, shard.range.row_count);
				append_u64(hash, shard.action_count);
				append_u64(hash, shard.serialized_bytes);
				append_u64(hash, shard.payload_hash);
			}
			return finish(hash);
		}

		bool safe_relative_shard_path(const std::string& value)
		{
			if (value.empty() || value.size() > 4096U)
				return false;
			const std::filesystem::path path = std::filesystem::u8path(value);
			if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
				return false;
			for (const auto& component : path)
			{
				if (component == ".." || component == ".")
					return false;
			}
			return path.lexically_normal().generic_u8string() == value;
		}

		std::filesystem::path manifest_path(const std::filesystem::path& checkpoint_path)
		{
			return checkpoint_path.string() + ".manifest";
		}

		void save_manifest_atomic(const std::filesystem::path& path, const MultiwayBlueprintManifest& manifest)
		{
			const auto temp = path.string() + ".tmp";
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out)
				throw std::runtime_error("multiway manifest temporary file cannot be opened");
			out.write(kManifestMagic.data(), static_cast<std::streamsize>(kManifestMagic.size()));
			portable::write_u32(out, manifest.schema_version);
			write_identity(out, manifest.identity);
			portable::write_u64(out, manifest.snapshot_hash);
			out.close();
			if (!out)
				throw std::runtime_error("multiway manifest write failed");
			texas::core::publish_atomic_replace(temp, path, "multiway manifest publish failed");
		}

		MultiwayBlueprintManifest load_manifest(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				throw std::runtime_error("multiway manifest cannot be opened");
			std::array<char, kManifestMagic.size()> magic{};
			in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
			if (!in || magic != kManifestMagic)
				throw std::runtime_error("multiway manifest schema is invalid");
			MultiwayBlueprintManifest manifest;
			if (!portable::read_u32(in, manifest.schema_version))
				throw std::runtime_error("multiway manifest is truncated");
			manifest.identity = read_identity(in);
			if (!portable::read_u64(in, manifest.snapshot_hash))
				throw std::runtime_error("multiway manifest is truncated");
			if (in.peek() != std::char_traits<char>::eof())
			{
				throw std::runtime_error("multiway manifest has trailing data");
			}
			manifest.validate();
			return manifest;
		}

		void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
		}

		void append_action(std::uint64_t& hash, const MultiwayActionDescriptor& action) noexcept
		{
			append_u64(hash, static_cast<std::uint64_t>(action.action));
			append_u64(hash, action.action_index);
			append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(action.target_street_contribution)));
			append_u64(hash, action.action_menu_id);
		}

		void append_policy_row(
			std::uint64_t& hash,
			const MultiwayFullBlueprintRowView& row) noexcept
		{
			append_u64(hash, row.infoset.public_state.value);
			append_u64(hash, static_cast<std::uint64_t>(row.infoset.seat));
			append_u64(hash, row.bucket);
			append_u64(hash, row.action_menu_id);
			append_u64(hash, row.action_count);
			for (std::size_t index = 0U; index < row.action_count; ++index)
			{
				append_action(hash, row.actions[index].action);
				append_u64(hash, row.actions[index].probability);
			}
		}

		void append_training(
			std::uint64_t& hash,
			const MultiwayBlueprintTrainingMetadata& training) noexcept
		{
			append_u64(hash, training.batches);
			append_u64(hash, training.trajectories);
			append_u64(hash, training.deterministic_seed);
			append_u64(hash, training.late_window_start_batch);
			append_u64(hash, training.schedule_hash);
			append_u64(hash, training.pruned_negative_regrets);
			append_u64(hash, training.linear_iteration_weighting);
			append_u64(hash, training.discounting_enabled);
			append_u64(hash, training.negative_regret_pruning_enabled);
		}

		struct RowKey
		{
			std::uint64_t public_state = 0U;
			core::PlayerId seat = -1;
			std::uint32_t bucket = 0U;
			std::uint64_t action_menu_id = 0U;
		};

		RowKey row_key(const MultiwayFullBlueprintRowView& row) noexcept
		{
			return { row.infoset.public_state.value, row.infoset.seat, row.bucket, row.action_menu_id };
		}

		bool row_key_less(const RowKey& left, const RowKey& right) noexcept
		{
			if (left.public_state != right.public_state)
				return left.public_state < right.public_state;
			if (left.seat != right.seat)
				return left.seat < right.seat;
			if (left.bucket != right.bucket)
				return left.bucket < right.bucket;
			return left.action_menu_id < right.action_menu_id;
		}

		bool row_key_equal(const RowKey& left, const RowKey& right) noexcept
		{
			return left.public_state == right.public_state && left.seat == right.seat
				&& left.bucket == right.bucket && left.action_menu_id == right.action_menu_id;
		}

		void validate_row(const MultiwayFullBlueprintRowView& row)
		{
			if (row.infoset.public_state.value == 0U || row.infoset.seat < 0
				|| row.action_menu_id == 0U || row.actions == nullptr || row.action_count == 0U
				|| row.action_count > kMaximumActionsPerRow)
			{
				throw std::invalid_argument("multiway full blueprint row is incomplete");
			}

			std::uint32_t total = 0U;
			for (std::size_t index = 0U; index < row.action_count; ++index)
			{
				const auto& action = row.actions[index];
				if (action.action.action_index != index || action.action.action_menu_id != row.action_menu_id
					|| action.probability == 0U)
				{
					throw std::invalid_argument("multiway full blueprint row has invalid action data");
				}
				if (action.probability > 65535U - total)
					throw std::invalid_argument("multiway full blueprint row probability sum overflows");
				total += action.probability;
			}
			if (total != 65535U)
				throw std::invalid_argument("multiway full blueprint row is not normalized");
		}

		std::uint64_t materialized_action_count(const std::vector<MultiwayBlueprintRow>& rows)
		{
			std::uint64_t action_count = 0U;
			for (const auto& row : rows)
			{
				validate_row(MultiwayFullBlueprintRowView(row));
				action_count = checked_add(
					action_count,
					static_cast<std::uint64_t>(row.actions.size()),
					"multiway full blueprint action count is too large");
			}
			return action_count;
		}

		void validate_materialized_rows(
			const std::vector<MultiwayBlueprintRow>& rows)
		{
			if (rows.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::length_error("multiway full blueprint row count exceeds the index width");

			bool already_sorted = true;
			RowKey previous{};
			bool has_previous = false;
			for (const auto& row : rows)
			{
				const MultiwayFullBlueprintRowView view(row);
				validate_row(view);
				const auto current = row_key(view);
				if (has_previous && !row_key_less(previous, current))
					already_sorted = false;
				previous = current;
				has_previous = true;
			}

			if (already_sorted)
				return;

			std::vector<std::uint32_t> row_indices(rows.size());
			for (std::uint32_t index = 0U; index < row_indices.size(); ++index)
				row_indices[index] = index;
			std::sort(row_indices.begin(), row_indices.end(), [&](std::uint32_t left, std::uint32_t right) {
				return row_key_less(
					row_key(MultiwayFullBlueprintRowView(rows[left])),
					row_key(MultiwayFullBlueprintRowView(rows[right])));
			});
			for (std::size_t index = 1U; index < row_indices.size(); ++index)
			{
				const auto previous_key = row_key(MultiwayFullBlueprintRowView(rows[row_indices[index - 1U]]));
				const auto current_key = row_key(MultiwayFullBlueprintRowView(rows[row_indices[index]]));
				if (row_key_equal(previous_key, current_key))
					throw std::invalid_argument("multiway full blueprint has duplicate row keys");
			}
		}

		std::uint64_t serialized_size(
			std::uint64_t row_count,
			std::uint64_t action_count)
		{
			auto bytes = checked_add(kSerializedHeaderBytes, kSerializedTrailerBytes, "multiway full blueprint size is too large");
			bytes = checked_add(bytes, checked_multiply(row_count, kSerializedRowFixedBytes, "multiway full blueprint size is too large"), "multiway full blueprint size is too large");
			bytes = checked_add(bytes, checked_multiply(action_count, kSerializedActionBytes, "multiway full blueprint size is too large"), "multiway full blueprint size is too large");
			return bytes;
		}

		class BufferedEncoder
		{
		public:
			explicit BufferedEncoder(std::ofstream& out) noexcept : out_(out) {}

			void bytes(const char* data, std::size_t count)
			{
				for (std::size_t index = 0U; index < count; ++index)
					byte(static_cast<std::uint8_t>(data[index]));
			}

			void byte(std::uint8_t value)
			{
				buffer_[used_++] = static_cast<char>(value);
				++encoded_bytes_;
				if (used_ == buffer_.size())
					flush();
			}

			void u16(std::uint16_t value)
			{
				byte(static_cast<std::uint8_t>(value));
				byte(static_cast<std::uint8_t>(value >> 8U));
			}

			void u32(std::uint32_t value)
			{
				for (std::uint8_t shift = 0U; shift < 32U; shift += 8U)
					byte(static_cast<std::uint8_t>(value >> shift));
			}

			void u64(std::uint64_t value)
			{
				for (std::uint8_t shift = 0U; shift < 64U; shift += 8U)
					byte(static_cast<std::uint8_t>(value >> shift));
			}

			void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

			void flush()
			{
				if (used_ == 0U)
					return;
				out_.write(buffer_.data(), static_cast<std::streamsize>(used_));
				if (!out_)
					throw std::runtime_error("multiway full blueprint write failed");
				used_ = 0U;
			}

			[[nodiscard]] std::uint64_t encoded_bytes() const noexcept { return encoded_bytes_; }

		private:
			std::ofstream& out_;
			std::array<char, kStreamBufferBytes> buffer_{};
			std::size_t used_ = 0U;
			std::uint64_t encoded_bytes_ = 0U;
		};

		void append_initial_config(std::uint64_t& hash, const MultiwayGameConfig& config) noexcept
		{
			append_u64(hash, config.starting_stacks.size());
			for (const auto value : config.starting_stacks)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, config.initial_contributions.size());
			for (const auto value : config.initial_contributions)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, config.initial_street_contributions.size());
			for (const auto value : config.initial_street_contributions)
			{
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}
			append_u64(hash, static_cast<std::uint64_t>(config.first_player));
			append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(config.big_blind)));
			append_u64(hash, static_cast<std::uint64_t>(config.street));
			append_u64(hash, config.rake_policy.identity());
		}

		std::uint16_t quantize_probability(double probability)
		{
			if (!std::isfinite(probability) || probability < 0.0)
			{
				throw std::invalid_argument("multiway decision log has invalid probability");
			}
			const auto scaled = probability * std::numeric_limits<std::uint16_t>::max();
			if (scaled > static_cast<double>(std::numeric_limits<std::uint16_t>::max()))
			{
				throw std::invalid_argument("multiway decision log probability exceeds one");
			}
			return static_cast<std::uint16_t>(std::floor(scaled));
		}

		std::uint64_t replay_hash(const MultiwayProtectedReplayRecord& record) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, record.schema_version);
			append_identity(hash, record.identity);
			append_u64(hash, record.public_history.schema_version);
			append_u64(hash, record.public_history.hand_seed);
			append_initial_config(hash, record.public_history.initial_config);
			append_u64(hash, record.public_history.events.size());
			for (const auto& event : record.public_history.events)
			{
				append_u64(hash, static_cast<std::uint64_t>(event.kind));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.acting_seat));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.action));
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(event.decision.target_street_contribution)));
				append_u64(hash, event.decision.decision_seed);
				append_u64(hash, static_cast<std::uint64_t>(event.next_street));
				append_u64(hash, static_cast<std::uint64_t>(event.first_player));
			}
			append_u64(hash, record.decision_seeds.size());
			for (const auto seed : record.decision_seeds)
				append_u64(hash, seed);
			return finish(hash);
		}

	} // namespace

	void MultiwayFullBlueprintStreamPlan::validate() const
	{
		if (schema_version != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION)
			throw std::invalid_argument("multiway full blueprint stream has invalid schema");
		identity.validate();
		if (serialized_bytes != serialized_size(row_count, action_count))
			throw std::invalid_argument("multiway full blueprint stream size plan is invalid");
	}

	MultiwayFullBlueprintStreamPlan make_multiway_full_blueprint_stream_plan(
		const MultiwayModelIdentity& identity,
		const MultiwayBlueprintTrainingMetadata& training,
		std::uint64_t row_count,
		std::uint64_t action_count)
	{
		MultiwayFullBlueprintStreamPlan plan;
		plan.identity = identity;
		plan.training = training;
		plan.row_count = row_count;
		plan.action_count = action_count;
		plan.serialized_bytes = serialized_size(row_count, action_count);
		plan.validate();
		return plan;
	}

	void MultiwayFullBlueprintShardLayout::validate(
		const MultiwayFullBlueprintStreamPlan& plan) const
	{
		plan.validate();
		const auto fixed_bytes = checked_add(
			kSerializedHeaderBytes, kSerializedTrailerBytes,
			"multiway blueprint shard fixed size overflows");
		const auto worst_case_row_bytes = checked_add(
			kSerializedRowFixedBytes,
			checked_multiply(MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW,
				kSerializedActionBytes, "multiway blueprint shard row size overflows"),
			"multiway blueprint shard row size overflows");
		if (maximum_serialized_bytes < fixed_bytes || maximum_rows_per_shard == 0U ||
			maximum_rows_per_shard > (maximum_serialized_bytes - fixed_bytes) / worst_case_row_bytes)
			throw std::invalid_argument("multiway blueprint shard layout exceeds its byte bound");
		const auto expected_count = plan.row_count == 0U ? 0U :
			1U + (plan.row_count - 1U) / maximum_rows_per_shard;
		if (expected_count > MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS || expected_count != ranges.size())
			throw std::invalid_argument("multiway blueprint shard layout has invalid range count");
		std::uint64_t next_row = 0U;
		for (std::size_t index = 0U; index < ranges.size(); ++index)
		{
			const auto& range = ranges[index];
			const auto expected_rows = std::min(maximum_rows_per_shard, plan.row_count - next_row);
			if (range.first_row != next_row || range.row_count != expected_rows || range.row_count == 0U)
				throw std::invalid_argument("multiway blueprint shard ranges are not canonical");
			next_row = checked_add(next_row, range.row_count,
				"multiway blueprint shard range overflows");
		}
		if (next_row != plan.row_count)
			throw std::invalid_argument("multiway blueprint shard layout does not cover all rows");
	}

	MultiwayFullBlueprintShardLayout make_multiway_full_blueprint_shard_layout(
		const MultiwayFullBlueprintStreamPlan& plan,
		std::uint64_t maximum_serialized_bytes)
	{
		plan.validate();
		const auto fixed_bytes = checked_add(
			kSerializedHeaderBytes, kSerializedTrailerBytes,
			"multiway blueprint shard fixed size overflows");
		const auto worst_case_row_bytes = checked_add(
			kSerializedRowFixedBytes,
			checked_multiply(MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW,
				kSerializedActionBytes, "multiway blueprint shard row size overflows"),
			"multiway blueprint shard row size overflows");
		if (maximum_serialized_bytes < fixed_bytes ||
			(maximum_serialized_bytes - fixed_bytes) / worst_case_row_bytes == 0U)
			throw std::invalid_argument("multiway blueprint shard byte bound cannot fit one maximum row");

		MultiwayFullBlueprintShardLayout layout;
		layout.maximum_serialized_bytes = maximum_serialized_bytes;
		layout.maximum_rows_per_shard = (maximum_serialized_bytes - fixed_bytes) / worst_case_row_bytes;
		const auto shard_count = plan.row_count == 0U ? 0U :
			1U + (plan.row_count - 1U) / layout.maximum_rows_per_shard;
		if (shard_count > MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS)
			throw std::length_error("multiway blueprint shard count exceeds configured bound");
		if (shard_count > layout.ranges.max_size())
			throw std::length_error("multiway blueprint shard count exceeds addressable memory");
		layout.ranges.reserve(static_cast<std::size_t>(shard_count));
		for (std::uint64_t first_row = 0U; first_row < plan.row_count;)
		{
			const auto rows = std::min(layout.maximum_rows_per_shard, plan.row_count - first_row);
			layout.ranges.push_back({ first_row, rows });
			first_row = checked_add(first_row, rows,
				"multiway blueprint shard range overflows");
		}
		layout.validate(plan);
		return layout;
	}

	MultiwayFullBlueprintShardedSizeEstimate estimate_multiway_full_blueprint_sharded_size(
		const MultiwayFullBlueprintStreamPlan& plan,
		const MultiwayFullBlueprintShardLayout& layout,
		const std::filesystem::path& manifest_path)
	{
		plan.validate();
		layout.validate(plan);
		if (manifest_path.empty() || layout.ranges.empty())
			throw std::invalid_argument("multiway sharded size estimate requires a destination and shards");

		MultiwayFullBlueprintShardedSizeEstimate estimate;
		const auto shard_overhead = checked_add(kSerializedHeaderBytes, kSerializedTrailerBytes,
			"multiway shard size estimate overflows");
		estimate.shard_bytes = checked_add(plan.serialized_bytes,
			checked_multiply(static_cast<std::uint64_t>(layout.ranges.size() - 1U), shard_overhead,
				"multiway shard size estimate overflows"),
			"multiway shard size estimate overflows");

		std::uint64_t identity_fields = 0U;
		visit_multiway_model_identity_fields(plan.identity, [&](std::uint64_t) { ++identity_fields; });
		estimate.manifest_bytes = checked_add(112U,
			checked_multiply(identity_fields, sizeof(std::uint64_t),
				"multiway manifest size estimate overflows"),
			"multiway manifest size estimate overflows");
		const auto filename_bytes = manifest_path.filename().generic_u8string().size();
		const auto generation_name_bytes = checked_add(filename_bytes, 49U,
			"multiway shard path estimate overflows");
		for (std::size_t index = 0U; index < layout.ranges.size(); ++index)
		{
			std::uint64_t digits = 1U;
			for (auto value = index; value >= 10U; value /= 10U)
				++digits;
			const auto relative_path_bytes = checked_add(generation_name_bytes,
				checked_add(12U, digits, "multiway shard path estimate overflows"),
				"multiway shard path estimate overflows");
			if (relative_path_bytes > std::numeric_limits<std::uint32_t>::max())
				throw std::length_error("multiway shard path exceeds manifest format");
			const auto shard_entry_bytes = checked_add(44U, relative_path_bytes,
				"multiway manifest size estimate overflows");
			estimate.manifest_bytes = checked_add(estimate.manifest_bytes, shard_entry_bytes,
				"multiway manifest size estimate overflows");
		}
		estimate.total_bytes = checked_add(estimate.shard_bytes, estimate.manifest_bytes,
			"multiway sharded output size estimate overflows");
		return estimate;
	}

	void MultiwayFullBlueprintShardManifest::validate() const
	{
		if (schema_version != MULTIWAY_FULL_BLUEPRINT_SHARD_MANIFEST_SCHEMA_VERSION ||
			policy_content_hash == 0U || shards.empty())
			throw std::invalid_argument("multiway full blueprint shard manifest has invalid metadata");
		identity.validate();
		const auto full_plan = make_multiway_full_blueprint_stream_plan(
			identity, training, row_count, action_count);
		const auto layout = make_multiway_full_blueprint_shard_layout(
			full_plan, maximum_serialized_bytes);
		if (layout.ranges.size() != shards.size())
			throw std::invalid_argument("multiway full blueprint shard manifest count is inconsistent");
		std::uint64_t action_total = 0U;
		for (std::size_t index = 0U; index < shards.size(); ++index)
		{
			const auto& shard = shards[index];
			if (!safe_relative_shard_path(shard.relative_path) ||
				shard.range.first_row != layout.ranges[index].first_row ||
				shard.range.row_count != layout.ranges[index].row_count ||
				shard.action_count < shard.range.row_count || shard.payload_hash == 0U)
				throw std::invalid_argument("multiway full blueprint shard reference is invalid");
			const auto shard_plan = make_multiway_full_blueprint_stream_plan(
				identity, training, shard.range.row_count, shard.action_count);
			if (shard.serialized_bytes != shard_plan.serialized_bytes ||
				shard.serialized_bytes > maximum_serialized_bytes)
				throw std::invalid_argument("multiway full blueprint shard size is inconsistent");
			action_total = checked_add(action_total, shard.action_count,
				"multiway full blueprint shard action total overflows");
		}
		if (action_total != action_count)
			throw std::invalid_argument("multiway full blueprint shard action total is inconsistent");
	}

	void MultiwayFullBlueprintPayloadHasher::begin(
		std::uint32_t schema_version,
		const MultiwayModelIdentity& identity,
		const MultiwayBlueprintTrainingMetadata& training,
		std::uint64_t row_count) noexcept
	{
		hash_ = texas::core::fingerprint::FNV1A_OFFSET;
		append_u64(hash_, schema_version);
		append_identity(hash_, identity);
		append_training(hash_, training);
		append_u64(hash_, row_count);
	}

	void MultiwayFullBlueprintPayloadHasher::append_row(
		const MultiwayFullBlueprintRowView& row) noexcept
	{
		append_policy_row(hash_, row);
	}

	std::uint64_t MultiwayFullBlueprintPayloadHasher::finish() const noexcept
	{
		return texas::core::fingerprint::finish(hash_);
	}

	void MultiwayFullBlueprintPolicyContentHasher::begin(std::uint64_t row_count) noexcept
	{
		hash_ = texas::core::fingerprint::FNV1A_OFFSET;
		append_u64(hash_, MULTIWAY_POLICY_CONTENT_HASH_VERSION);
		append_u64(hash_, row_count);
	}

	void MultiwayFullBlueprintPolicyContentHasher::append_row(
		const MultiwayFullBlueprintRowView& row) noexcept
	{
		append_policy_row(hash_, row);
	}

	std::uint64_t MultiwayFullBlueprintPolicyContentHasher::finish() const noexcept
	{
		return texas::core::fingerprint::finish(hash_);
	}

	void write_stream_header(
		BufferedEncoder& encoder,
		const MultiwayFullBlueprintStreamPlan& plan)
	{
		encoder.bytes(kFullBlueprintMagic.data(), kFullBlueprintMagic.size());
		encoder.u32(plan.schema_version);
		visit_multiway_model_identity_fields(plan.identity, [&](std::uint64_t field) {
			encoder.u64(field);
		});
		encoder.u64(plan.training.batches);
		encoder.u64(plan.training.trajectories);
		encoder.u64(plan.training.deterministic_seed);
		encoder.u64(plan.training.late_window_start_batch);
		encoder.u64(plan.training.schedule_hash);
		encoder.u64(plan.training.pruned_negative_regrets);
		encoder.byte(plan.training.linear_iteration_weighting);
		encoder.byte(plan.training.discounting_enabled);
		encoder.byte(plan.training.negative_regret_pruning_enabled);
		encoder.byte(plan.training.reserved);
		encoder.u64(plan.row_count);
	}

	void write_stream_row(
		BufferedEncoder& encoder,
		const MultiwayFullBlueprintRowView& row)
	{
		encoder.u64(row.infoset.public_state.value);
		encoder.i32(row.infoset.seat);
		encoder.u32(row.bucket);
		encoder.u64(row.action_menu_id);
		encoder.u32(static_cast<std::uint32_t>(row.action_count));
		for (std::size_t index = 0U; index < row.action_count; ++index)
		{
			const auto& action = row.actions[index];
			encoder.byte(static_cast<std::uint8_t>(action.action.action));
			encoder.u32(action.action.action_index);
			encoder.i32(action.action.target_street_contribution);
			encoder.u64(action.action.action_menu_id);
			encoder.u16(action.probability);
		}
	}

	struct TemporaryFileCleanup
	{
		explicit TemporaryFileCleanup(std::filesystem::path file) : file(std::move(file)) {}
		~TemporaryFileCleanup() noexcept
		{
			if (!active)
				return;
			std::error_code error;
			std::filesystem::remove(file, error);
		}

		std::filesystem::path file;
		bool active = true;
	};

	struct TemporaryDirectoryCleanup
	{
		explicit TemporaryDirectoryCleanup(std::filesystem::path directory)
			: directory(std::move(directory))
		{
		}
		~TemporaryDirectoryCleanup() noexcept
		{
			if (!active)
				return;
			std::error_code error;
			std::filesystem::remove_all(directory, error);
		}

		std::filesystem::path directory;
		bool active = true;
	};

	struct MultiwayFullBlueprintStreamWriter::Impl
	{
		Impl(
			const std::filesystem::path& destination_path,
			const MultiwayFullBlueprintStreamPlan& stream_plan,
			MultiwayFullBlueprintStreamOptions stream_options)
			: destination(destination_path), temporary(destination_path.string() + ".tmp"), plan(stream_plan), options(stream_options), encoder(output)
		{
			plan.validate();
			cleanup = std::make_unique<TemporaryFileCleanup>(temporary);
			output.open(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("multiway full blueprint temporary file cannot be opened");
			hasher.begin(plan.schema_version, plan.identity, plan.training, plan.row_count);
			policy_hasher.begin(plan.row_count);
			write_stream_header(encoder, plan);
		}

		~Impl()
		{
			if (output.is_open())
				output.close();
		}

		std::filesystem::path destination;
		std::filesystem::path temporary;
		MultiwayFullBlueprintStreamPlan plan;
		MultiwayFullBlueprintStreamOptions options;
		std::ofstream output;
		BufferedEncoder encoder;
		std::unique_ptr<TemporaryFileCleanup> cleanup;
		MultiwayFullBlueprintPayloadHasher hasher;
		MultiwayFullBlueprintPolicyContentHasher policy_hasher;
		RowKey previous_key{};
		bool has_previous_key = false;
		bool finished = false;
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
	};

	void MultiwayFullBlueprintArtifact::validate() const
	{
		if (schema_version != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION || payload_hash == 0U)
		{
			throw std::invalid_argument("multiway full blueprint has invalid schema or hash");
		}
		identity.validate();
		validate_materialized_rows(rows);
		if (payload_hash != MultiwayFullBlueprintArtifacts::payload_hash(*this))
		{
			throw std::invalid_argument("multiway full blueprint payload hash does not match");
		}
		if (MultiwayFullBlueprintArtifacts::policy_content_hash(*this) == 0U)
		{
			throw std::invalid_argument("multiway full blueprint policy content hash is invalid");
		}
	}

	MultiwayMemoryComponent MultiwayFullBlueprintArtifact::memory_snapshot() const noexcept
	{
		MultiwayMemoryComponent result;
		result.add_buffer(rows.size(), rows.capacity(), sizeof(MultiwayBlueprintRow));
		for (const auto& row : rows)
			result.add_buffer(row.actions.size(), row.actions.capacity(), sizeof(MultiwayQuantizedRootAction));
		return result;
	}

	std::uint64_t MultiwayFullBlueprintArtifacts::payload_hash(
		const MultiwayFullBlueprintArtifact& artifact) noexcept
	{
		MultiwayFullBlueprintPayloadHasher hasher;
		hasher.begin(artifact.schema_version, artifact.identity, artifact.training, artifact.rows.size());
		for (const auto& row : artifact.rows)
			hasher.append_row(MultiwayFullBlueprintRowView(row));
		return hasher.finish();
	}

	std::uint64_t MultiwayFullBlueprintArtifacts::policy_content_hash(
		const MultiwayFullBlueprintArtifact& artifact)
	{
		validate_materialized_rows(artifact.rows);
		std::vector<std::size_t> row_indices(artifact.rows.size());
		for (std::size_t index = 0U; index < row_indices.size(); ++index)
			row_indices[index] = index;
		std::sort(row_indices.begin(), row_indices.end(), [&](std::size_t left, std::size_t right) {
			const auto left_key = row_key(MultiwayFullBlueprintRowView(artifact.rows[left]));
			const auto right_key = row_key(MultiwayFullBlueprintRowView(artifact.rows[right]));
			if (row_key_less(left_key, right_key))
				return true;
			if (row_key_less(right_key, left_key))
				return false;
			return left < right;
		});
		MultiwayFullBlueprintPolicyContentHasher hasher;
		hasher.begin(row_indices.size());
		for (const auto index : row_indices)
			hasher.append_row(MultiwayFullBlueprintRowView(artifact.rows[index]));
		return hasher.finish();
	}

	MultiwayFullBlueprintStreamWriter::MultiwayFullBlueprintStreamWriter(
		const std::filesystem::path& destination,
		const MultiwayFullBlueprintStreamPlan& plan,
		MultiwayFullBlueprintStreamOptions options)
		: impl_(std::make_unique<Impl>(destination, plan, options))
	{
	}

	MultiwayFullBlueprintStreamWriter::~MultiwayFullBlueprintStreamWriter() = default;

	void MultiwayFullBlueprintStreamWriter::write_row(
		const MultiwayFullBlueprintRowView& row)
	{
		if (impl_ == nullptr || impl_->finished)
			throw std::logic_error("multiway full blueprint stream is already finished");
		if (impl_->options.failure_point == MultiwayFullBlueprintStreamFailurePoint::Write)
			throw std::runtime_error("injected multiway full blueprint write failure");
		validate_row(row);
		if (impl_->row_count >= impl_->plan.row_count)
			throw std::length_error("multiway full blueprint emitted too many rows");
		const auto row_actions = static_cast<std::uint64_t>(row.action_count);
		if (row_actions > impl_->plan.action_count - impl_->action_count)
			throw std::length_error("multiway full blueprint emitted too many actions");
		const auto current_key = row_key(row);
		if (impl_->options.require_strict_row_order && impl_->has_previous_key
			&& !row_key_less(impl_->previous_key, current_key))
		{
			throw std::invalid_argument("multiway full blueprint emitted row keys are not strictly increasing");
		}

		impl_->hasher.append_row(row);
		impl_->policy_hasher.append_row(row);
		write_stream_row(impl_->encoder, row);
		impl_->previous_key = current_key;
		impl_->has_previous_key = true;
		++impl_->row_count;
		impl_->action_count += row_actions;
	}

	MultiwayFullBlueprintStreamResult MultiwayFullBlueprintStreamWriter::finish()
	{
		if (impl_ == nullptr || impl_->finished)
			throw std::logic_error("multiway full blueprint stream is already finished");
		if (impl_->row_count != impl_->plan.row_count || impl_->action_count != impl_->plan.action_count)
			throw std::invalid_argument("multiway full blueprint emitted counts do not match the plan");

		const auto payload_hash = impl_->hasher.finish();
		impl_->encoder.u64(payload_hash);
		impl_->encoder.flush();
		if (impl_->encoder.encoded_bytes() != impl_->plan.serialized_bytes)
			throw std::runtime_error("multiway full blueprint serialized size does not match the plan");
		impl_->output.flush();
		if (!impl_->output)
			throw std::runtime_error("multiway full blueprint write flush failed");
		if (impl_->options.failure_point == MultiwayFullBlueprintStreamFailurePoint::Close)
			throw std::runtime_error("injected multiway full blueprint close failure");
		impl_->output.close();
		if (!impl_->output)
			throw std::runtime_error("multiway full blueprint close failed");
		if (impl_->options.failure_point == MultiwayFullBlueprintStreamFailurePoint::Publication)
			throw std::runtime_error("injected multiway full blueprint publication failure");

		const auto publication_mode = texas::core::publish_atomic_replace(
			impl_->temporary,
			impl_->destination,
			"multiway full blueprint publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectCopyFallback);
		if (publication_mode == texas::core::AtomicPublishMode::CopyFallback)
			throw std::runtime_error("multiway full blueprint publication was not atomic");
		impl_->cleanup->active = false;
		impl_->finished = true;
		return { impl_->row_count, impl_->action_count, impl_->encoder.encoded_bytes(), payload_hash, impl_->policy_hasher.finish(), publication_mode };
	}

	struct MultiwayFullBlueprintShardedStreamWriter::Impl
	{
		Impl(const std::filesystem::path& destination_path,
			const MultiwayFullBlueprintStreamPlan& stream_plan,
			MultiwayFullBlueprintShardLayout shard_layout,
			std::vector<std::uint64_t> per_shard_action_counts,
			MultiwayFullBlueprintStreamOptions stream_options)
			: manifest_path(destination_path), plan(stream_plan), layout(std::move(shard_layout)),
			  shard_action_counts(std::move(per_shard_action_counts)), options(stream_options)
		{
			plan.validate();
			layout.validate(plan);
			if (manifest_path.empty() || plan.row_count == 0U || !options.require_strict_row_order ||
				shard_action_counts.size() != layout.ranges.size())
				throw std::invalid_argument("multiway sharded stream writer configuration is invalid");
			std::uint64_t total_actions = 0U;
			for (std::size_t index = 0U; index < shard_action_counts.size(); ++index)
			{
				const auto rows = layout.ranges[index].row_count;
				const auto maximum_actions = checked_multiply(rows, MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW,
					"multiway shard action capacity overflows");
				if (shard_action_counts[index] < rows || shard_action_counts[index] > maximum_actions)
					throw std::invalid_argument("multiway shard action count is outside the row bounds");
				total_actions = checked_add(total_actions, shard_action_counts[index],
					"multiway shard action total overflows");
			}
			if (total_actions != plan.action_count)
				throw std::invalid_argument("multiway shard action counts do not match the stream plan");

			std::error_code directory_error;
			const auto parent = manifest_path.parent_path().empty()
				? std::filesystem::current_path(directory_error)
				: manifest_path.parent_path();
			if (directory_error || !std::filesystem::is_directory(parent, directory_error) || directory_error)
				throw std::runtime_error("multiway blueprint shard output directory is unavailable");
			static std::atomic<std::uint64_t> generation_sequence{ 0U };
			bool created = false;
			for (std::uint32_t attempt = 0U; attempt < 1024U; ++attempt)
			{
				generation_name = manifest_path.filename().u8string() + ".shards-" +
					fixed_width_u64(static_cast<std::uint64_t>(
						std::chrono::steady_clock::now().time_since_epoch().count())) + "-" +
					fixed_width_u64(generation_sequence.fetch_add(1U, std::memory_order_relaxed));
				generation_directory = parent / std::filesystem::u8path(generation_name);
				if (std::filesystem::create_directory(generation_directory, directory_error))
				{
					created = true;
					break;
				}
				if (directory_error)
					throw std::runtime_error("multiway blueprint shard generation directory cannot be created");
			}
			if (!created)
				throw std::runtime_error("multiway blueprint shard generation name is unavailable");
			directory_cleanup = std::make_unique<TemporaryDirectoryCleanup>(generation_directory);

			manifest.identity = plan.identity;
			manifest.training = plan.training;
			manifest.row_count = plan.row_count;
			manifest.action_count = plan.action_count;
			manifest.maximum_serialized_bytes = layout.maximum_serialized_bytes;
			manifest.shards.reserve(layout.ranges.size());
			policy_hasher.begin(plan.row_count);
		}

		void begin_shard()
		{
			const auto range = layout.ranges[current_shard];
			const auto shard_plan = make_multiway_full_blueprint_stream_plan(
				plan.identity, plan.training, range.row_count, shard_action_counts[current_shard]);
			shard_name = "shard-" + std::to_string(current_shard) + ".mwfb";
			shard_writer = std::make_unique<MultiwayFullBlueprintStreamWriter>(
				generation_directory / shard_name, shard_plan, options);
			current_shard_rows = 0U;
		}

		void finish_shard()
		{
			const auto result = shard_writer->finish();
			if (result.serialized_bytes > layout.maximum_serialized_bytes)
				throw std::length_error("multiway full blueprint shard exceeds its byte bound");
			total_serialized_bytes = checked_add(total_serialized_bytes, result.serialized_bytes,
				"multiway blueprint sharded output size overflows");
			const auto relative = (std::filesystem::path(generation_name) / shard_name).generic_u8string();
			manifest.shards.push_back({ relative, layout.ranges[current_shard], result.action_count,
				result.serialized_bytes, result.payload_hash });
			shard_writer.reset();
			++current_shard;
		}

		std::filesystem::path manifest_path;
		MultiwayFullBlueprintStreamPlan plan;
		MultiwayFullBlueprintShardLayout layout;
		std::vector<std::uint64_t> shard_action_counts;
		MultiwayFullBlueprintStreamOptions options;
		MultiwayFullBlueprintShardManifest manifest;
		MultiwayFullBlueprintPolicyContentHasher policy_hasher;
		std::unique_ptr<TemporaryDirectoryCleanup> directory_cleanup;
		std::unique_ptr<MultiwayFullBlueprintStreamWriter> shard_writer;
		std::filesystem::path generation_directory;
		std::string generation_name;
		std::string shard_name;
		RowKey previous_key{};
		bool has_previous_key = false;
		bool finished = false;
		std::size_t current_shard = 0U;
		std::uint64_t current_shard_rows = 0U;
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t total_serialized_bytes = 0U;
	};

	MultiwayFullBlueprintShardedStreamWriter::MultiwayFullBlueprintShardedStreamWriter(
		const std::filesystem::path& manifest_path,
		const MultiwayFullBlueprintStreamPlan& plan,
		MultiwayFullBlueprintShardLayout layout,
		std::vector<std::uint64_t> shard_action_counts,
		MultiwayFullBlueprintStreamOptions options)
		: impl_(std::make_unique<Impl>(manifest_path, plan, std::move(layout),
			std::move(shard_action_counts), options))
	{
	}

	MultiwayFullBlueprintShardedStreamWriter::~MultiwayFullBlueprintShardedStreamWriter() = default;

	void MultiwayFullBlueprintShardedStreamWriter::write_row(const MultiwayFullBlueprintRowView& row)
	{
		if (impl_ == nullptr || impl_->finished || impl_->row_count >= impl_->plan.row_count ||
			impl_->current_shard >= impl_->layout.ranges.size())
			throw std::logic_error("multiway sharded blueprint stream is closed or complete");
		validate_row(row);
		const auto key = row_key(row);
		if (impl_->has_previous_key && !row_key_less(impl_->previous_key, key))
			throw std::invalid_argument("multiway sharded blueprint rows are not strictly ordered");
		if (impl_->shard_writer == nullptr)
			impl_->begin_shard();
		impl_->policy_hasher.append_row(row);
		impl_->shard_writer->write_row(row);
		impl_->previous_key = key;
		impl_->has_previous_key = true;
		++impl_->row_count;
		++impl_->current_shard_rows;
		impl_->action_count = checked_add(impl_->action_count, row.action_count,
			"multiway sharded blueprint action count overflows");
		if (impl_->current_shard_rows == impl_->layout.ranges[impl_->current_shard].row_count)
			impl_->finish_shard();
	}

	MultiwayFullBlueprintShardedStreamResult MultiwayFullBlueprintShardedStreamWriter::finish()
	{
		if (impl_ == nullptr || impl_->finished || impl_->row_count != impl_->plan.row_count ||
			impl_->action_count != impl_->plan.action_count || impl_->current_shard != impl_->layout.ranges.size())
			throw std::invalid_argument("multiway sharded blueprint stream counts do not match its plan");
		impl_->manifest.policy_content_hash = impl_->policy_hasher.finish();
		impl_->manifest.validate();
		const auto manifest_hash = shard_manifest_hash(impl_->manifest);
		const auto temporary_manifest = impl_->manifest_path.string() + ".tmp." + impl_->generation_name;
		TemporaryFileCleanup manifest_cleanup(temporary_manifest);
		{
			std::ofstream out(temporary_manifest, std::ios::binary | std::ios::trunc);
			if (!out)
				throw std::runtime_error("multiway blueprint shard manifest temporary cannot be opened");
			out.write(kShardedBlueprintMagic.data(), static_cast<std::streamsize>(kShardedBlueprintMagic.size()));
			portable::write_u32(out, impl_->manifest.schema_version);
			write_identity(out, impl_->manifest.identity);
			write_training(out, impl_->manifest.training);
			portable::write_u64(out, impl_->manifest.row_count);
			portable::write_u64(out, impl_->manifest.action_count);
			portable::write_u64(out, impl_->manifest.maximum_serialized_bytes);
			portable::write_u64(out, impl_->manifest.policy_content_hash);
			portable::write_u64(out, impl_->manifest.shards.size());
			for (const auto& shard : impl_->manifest.shards)
			{
				portable::write_u32(out, static_cast<std::uint32_t>(shard.relative_path.size()));
				out.write(shard.relative_path.data(), static_cast<std::streamsize>(shard.relative_path.size()));
				portable::write_u64(out, shard.range.first_row);
				portable::write_u64(out, shard.range.row_count);
				portable::write_u64(out, shard.action_count);
				portable::write_u64(out, shard.serialized_bytes);
				portable::write_u64(out, shard.payload_hash);
			}
			portable::write_u64(out, manifest_hash);
			out.close();
			if (!out)
				throw std::runtime_error("multiway blueprint shard manifest write failed");
		}
		std::error_code size_error;
		const auto manifest_bytes = std::filesystem::file_size(temporary_manifest, size_error);
		if (size_error)
			throw std::runtime_error("multiway blueprint shard manifest size cannot be determined");
		const auto total_bytes = checked_add(impl_->total_serialized_bytes, manifest_bytes,
			"multiway blueprint sharded output size overflows");
		if (impl_->options.failure_point == MultiwayFullBlueprintStreamFailurePoint::ManifestPublication)
			throw std::runtime_error("injected multiway blueprint shard manifest publication failure");
		const auto publication_mode = texas::core::publish_atomic_replace(
			temporary_manifest, impl_->manifest_path,
			"multiway blueprint shard manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectCopyFallback);
		if (publication_mode == texas::core::AtomicPublishMode::CopyFallback)
			throw std::runtime_error("multiway blueprint shard manifest publication was not atomic");
		manifest_cleanup.active = false;
		impl_->directory_cleanup->active = false;
		impl_->finished = true;
		return { impl_->row_count, impl_->action_count, total_bytes,
			impl_->manifest.policy_content_hash,
			static_cast<std::uint64_t>(impl_->manifest.shards.size()), manifest_bytes, publication_mode };
	}

	MultiwayFullBlueprintStreamResult MultiwayFullBlueprintArtifacts::save_atomic_streaming(
		const std::filesystem::path& path,
		const MultiwayFullBlueprintStreamPlan& plan,
		bool (*produce_row)(std::uint64_t, MultiwayFullBlueprintRowView&, void*),
		void* context)
	{
		plan.validate();
		if (produce_row == nullptr)
			throw std::invalid_argument("multiway full blueprint row producer is missing");
		MultiwayFullBlueprintStreamWriter writer(path, plan);
		for (std::uint64_t index = 0U; index < plan.row_count; ++index)
		{
			MultiwayFullBlueprintRowView row;
			if (!produce_row(index, row, context))
				throw std::runtime_error("multiway full blueprint row production failed");
			writer.write_row(row);
		}
		return writer.finish();
	}

	MultiwayFullBlueprintShardedStreamResult MultiwayFullBlueprintArtifacts::save_atomic_sharded_streaming(
		const std::filesystem::path& manifest_path,
		const MultiwayFullBlueprintStreamPlan& plan,
		std::uint64_t maximum_shard_bytes,
		bool (*produce_row)(std::uint64_t, MultiwayFullBlueprintRowView&, void*),
		void* context,
		MultiwayFullBlueprintStreamOptions options)
	{
		plan.validate();
		if (manifest_path.empty() || produce_row == nullptr || plan.row_count == 0U ||
			!options.require_strict_row_order)
			throw std::invalid_argument("multiway sharded export request is incomplete");
		const auto layout = make_multiway_full_blueprint_shard_layout(plan, maximum_shard_bytes);
		std::vector<std::uint64_t> shard_actions(layout.ranges.size(), 0U);
		MultiwayFullBlueprintPolicyContentHasher first_pass_hasher;
		first_pass_hasher.begin(plan.row_count);
		RowKey previous{};
		bool has_previous = false;
		std::uint64_t action_total = 0U;
		for (std::uint64_t index = 0U; index < plan.row_count; ++index)
		{
			MultiwayFullBlueprintRowView row;
			if (!produce_row(index, row, context))
				throw std::runtime_error("multiway full blueprint shard planning row production failed");
			validate_row(row);
			const auto key = row_key(row);
			if (has_previous && !row_key_less(previous, key))
				throw std::invalid_argument("multiway full blueprint shard rows are not strictly ordered");
			previous = key;
			has_previous = true;
			first_pass_hasher.append_row(row);
			const auto row_actions = static_cast<std::uint64_t>(row.action_count);
			action_total = checked_add(action_total, row_actions,
				"multiway full blueprint shard action count overflows");
			const auto shard_index = static_cast<std::size_t>(index / layout.maximum_rows_per_shard);
			shard_actions[shard_index] = checked_add(shard_actions[shard_index], row_actions,
				"multiway full blueprint shard action count overflows");
		}
		if (action_total != plan.action_count)
			throw std::invalid_argument("multiway full blueprint shard plan action count is inconsistent");
		const auto expected_policy_hash = first_pass_hasher.finish();
		MultiwayFullBlueprintShardedStreamWriter writer(
			manifest_path, plan, layout, std::move(shard_actions), options);
		MultiwayFullBlueprintPolicyContentHasher second_pass_hasher;
		second_pass_hasher.begin(plan.row_count);
		for (std::uint64_t index = 0U; index < plan.row_count; ++index)
		{
			MultiwayFullBlueprintRowView row;
			if (!produce_row(index, row, context))
				throw std::runtime_error("multiway full blueprint shard writing row production failed");
			validate_row(row);
			second_pass_hasher.append_row(row);
			writer.write_row(row);
		}
		if (second_pass_hasher.finish() != expected_policy_hash)
			throw std::runtime_error("multiway full blueprint rows changed between shard planning and writing");
		return writer.finish();
	}

	void MultiwayFullBlueprintArtifacts::save_atomic(
		const std::filesystem::path& path,
		const MultiwayFullBlueprintArtifact& artifact)
	{
		if (artifact.schema_version != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION)
			throw std::invalid_argument("multiway full blueprint has invalid schema");
		artifact.identity.validate();
		validate_materialized_rows(artifact.rows);
		const auto plan = make_multiway_full_blueprint_stream_plan(
			artifact.identity,
			artifact.training,
			artifact.rows.size(),
			materialized_action_count(artifact.rows));
		MultiwayFullBlueprintStreamWriter writer(
			path,
			plan,
			{ false });
		for (const auto& row : artifact.rows)
			writer.write_row(MultiwayFullBlueprintRowView(row));
		(void)writer.finish();
	}

	MultiwayFullBlueprintShardManifest MultiwayFullBlueprintArtifacts::load_sharded_manifest(
		const std::filesystem::path& manifest_path,
		const MultiwayModelIdentity& expected_identity)
	{
		constexpr std::uint64_t maximum_manifest_bytes = 64U * 1024U * 1024U;
		std::error_code size_error;
		const auto file_bytes = std::filesystem::file_size(manifest_path, size_error);
		if (size_error || file_bytes > maximum_manifest_bytes)
			throw std::runtime_error("multiway blueprint shard manifest size is unavailable or too large");
		std::ifstream in(manifest_path, std::ios::binary);
		if (!in)
			throw std::runtime_error("multiway blueprint shard manifest cannot be opened");
		std::array<char, kShardedBlueprintMagic.size()> magic{};
		in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
		if (!in || magic != kShardedBlueprintMagic)
			throw std::runtime_error("multiway blueprint shard manifest schema is invalid");
		MultiwayFullBlueprintShardManifest manifest;
		if (!portable::read_u32(in, manifest.schema_version))
			throw std::runtime_error("multiway blueprint shard manifest is truncated");
		manifest.identity = read_identity(in);
		manifest.training = read_training(in);
		std::uint64_t shard_count = 0U;
		if (!portable::read_u64(in, manifest.row_count) ||
			!portable::read_u64(in, manifest.action_count) ||
			!portable::read_u64(in, manifest.maximum_serialized_bytes) ||
			!portable::read_u64(in, manifest.policy_content_hash) ||
			!portable::read_u64(in, shard_count) || shard_count == 0U ||
			shard_count > MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS)
			throw std::runtime_error("multiway blueprint shard manifest has invalid counts");
		if (shard_count > manifest.shards.max_size())
			throw std::length_error("multiway blueprint shard manifest exceeds addressable memory");
		manifest.shards.reserve(static_cast<std::size_t>(shard_count));
		for (std::uint64_t index = 0U; index < shard_count; ++index)
		{
			std::uint32_t path_bytes = 0U;
			if (!portable::read_u32(in, path_bytes) || path_bytes == 0U || path_bytes > 4096U)
				throw std::runtime_error("multiway blueprint shard path has invalid length");
			MultiwayFullBlueprintShardReference shard;
			shard.relative_path.resize(path_bytes);
			in.read(shard.relative_path.data(), static_cast<std::streamsize>(path_bytes));
			if (!in || !portable::read_u64(in, shard.range.first_row) ||
				!portable::read_u64(in, shard.range.row_count) ||
				!portable::read_u64(in, shard.action_count) ||
				!portable::read_u64(in, shard.serialized_bytes) ||
				!portable::read_u64(in, shard.payload_hash))
				throw std::runtime_error("multiway blueprint shard manifest is truncated");
			manifest.shards.push_back(std::move(shard));
		}
		std::uint64_t stored_hash = 0U;
		if (!portable::read_u64(in, stored_hash) || in.peek() != std::char_traits<char>::eof())
			throw std::runtime_error("multiway blueprint shard manifest has trailing or missing data");
		manifest.validate();
		expected_identity.validate();
		if (manifest.identity != expected_identity || shard_manifest_hash(manifest) != stored_hash)
			throw std::invalid_argument("multiway blueprint shard manifest identity or hash mismatch");
		return manifest;
	}

	void MultiwayFullBlueprintArtifacts::verify_sharded_artifact(
		const std::filesystem::path& manifest_path,
		const MultiwayModelIdentity& expected_identity)
	{
		const auto manifest = load_sharded_manifest(manifest_path, expected_identity);
		MultiwayFullBlueprintPolicyContentHasher policy_hasher;
		policy_hasher.begin(manifest.row_count);
		std::uint64_t total_actions = 0U;
		RowKey previous_key{};
		bool has_previous_key = false;
		std::error_code root_error;
		const auto root_candidate = manifest_path.parent_path().empty()
			? std::filesystem::current_path(root_error)
			: manifest_path.parent_path();
		const auto canonical_root = std::filesystem::weakly_canonical(root_candidate, root_error);
		if (root_error)
			throw std::runtime_error("multiway blueprint shard root cannot be resolved");
		for (const auto& shard : manifest.shards)
		{
			const auto path = manifest_path.parent_path() / std::filesystem::u8path(shard.relative_path);
			std::error_code size_error;
			const auto canonical_shard = std::filesystem::weakly_canonical(path, size_error);
			if (size_error)
				throw std::runtime_error("multiway blueprint shard path cannot be resolved");
			const auto relative_shard = canonical_shard.lexically_relative(canonical_root);
			if (relative_shard.empty() || relative_shard.is_absolute() ||
				std::any_of(relative_shard.begin(), relative_shard.end(), [](const auto& part) {
					return part == "..";
				}))
				throw std::invalid_argument("multiway blueprint shard escapes its manifest directory");
			const auto bytes = std::filesystem::file_size(path, size_error);
			if (size_error || bytes != shard.serialized_bytes || bytes > manifest.maximum_serialized_bytes)
				throw std::runtime_error("multiway blueprint shard is missing or has an invalid size");
			std::ifstream in(path, std::ios::binary);
			if (!in)
				throw std::runtime_error("multiway blueprint shard cannot be opened");
			std::array<char, kFullBlueprintMagic.size()> magic{};
			in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
			if (!in || magic != kFullBlueprintMagic)
				throw std::runtime_error("multiway blueprint shard schema is invalid");
			std::uint32_t schema = 0U;
			if (!portable::read_u32(in, schema))
				throw std::runtime_error("multiway blueprint shard is truncated");
			const auto identity = read_identity(in);
			const auto training = read_training(in);
			std::uint64_t row_count = 0U;
			if (!portable::read_u64(in, row_count) || schema != MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION ||
				identity != manifest.identity || !same_stream_training(training, manifest.training) ||
				row_count != shard.range.row_count)
				throw std::invalid_argument("multiway blueprint shard identity or range mismatch");
			MultiwayFullBlueprintPayloadHasher payload_hasher;
			payload_hasher.begin(schema, identity, training, row_count);
			std::uint64_t shard_actions = 0U;
			for (std::uint64_t row_index = 0U; row_index < row_count; ++row_index)
			{
				MultiwayBlueprintRow row;
				row.infoset = read_infoset(in);
				std::uint32_t action_count = 0U;
				if (!portable::read_u32(in, row.bucket) || !portable::read_u64(in, row.action_menu_id) ||
					!portable::read_u32(in, action_count) || action_count == 0U ||
					action_count > MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW)
					throw std::runtime_error("multiway blueprint shard row is invalid or truncated");
				std::array<MultiwayQuantizedRootAction, MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW> actions{};
				for (std::uint32_t action_index = 0U; action_index < action_count; ++action_index)
					actions[action_index] = read_action(in);
				MultiwayFullBlueprintRowView row_view;
				row_view.infoset = row.infoset;
				row_view.bucket = row.bucket;
				row_view.action_menu_id = row.action_menu_id;
				row_view.actions = actions.data();
				row_view.action_count = action_count;
				validate_row(row_view);
				const auto key = row_key(row_view);
				if (has_previous_key && !row_key_less(previous_key, key))
					throw std::invalid_argument("multiway blueprint shard rows are not globally ordered");
				previous_key = key;
				has_previous_key = true;
				payload_hasher.append_row(row_view);
				policy_hasher.append_row(row_view);
				shard_actions = checked_add(shard_actions, action_count,
					"multiway blueprint shard action count overflows");
			}
			std::uint64_t stored_payload_hash = 0U;
			if (!portable::read_u64(in, stored_payload_hash) || in.peek() != std::char_traits<char>::eof() ||
				stored_payload_hash == 0U || stored_payload_hash != payload_hasher.finish() ||
				stored_payload_hash != shard.payload_hash || shard_actions != shard.action_count)
				throw std::invalid_argument("multiway blueprint shard hash or counts do not match");
			total_actions = checked_add(total_actions, shard_actions,
				"multiway blueprint total action count overflows");
		}
		if (total_actions != manifest.action_count || policy_hasher.finish() != manifest.policy_content_hash)
			throw std::invalid_argument("multiway blueprint shard aggregate hash or counts do not match");
	}

	MultiwayFullBlueprintArtifact MultiwayFullBlueprintArtifacts::load_verified(
		const std::filesystem::path& path,
		const MultiwayModelIdentity& expected_identity)
	{
		expected_identity.validate();
		std::error_code file_error;
		const auto file_bytes = std::filesystem::file_size(path, file_error);
		if (file_error)
			throw std::runtime_error("multiway full blueprint size cannot be determined");
		if (file_bytes > kFullBlueprintOperatingBytes)
		{
			throw std::length_error("multiway full blueprint exceeds the operating memory guardrail");
		}
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("multiway full blueprint cannot be opened");
		std::array<char, kFullBlueprintMagic.size()> magic{};
		in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
		if (!in || magic != kFullBlueprintMagic)
			throw std::runtime_error("multiway full blueprint schema is invalid");
		MultiwayFullBlueprintArtifact artifact;
		if (!portable::read_u32(in, artifact.schema_version))
			throw std::runtime_error("multiway full blueprint is truncated");
		artifact.identity = read_identity(in);
		artifact.training = read_training(in);
		std::uint64_t row_count = 0;
		if (!portable::read_u64(in, row_count))
			throw std::runtime_error("multiway full blueprint is truncated");
		if (row_count > 100000000U)
			throw std::runtime_error("multiway full blueprint row count is invalid");
		const auto row_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayBlueprintRow));
		const auto action_storage_bytes = static_cast<std::uint64_t>(sizeof(MultiwayQuantizedRootAction));
		constexpr std::uint64_t max_actions_per_row = 64U;
		const auto action_payload_bytes = action_storage_bytes > std::numeric_limits<std::uint64_t>::max() / max_actions_per_row
			? std::numeric_limits<std::uint64_t>::max()
			: action_storage_bytes * max_actions_per_row;
		const auto estimated_row_bytes = row_storage_bytes > std::numeric_limits<std::uint64_t>::max() - action_payload_bytes
			? std::numeric_limits<std::uint64_t>::max()
			: row_storage_bytes + action_payload_bytes;
		if (estimated_row_bytes == 0U || row_count > (kFullBlueprintOperatingBytes - file_bytes) / estimated_row_bytes)
		{
			throw std::length_error("multiway full blueprint rows exceed the operating memory guardrail");
		}
		artifact.rows.resize(static_cast<std::size_t>(row_count));
		for (auto& row : artifact.rows)
		{
			row.infoset = read_infoset(in);
			if (!portable::read_u32(in, row.bucket) || !portable::read_u64(in, row.action_menu_id))
			{
				throw std::runtime_error("multiway full blueprint is truncated");
			}
			std::uint32_t action_count = 0;
			if (!portable::read_u32(in, action_count))
				throw std::runtime_error("multiway full blueprint is truncated");
			if (action_count == 0U || action_count > 64U)
				throw std::runtime_error("multiway full blueprint action count is invalid");
			row.actions.resize(action_count);
			for (auto& action : row.actions)
				action = read_action(in);
		}
		if (!portable::read_u64(in, artifact.payload_hash))
			throw std::runtime_error("multiway full blueprint is truncated");
		if (in.peek() != std::char_traits<char>::eof())
			throw std::runtime_error("multiway full blueprint has trailing data");
		artifact.validate();
		if (artifact.identity != expected_identity)
		{
			throw std::invalid_argument("multiway full blueprint identity does not match");
		}
		return artifact;
	}

	void MultiwayBlueprintManifest::validate() const
	{
		if (schema_version != MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION || snapshot_hash == 0U)
		{
			throw std::invalid_argument("multiway blueprint manifest has invalid schema or hash");
		}
		identity.validate();
	}

	void MultiwayVerifiedBlueprintArtifact::validate(const MultiwayModelIdentity& expected_identity) const
	{
		snapshot.validate();
		manifest.validate();
		expected_identity.validate();
		if (snapshot.identity != manifest.identity || snapshot.identity != expected_identity || manifest.snapshot_hash != MultiwayBlueprintArtifacts::snapshot_hash(snapshot))
		{
			throw std::invalid_argument("multiway blueprint artifact identity or integrity mismatch");
		}
	}

	std::uint64_t MultiwayBlueprintArtifacts::snapshot_hash(const MultiwayBlueprintSnapshot& snapshot) noexcept
	{
		auto hash = texas::core::fingerprint::FNV1A_OFFSET;
		append_identity(hash, snapshot.identity);
		append_u64(hash, snapshot.public_state.value);
		append_u64(hash, snapshot.infoset.public_state.value);
		append_u64(hash, static_cast<std::uint64_t>(snapshot.infoset.seat));
		append_u64(hash, snapshot.bucket);
		append_u64(hash, snapshot.trajectories);
		append_u64(hash, static_cast<std::uint64_t>(snapshot.policy_kind));
		append_u64(hash, snapshot.training.batches);
		append_u64(hash, snapshot.training.trajectories);
		append_u64(hash, snapshot.training.deterministic_seed);
		append_u64(hash, snapshot.training.late_window_start_batch);
		append_u64(hash, snapshot.training.schedule_hash);
		append_u64(hash, snapshot.training.pruned_negative_regrets);
		append_u64(hash, snapshot.training.linear_iteration_weighting);
		append_u64(hash, snapshot.training.discounting_enabled);
		append_u64(hash, snapshot.training.negative_regret_pruning_enabled);
		append_u64(hash, snapshot.actions.size());
		for (const auto& action : snapshot.actions)
		{
			append_action(hash, action.action);
			append_u64(hash, action.probability);
		}
		return finish(hash);
	}

	void MultiwayBlueprintArtifacts::save_atomic(
		const std::filesystem::path& checkpoint_path,
		const MultiwayBlueprintSnapshot& snapshot)
	{
		snapshot.validate();
		MultiwayBlueprintManifest manifest;
		manifest.identity = snapshot.identity;
		manifest.snapshot_hash = snapshot_hash(snapshot);
		manifest.validate();
		MultiwayRootPolicyArtifact::save_atomic(checkpoint_path, snapshot);
		save_manifest_atomic(manifest_path(checkpoint_path), manifest);
	}

	MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_verified(
		const std::filesystem::path& checkpoint_path,
		const MultiwayModelIdentity& expected_identity)
	{
		MultiwayVerifiedBlueprintArtifact artifact;
		artifact.snapshot = MultiwayRootPolicyArtifact::load(checkpoint_path);
		artifact.manifest = load_manifest(manifest_path(checkpoint_path));
		artifact.validate(expected_identity);
		return artifact;
	}

	MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_with_fallback(
		const std::filesystem::path& primary_checkpoint_path,
		const std::filesystem::path& known_good_checkpoint_path,
		const MultiwayModelIdentity& expected_identity)
	{
		try
		{
			return load_verified(primary_checkpoint_path, expected_identity);
		}
		catch (const std::exception&)
		{
			auto fallback = load_verified(known_good_checkpoint_path, expected_identity);
			fallback.source = MultiwayArtifactSource::KnownGoodFallback;
			return fallback;
		}
	}

	void MultiwayPublicDecisionLog::validate() const
	{
		identity.validate();
		if (schema_version != MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION || public_state_id == 0U || decision_index == 0U || acting_seat < 0 || policy.empty() || sampled_action.action_menu_id == 0U || policy_provenance == MultiwayPolicyProvenance::None || search_engine_version == 0U || (resolver_status != MultiwayResolverStatus::Solved && resolver_status != MultiwayResolverStatus::Partial && resolver_status != MultiwayResolverStatus::DeadlineFallback && resolver_status != MultiwayResolverStatus::ArtifactMismatch && resolver_status != MultiwayResolverStatus::BucketUnavailable && resolver_status != MultiwayResolverStatus::ResourceExhausted && resolver_status != MultiwayResolverStatus::RejectedByBudget))
		{
			throw std::invalid_argument("multiway public decision log has invalid metadata");
		}
		const bool runtime_search = policy_provenance == MultiwayPolicyProvenance::RuntimeSearch;
		const bool fallback = policy_provenance == MultiwayPolicyProvenance::StableRootFallback || policy_provenance == MultiwayPolicyProvenance::BlueprintFallback || policy_provenance == MultiwayPolicyProvenance::StaticLegalFallback;
		if ((runtime_search && (search_engine != MultiwayResolverEngine::RootExternalSamplingMCCFR || search_engine_version != MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION)) || (fallback && (search_engine != MultiwayResolverEngine::NoRuntimeSearch || search_engine_version != MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION)))
		{
			throw std::invalid_argument("multiway public decision log has inconsistent provenance");
		}
		std::uint64_t total = 0;
		bool found_sample = false;
		for (std::size_t index = 0; index < policy.size(); ++index)
		{
			if (policy[index].action.action_index != index || policy[index].action.action_menu_id == 0U)
			{
				throw std::invalid_argument("multiway public decision log has invalid action order");
			}
			total += policy[index].probability;
			found_sample = found_sample || policy[index].action == sampled_action;
		}
		if (total != std::numeric_limits<std::uint16_t>::max() || !found_sample)
		{
			throw std::invalid_argument("multiway public decision log has invalid policy");
		}
	}

	MultiwayPublicDecisionLog make_multiway_public_decision_log(
		const MultiwayResolverRequest& request,
		const MultiwayResolverResult& result,
		std::uint64_t decision_index)
	{
		request.blueprint_identity.validate();
		if (decision_index == 0U || !result.has_sampled_action || !result.diagnostics.policy_normalized || result.policy.empty())
		{
			throw std::invalid_argument("multiway decision log requires a normalized resolver result");
		}
		MultiwayPublicDecisionLog log;
		log.identity = request.blueprint_identity;
		log.public_state_id = result.diagnostics.resolved_public_state_id;
		log.decision_index = decision_index;
		log.acting_seat = request.hero_seat;
		log.sampled_action = result.sampled_action;
		log.resolver_status = result.diagnostics.status;
		log.policy_provenance = result.diagnostics.policy_provenance;
		log.search_engine = result.diagnostics.search_engine;
		log.search_engine_version = result.diagnostics.search_engine_version;
		log.used_fallback = result.diagnostics.used_fallback;
		log.policy.reserve(result.policy.size());
		std::uint32_t assigned = 0;
		for (std::size_t index = 0; index < result.policy.size(); ++index)
		{
			auto probability = index + 1U == result.policy.size()
				? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
				: quantize_probability(result.policy[index].probability);
			assigned += probability;
			log.policy.push_back({ result.policy[index].action, probability });
		}
		log.validate();
		return log;
	}

	MultiwayProtectedReplayRecord MultiwayProtectedReplayRecord::from_history(
		const MultiwayModelIdentity& record_identity,
		const MultiwayHandHistory& history)
	{
		record_identity.validate();
		history.validate();
		MultiwayProtectedReplayRecord record;
		record.identity = record_identity;
		record.public_history = history;
		record.decision_seeds.reserve(history.events.size());
		for (const auto& event : history.events)
		{
			if (event.kind == MultiwayReplayEventKind::Decision)
			{
				record.decision_seeds.push_back(event.decision.decision_seed);
			}
		}
		record.seal();
		record.validate();
		return record;
	}

	void MultiwayProtectedReplayRecord::seal() noexcept
	{
		integrity_hash = replay_hash(*this);
	}

	void MultiwayProtectedReplayRecord::validate() const
	{
		if (schema_version != MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION || integrity_hash == 0U)
		{
			throw std::invalid_argument("multiway protected replay has invalid schema or integrity hash");
		}
		identity.validate();
		public_history.validate();
		std::size_t decision_index = 0;
		for (const auto& event : public_history.events)
		{
			if (event.kind == MultiwayReplayEventKind::Decision)
			{
				if (decision_index >= decision_seeds.size() || decision_seeds[decision_index] != event.decision.decision_seed)
				{
					throw std::invalid_argument("multiway protected replay seed record is inconsistent");
				}
				++decision_index;
			}
		}
		if (decision_index != decision_seeds.size() || integrity_hash != replay_hash(*this))
		{
			throw std::invalid_argument("multiway protected replay integrity check failed");
		}
	}

} // namespace texas::solver::multiway
