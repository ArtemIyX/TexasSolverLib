#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
		constexpr std::uint64_t kFullBlueprintOperatingBytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr std::size_t kMaximumActionsPerRow = 64U;
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
