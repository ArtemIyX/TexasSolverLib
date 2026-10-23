#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <streambuf>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		namespace portable = texas::core::portable;

		constexpr std::array<char, 8> kMagic = { 'M', 'W', 'T', 'C', '0', '0', '0', '1' };
		constexpr std::uint32_t kSchema = 5U;
		constexpr std::uint64_t kMaxFileBytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr std::uint32_t kMaxStates = 100000000U;
		constexpr std::uint32_t kMaxRows = 100000000U;

		void require(bool ok)
		{
			if (!ok)
				throw std::runtime_error("multiway training checkpoint is truncated");
		}

		constexpr std::size_t kCheckpointWriteBufferBytes = 64U * 1024U;
		constexpr std::size_t kDoubleWriteBlockValues = kCheckpointWriteBufferBytes / sizeof(std::uint64_t);

		void append(std::uint64_t& hash, std::uint64_t value) noexcept;

		class BufferedCheckpointStreambuf final : public std::streambuf
		{
		public:
			explicit BufferedCheckpointStreambuf(std::ofstream& file) noexcept
				: file_(file)
			{
				setp(buffer_.data(), buffer_.data() + buffer_.size());
			}

			bool flush_buffer() noexcept
			{
				const auto count = static_cast<std::streamsize>(pptr() - pbase());
				if (count != 0)
				{
					file_.write(pbase(), count);
					if (!file_)
						return false;
				}
				setp(buffer_.data(), buffer_.data() + buffer_.size());
				return true;
			}

		protected:
			int_type overflow(int_type character) override
			{
				if (!flush_buffer())
					return traits_type::eof();
				if (!traits_type::eq_int_type(character, traits_type::eof()))
				{
					*pptr() = traits_type::to_char_type(character);
					pbump(1);
				}
				return traits_type::not_eof(character);
			}

			std::streamsize xsputn(const char* source, std::streamsize count) override
			{
				std::streamsize written = 0;
				while (written < count)
				{
					const auto available = static_cast<std::streamsize>(epptr() - pptr());
					if (available == 0)
					{
						if (overflow(traits_type::eof()) == traits_type::eof())
							break;
						continue;
					}
					const auto chunk = std::min(available, count - written);
					std::memcpy(pptr(), source + written, static_cast<std::size_t>(chunk));
					pbump(static_cast<int>(chunk));
					written += chunk;
				}
				return written;
			}

			int sync() override
			{
				return flush_buffer() ? 0 : -1;
			}

		private:
			std::ofstream& file_;
			std::array<char, kCheckpointWriteBufferBytes> buffer_{};
		};

		class CheckpointPayloadWriter final
		{
		public:
			explicit CheckpointPayloadWriter(std::ostream& out) noexcept
				: out_(out)
			{
			}

			void write_u8(std::uint8_t value)
			{
				const auto byte = static_cast<char>(value);
				write_raw(&byte, sizeof(byte));
				append(hash_, value);
			}

			void write_u8_unhashed(std::uint8_t value)
			{
				const auto byte = static_cast<char>(value);
				write_raw(&byte, sizeof(byte));
			}

			void write_u32(std::uint32_t value)
			{
				std::array<char, sizeof(value)> bytes{};
				encode_u32(bytes.data(), value);
				write_raw(bytes.data(), bytes.size());
				append(hash_, value);
			}

			void write_i32(std::int32_t value)
			{
				std::array<char, sizeof(value)> bytes{};
				encode_u32(bytes.data(), static_cast<std::uint32_t>(value));
				write_raw(bytes.data(), bytes.size());
				append(hash_, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
			}

			void write_u64(std::uint64_t value)
			{
				write_u64_unhashed(value);
				hash_u64(value);
			}

			void write_u64_unhashed(std::uint64_t value)
			{
				std::array<char, sizeof(value)> bytes{};
				encode_u64(bytes.data(), value);
				write_raw(bytes.data(), bytes.size());
			}

			void hash_u64(std::uint64_t value) noexcept
			{
				append(hash_, value);
			}

			void write_double(double value)
			{
				std::uint64_t bits = 0U;
				std::memcpy(&bits, &value, sizeof(bits));
				write_u64_bytes(bits);
				append(hash_, bits);
			}

			void write_double_array(const std::vector<double>& values)
			{
				write_u64(values.size());
				std::array<char, kCheckpointWriteBufferBytes> bytes{};
				for (std::size_t offset = 0U; offset < values.size(); offset += kDoubleWriteBlockValues)
				{
					const auto count = std::min(kDoubleWriteBlockValues, values.size() - offset);
					for (std::size_t index = 0U; index < count; ++index)
					{
						std::uint64_t bits = 0U;
						std::memcpy(&bits, &values[offset + index], sizeof(bits));
						encode_u64(bytes.data() + index * sizeof(bits), bits);
						append(hash_, bits);
					}
					write_raw(bytes.data(), count * sizeof(std::uint64_t));
					if (!out_)
						throw std::runtime_error("multiway training checkpoint buffered write failed");
				}
			}

			void write_i32_array(const std::vector<std::int32_t>& values)
			{
				write_u64(values.size());
				for (const auto value : values)
					write_i32(value);
			}

			void write_u64_array(const std::vector<std::uint64_t>& values)
			{
				write_u64(values.size());
				for (const auto value : values)
					write_u64(value);
			}

			void write_raw(const void* data, std::size_t size)
			{
				out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
				bytes_written_ += size;
			}

			[[nodiscard]] std::uint64_t finish_hash() const noexcept
			{
				return core::fingerprint::finish(hash_);
			}

			[[nodiscard]] std::uint64_t bytes_written() const noexcept
			{
				return bytes_written_;
			}

		private:
			static void encode_u32(char* destination, std::uint32_t value) noexcept
			{
				for (std::uint8_t shift = 0U; shift < 32U; shift += 8U)
					destination[shift / 8U] = static_cast<char>(value >> shift);
			}

			static void encode_u64(char* destination, std::uint64_t value) noexcept
			{
				for (std::uint8_t shift = 0U; shift < 64U; shift += 8U)
					destination[shift / 8U] = static_cast<char>(value >> shift);
			}

			void write_u64_bytes(std::uint64_t value)
			{
				std::array<char, sizeof(value)> bytes{};
				encode_u64(bytes.data(), value);
				write_raw(bytes.data(), bytes.size());
			}

			std::ostream& out_;
			std::uint64_t hash_ = core::fingerprint::FNV1A_OFFSET;
			std::uint64_t bytes_written_ = 0U;
		};

		void add_elapsed(
			std::uint64_t* target,
			const std::chrono::steady_clock::time_point started) noexcept
		{
			if (target != nullptr)
				*target += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - started)
						.count());
		}
		void record_max(std::uint64_t* target, std::uint64_t value) noexcept
		{
			if (target != nullptr && value > *target)
				*target = value;
		}
		bool read_double(std::istream& in, double& value)
		{
			std::uint64_t bits = 0U;
			if (!portable::read_u64(in, bits))
				return false;
			std::memcpy(&value, &bits, sizeof(value));
			return true;
		}
		void write_identity(CheckpointPayloadWriter& out, const MultiwayModelIdentity& value)
		{
			visit_multiway_model_identity_fields(value, [&](std::uint64_t field) { out.write_u64(field); });
		}
		bool read_identity(std::istream& in, MultiwayModelIdentity& value)
		{
			bool ok = true;
			visit_multiway_model_identity_fields(value, [&](std::uint64_t& field) { if (ok) ok = portable::read_u64(in, field); });
			return ok;
		}
		void write_action(CheckpointPayloadWriter& out, const MultiwayActionDescriptor& value)
		{
			out.write_u8(static_cast<std::uint8_t>(value.action));
			out.write_u32(value.action_index);
			out.write_i32(value.target_street_contribution);
			out.write_u64(value.action_menu_id);
		}
		bool read_action(std::istream& in, MultiwayActionDescriptor& value)
		{
			std::uint8_t action = 0U;
			if (!portable::read_u8(in, action) || !portable::read_u32(in, value.action_index) || !portable::read_i32(in, value.target_street_contribution) || !portable::read_u64(in, value.action_menu_id))
				return false;
			value.action = static_cast<MultiwayAction>(action);
			return true;
		}
		void write_ints(CheckpointPayloadWriter& out, const std::vector<int>& values)
		{
			out.write_u8(static_cast<std::uint8_t>(values.size()));
			for (const auto value : values)
				out.write_i32(value);
		}
		bool read_ints(std::istream& in, std::vector<int>& values)
		{
			std::uint8_t size = 0U;
			if (!portable::read_u8(in, size) || size > 6U)
				return false;
			values.resize(size);
			for (auto& value : values)
				if (!portable::read_i32(in, value))
					return false;
			return true;
		}
		void write_bools(CheckpointPayloadWriter& out, const std::vector<bool>& values)
		{
			out.write_u8(static_cast<std::uint8_t>(values.size()));
			for (const auto value : values)
				out.write_u8(value ? 1U : 0U);
		}
		bool read_bools(std::istream& in, std::vector<bool>& values)
		{
			std::uint8_t size = 0U;
			if (!portable::read_u8(in, size) || size > 6U)
				return false;
			values.assign(size, false);
			for (std::size_t index = 0U; index < size; ++index)
			{
				std::uint8_t value = 0U;
				if (!portable::read_u8(in, value) || value > 1U)
					return false;
				values[index] = value != 0U;
			}
			return true;
		}
		void write_cards(CheckpointPayloadWriter& out, const std::vector<std::uint8_t>& values)
		{
			out.write_u8(static_cast<std::uint8_t>(values.size()));
			for (const auto value : values)
				out.write_u8(value);
		}
		bool read_cards(std::istream& in, std::vector<std::uint8_t>& values, std::uint8_t maximum = 5U)
		{
			std::uint8_t size = 0U;
			if (!portable::read_u8(in, size) || size > maximum)
				return false;
			values.resize(size);
			for (auto& value : values)
				if (!portable::read_u8(in, value))
					return false;
			return true;
		}
		void write_betting(CheckpointPayloadWriter& out, const MultiwayBettingSnapshot& value)
		{
			write_ints(out, value.stacks);
			write_ints(out, value.contributions);
			write_ints(out, value.street_contributions);
			write_bools(out, value.folded);
			write_bools(out, value.all_in);
			write_bools(out, value.may_raise);
			write_bools(out, value.pending);
			write_bools(out, value.has_acted);
			write_ints(out, value.bet_faced_when_acted);
			out.write_i32(value.current_player);
			out.write_i32(value.last_aggressor);
			out.write_i32(value.current_bet);
			out.write_i32(value.last_full_raise_size);
			out.write_i32(value.big_blind);
			out.write_u8(static_cast<std::uint8_t>(value.street));
		}
		bool read_betting(std::istream& in, MultiwayBettingSnapshot& value)
		{
			std::uint8_t street = 0U;
			return read_ints(in, value.stacks) && read_ints(in, value.contributions) && read_ints(in, value.street_contributions) && read_bools(in, value.folded) && read_bools(in, value.all_in) && read_bools(in, value.may_raise) && read_bools(in, value.pending) && read_bools(in, value.has_acted) && read_ints(in, value.bet_faced_when_acted) && portable::read_i32(in, value.current_player) && portable::read_i32(in, value.last_aggressor) && portable::read_i32(in, value.current_bet) && portable::read_i32(in, value.last_full_raise_size) && portable::read_i32(in, value.big_blind) && portable::read_u8(in, street) && ((value.street = static_cast<Street>(street)), true);
		}
		void write_state(CheckpointPayloadWriter& out, const MultiwayPublicStateDescriptor& value)
		{
			out.write_u64(value.id.value);
			out.write_u64(value.parent_id.value);
			out.write_u8(static_cast<std::uint8_t>(value.incoming_edge.kind));
			write_action(out, value.incoming_edge.action);
			out.write_u8(value.incoming_edge.dealt_card);
			write_cards(out, value.incoming_edge.dealt_cards);
			write_cards(out, value.incoming_edge.transition_board);
			out.write_u64(value.canonical_history_id);
			write_betting(out, value.betting);
			write_cards(out, value.board);
			out.write_u8(value.board_runout.remaining_board_cards);
			out.write_u8(value.board_runout.chance_only_runout ? 1U : 0U);
			out.write_u32(static_cast<std::uint32_t>(value.history.size()));
			for (const auto& entry : value.history)
			{
				out.write_i32(entry.actor);
				write_action(out, entry.action);
			}
			out.write_u8(static_cast<std::uint8_t>(value.legal_actions.size()));
			for (const auto& action : value.legal_actions)
				write_action(out, action);
		}
		bool read_state(std::istream& in, MultiwayPublicStateDescriptor& value)
		{
			std::uint8_t kind = 0U, chance_only = 0U, actions = 0U;
			std::uint32_t history = 0U;
			if (!portable::read_u64(in, value.id.value) || !portable::read_u64(in, value.parent_id.value) || !portable::read_u8(in, kind) || !read_action(in, value.incoming_edge.action) || !portable::read_u8(in, value.incoming_edge.dealt_card) || !read_cards(in, value.incoming_edge.dealt_cards) || !read_cards(in, value.incoming_edge.transition_board) || !portable::read_u64(in, value.canonical_history_id) || !read_betting(in, value.betting) || !read_cards(in, value.board) || !portable::read_u8(in, value.board_runout.remaining_board_cards) || !portable::read_u8(in, chance_only) || chance_only > 1U || !portable::read_u32(in, history) || history > 512U)
				return false;
			value.incoming_edge.kind = static_cast<MultiwayPublicParentEdgeKind>(kind);
			value.board_runout.chance_only_runout = chance_only != 0U;
			value.history.resize(history);
			for (auto& entry : value.history)
				if (!portable::read_i32(in, entry.actor) || !read_action(in, entry.action))
					return false;
			if (!portable::read_u8(in, actions) || actions > 6U)
				return false;
			value.legal_actions.resize(actions);
			for (auto& action : value.legal_actions)
				if (!read_action(in, action))
					return false;
			return true;
		}
		void write_metadata(CheckpointPayloadWriter& out, const MultiwayBlueprintTrainingMetadata& value)
		{
			out.write_u64(value.batches);
			out.write_u64(value.trajectories);
			out.write_u64(value.deterministic_seed);
			out.write_u64(value.late_window_start_batch);
			out.write_u64(value.schedule_hash);
			out.write_u64(value.pruned_negative_regrets);
			out.write_u64(value.terminal_visits);
			out.write_u64(value.leaf_visits);
			out.write_u64(value.missing_lookup_requests);
			out.write_u8(value.linear_iteration_weighting);
			out.write_u8(value.discounting_enabled);
			out.write_u8(value.negative_regret_pruning_enabled);
			out.write_u8_unhashed(value.reserved);
		}
		bool read_metadata(std::istream& in, MultiwayBlueprintTrainingMetadata& value)
		{
			return portable::read_u64(in, value.batches) && portable::read_u64(in, value.trajectories) && portable::read_u64(in, value.deterministic_seed) && portable::read_u64(in, value.late_window_start_batch) && portable::read_u64(in, value.schedule_hash) && portable::read_u64(in, value.pruned_negative_regrets) && portable::read_u64(in, value.terminal_visits) && portable::read_u64(in, value.leaf_visits) && portable::read_u64(in, value.missing_lookup_requests) && portable::read_u8(in, value.linear_iteration_weighting) && portable::read_u8(in, value.discounting_enabled) && portable::read_u8(in, value.negative_regret_pruning_enabled) && portable::read_u8(in, value.reserved);
		}
		void append(std::uint64_t& hash, std::uint64_t value) noexcept
		{
			core::fingerprint::append_u64(hash, value);
		}
		void append_double(std::uint64_t& hash, double value) noexcept
		{
			std::uint64_t bits = 0U;
			std::memcpy(&bits, &value, sizeof(bits));
			append(hash, bits);
		}
		void hash_ints(std::uint64_t& hash, const std::vector<int>& values) noexcept
		{
			append(hash, values.size());
			for (const auto value : values)
				append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
		}
		void hash_bools(std::uint64_t& hash, const std::vector<bool>& values) noexcept
		{
			append(hash, values.size());
			for (const auto value : values)
				append(hash, value ? 1U : 0U);
		}
		void hash_cards(std::uint64_t& hash, const std::vector<std::uint8_t>& values) noexcept
		{
			append(hash, values.size());
			for (const auto value : values)
				append(hash, value);
		}
		void hash_action(std::uint64_t& hash, const MultiwayActionDescriptor& value) noexcept
		{
			append(hash, static_cast<std::uint8_t>(value.action));
			append(hash, value.action_index);
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value.target_street_contribution)));
			append(hash, value.action_menu_id);
		}
		void hash_state(std::uint64_t& hash, const MultiwayPublicStateDescriptor& state) noexcept
		{
			append(hash, state.id.value);
			append(hash, state.parent_id.value);
			append(hash, static_cast<std::uint8_t>(state.incoming_edge.kind));
			hash_action(hash, state.incoming_edge.action);
			append(hash, state.incoming_edge.dealt_card);
			hash_cards(hash, state.incoming_edge.dealt_cards);
			hash_cards(hash, state.incoming_edge.transition_board);
			append(hash, state.canonical_history_id);
			const auto& betting = state.betting;
			hash_ints(hash, betting.stacks);
			hash_ints(hash, betting.contributions);
			hash_ints(hash, betting.street_contributions);
			hash_bools(hash, betting.folded);
			hash_bools(hash, betting.all_in);
			hash_bools(hash, betting.may_raise);
			hash_bools(hash, betting.pending);
			hash_bools(hash, betting.has_acted);
			hash_ints(hash, betting.bet_faced_when_acted);
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(betting.current_player)));
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(betting.last_aggressor)));
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(betting.current_bet)));
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(betting.last_full_raise_size)));
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(betting.big_blind)));
			append(hash, static_cast<std::uint8_t>(betting.street));
			hash_cards(hash, state.board);
			append(hash, state.board_runout.remaining_board_cards);
			append(hash, state.board_runout.chance_only_runout ? 1U : 0U);
			append(hash, state.history.size());
			for (const auto& entry : state.history)
			{
				append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(entry.actor)));
				hash_action(hash, entry.action);
			}
			append(hash, state.legal_actions.size());
			for (const auto& action : state.legal_actions)
				hash_action(hash, action);
		}
	} // namespace

	std::uint64_t MultiwayTrainingCheckpointArtifacts::payload_hash(const MultiwayBlueprintTrainingCheckpoint& checkpoint) noexcept
	{
		auto hash = core::fingerprint::FNV1A_OFFSET;
		visit_multiway_model_identity_fields(checkpoint.identity, [&](std::uint64_t value) { append(hash, value); });
		const auto& training = checkpoint.training;
		append(hash, training.batches);
		append(hash, training.trajectories);
		append(hash, training.deterministic_seed);
		append(hash, training.late_window_start_batch);
		append(hash, training.schedule_hash);
		append(hash, training.pruned_negative_regrets);
		append(hash, training.terminal_visits);
		append(hash, training.leaf_visits);
		append(hash, training.missing_lookup_requests);
		append(hash, training.linear_iteration_weighting);
		append(hash, training.discounting_enabled);
		append(hash, training.negative_regret_pruning_enabled);
		const auto& coverage = checkpoint.coverage;
		append(hash, coverage.visited_public_descriptors);
		append(hash, coverage.admitted_rows);
		append(hash, coverage.admitted_action_cells);
		append(hash, coverage.terminal_visits);
		append(hash, coverage.leaf_visits);
		append(hash, coverage.missing_lookup_requests);
		append(hash, coverage.preflop_rows);
		append(hash, coverage.flop_rows);
		append(hash, coverage.turn_rows);
		append(hash, coverage.river_rows);
		append(hash, checkpoint.coordinator.terminal_visits);
		append(hash, checkpoint.coordinator.leaf_visits);
		append(hash, checkpoint.coordinator.missing_lookup_requests);
		append(hash, checkpoint.coordinator.last_merged_stream_fingerprint);
		for (const auto value : coverage.street_visits)
			append(hash, value);
		for (const auto value : checkpoint.coordinator.street_visits)
			append(hash, value);
		append(hash, checkpoint.coordinator.public_states.size());
		for (const auto& state : checkpoint.coordinator.public_states)
			hash_state(hash, state);
		append(hash, checkpoint.coordinator.storage.shapes.size());
		for (const auto& shape : checkpoint.coordinator.storage.shapes)
		{
			append(hash, shape.infoset.public_state.value);
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(shape.infoset.seat)));
			append(hash, shape.bucket_count);
			append(hash, shape.action_count);
		}
		append(hash, checkpoint.coordinator.storage.active_pages.size());
		for (const auto& page : checkpoint.coordinator.storage.active_pages)
		{
			append(hash, page.infoset.public_state.value);
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(page.infoset.seat)));
			append(hash, page.page_index);
		}
		append(hash, checkpoint.coordinator.storage.compact_regrets.size());
		for (const auto value : checkpoint.coordinator.storage.compact_regrets)
			append(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
		append(hash, checkpoint.coordinator.storage.compact_strategy_masses.size());
		for (const auto value : checkpoint.coordinator.storage.compact_strategy_masses)
			append(hash, value);
		append(hash, checkpoint.coordinator.storage.regrets.size());
		for (const auto value : checkpoint.coordinator.storage.regrets)
			append_double(hash, value);
		append(hash, checkpoint.coordinator.storage.strategy_sums.size());
		for (const auto value : checkpoint.coordinator.storage.strategy_sums)
			append_double(hash, value);
		append(hash, checkpoint.late_window_baseline.size());
		for (const auto value : checkpoint.late_window_baseline)
			append_double(hash, value);
		return core::fingerprint::finish(hash);
	}

	void MultiwayTrainingCheckpointArtifacts::save_atomic(
		const std::filesystem::path& path,
		const MultiwayBlueprintTrainingCheckpoint& checkpoint,
		MultiwayCheckpointTelemetry* telemetry)
	{
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.artifact_validation");
			const auto started = std::chrono::steady_clock::now();
			checkpoint.validate();
			add_elapsed(telemetry == nullptr ? nullptr : &telemetry->validation_nanoseconds, started);
		}
		save_atomic_sealed(path, checkpoint, telemetry);
	}

	void MultiwayTrainingCheckpointArtifacts::save_atomic_sealed(
		const std::filesystem::path& path,
		const MultiwayBlueprintTrainingCheckpoint& checkpoint,
		MultiwayCheckpointTelemetry* telemetry)
	{
		const auto temporary = path.string() + ".tmp";
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
			throw std::runtime_error("multiway training checkpoint temporary file cannot be opened");
		BufferedCheckpointStreambuf buffer(file);
		std::ostream out(&buffer);
		CheckpointPayloadWriter payload(out);
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.binary_encoding_write");
			const auto encoding_started = std::chrono::steady_clock::now();
			out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
			portable::write_u32(out, kSchema);
			write_identity(payload, checkpoint.identity);
			write_metadata(payload, checkpoint.training);
			const auto& coverage = checkpoint.coverage;
			payload.write_u64(coverage.visited_public_descriptors);
			payload.write_u64(coverage.admitted_rows);
			payload.write_u64(coverage.admitted_action_cells);
			payload.write_u64(coverage.terminal_visits);
			payload.write_u64(coverage.leaf_visits);
			payload.write_u64(coverage.missing_lookup_requests);
			payload.write_u64(coverage.preflop_rows);
			payload.write_u64(coverage.flop_rows);
			payload.write_u64(coverage.turn_rows);
			payload.write_u64(coverage.river_rows);
			for (const auto value : coverage.street_visits)
				payload.write_u64_unhashed(value);
			payload.write_u64(checkpoint.coordinator.terminal_visits);
			payload.write_u64(checkpoint.coordinator.leaf_visits);
			payload.write_u64(checkpoint.coordinator.missing_lookup_requests);
			payload.write_u64(checkpoint.coordinator.last_merged_stream_fingerprint);
			for (const auto value : checkpoint.coordinator.street_visits)
				payload.write_u64_unhashed(value);
			for (const auto value : coverage.street_visits)
				payload.hash_u64(value);
			for (const auto value : checkpoint.coordinator.street_visits)
				payload.hash_u64(value);
			const auto public_state_start = payload.bytes_written();
			payload.write_u32(static_cast<std::uint32_t>(checkpoint.coordinator.public_states.size()));
			for (const auto& state : checkpoint.coordinator.public_states)
				write_state(payload, state);
			if (telemetry != nullptr)
				record_max(&telemetry->public_state_bytes, payload.bytes_written() - public_state_start);
			const auto row_shape_start = payload.bytes_written();
			payload.write_u32(static_cast<std::uint32_t>(checkpoint.coordinator.storage.shapes.size()));
			for (const auto& shape : checkpoint.coordinator.storage.shapes)
			{
				payload.write_u64(shape.infoset.public_state.value);
				payload.write_i32(shape.infoset.seat);
				payload.write_u32(shape.bucket_count);
				payload.write_u8(shape.action_count);
			}
			payload.write_u32(static_cast<std::uint32_t>(checkpoint.coordinator.storage.active_pages.size()));
			for (const auto& page : checkpoint.coordinator.storage.active_pages)
			{
				payload.write_u64(page.infoset.public_state.value);
				payload.write_i32(page.infoset.seat);
				payload.write_u32(page.page_index);
			}
			payload.write_i32_array(checkpoint.coordinator.storage.compact_regrets);
			payload.write_u64_array(checkpoint.coordinator.storage.compact_strategy_masses);
			if (telemetry != nullptr)
				record_max(&telemetry->row_shape_bytes, payload.bytes_written() - row_shape_start);
			const auto regret_start = payload.bytes_written();
			payload.write_double_array(checkpoint.coordinator.storage.regrets);
			if (telemetry != nullptr)
				record_max(&telemetry->regret_bytes, payload.bytes_written() - regret_start);
			const auto strategy_start = payload.bytes_written();
			payload.write_double_array(checkpoint.coordinator.storage.strategy_sums);
			if (telemetry != nullptr)
				record_max(&telemetry->strategy_sum_bytes, payload.bytes_written() - strategy_start);
			const auto baseline_start = payload.bytes_written();
			payload.write_double_array(checkpoint.late_window_baseline);
			if (telemetry != nullptr)
				record_max(&telemetry->late_window_baseline_bytes, payload.bytes_written() - baseline_start);
			add_elapsed(telemetry == nullptr ? nullptr : &telemetry->binary_encoding_write_nanoseconds, encoding_started);
		}
		std::uint64_t serialized_payload_hash = 0U;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.payload_hash");
			const auto hash_started = std::chrono::steady_clock::now();
			serialized_payload_hash = payload.finish_hash();
			add_elapsed(telemetry == nullptr ? nullptr : &telemetry->payload_hash_nanoseconds, hash_started);
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.binary_encoding_write");
			const auto encoding_started = std::chrono::steady_clock::now();
			portable::write_u64(out, serialized_payload_hash);
			add_elapsed(telemetry == nullptr ? nullptr : &telemetry->binary_encoding_write_nanoseconds, encoding_started);
		}
		const auto close_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.stream_close_or_flush");
			out.flush();
			file.close();
		}
		add_elapsed(telemetry == nullptr ? nullptr : &telemetry->stream_close_or_flush_nanoseconds, close_started);
		if (!out || !file)
			throw std::runtime_error("multiway training checkpoint write failed");
		const auto publication_started = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.atomic_publication");
			const auto mode = core::publish_atomic_replace(temporary, path, "multiway training checkpoint publish failed");
			if (telemetry != nullptr)
				telemetry->atomic_publication_mode = mode;
		}
		add_elapsed(telemetry == nullptr ? nullptr : &telemetry->atomic_publication_nanoseconds, publication_started);
	}

	MultiwayBlueprintTrainingCheckpoint MultiwayTrainingCheckpointArtifacts::load_verified(const std::filesystem::path& path, const MultiwayModelIdentity& expected_identity, std::uint64_t expected_schedule_hash, std::uint64_t expected_seed)
	{
		std::error_code error;
		const auto bytes = std::filesystem::file_size(path, error);
		if (error || bytes > kMaxFileBytes)
			throw std::length_error("multiway training checkpoint size is invalid");
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("multiway training checkpoint cannot be opened");
		std::array<char, kMagic.size()> magic{};
		in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
		std::uint32_t schema = 0U;
		if (!in || magic != kMagic || !portable::read_u32(in, schema) || schema != kSchema)
			throw std::runtime_error("multiway training checkpoint schema is invalid");
		MultiwayBlueprintTrainingCheckpoint result;
		require(read_identity(in, result.identity));
		require(read_metadata(in, result.training));
		auto& coverage = result.coverage;
		require(portable::read_u64(in, coverage.visited_public_descriptors) && portable::read_u64(in, coverage.admitted_rows) && portable::read_u64(in, coverage.admitted_action_cells) && portable::read_u64(in, coverage.terminal_visits) && portable::read_u64(in, coverage.leaf_visits) && portable::read_u64(in, coverage.missing_lookup_requests) && portable::read_u64(in, coverage.preflop_rows) && portable::read_u64(in, coverage.flop_rows) && portable::read_u64(in, coverage.turn_rows) && portable::read_u64(in, coverage.river_rows));
		for (auto& value : coverage.street_visits)
			require(portable::read_u64(in, value));
		require(portable::read_u64(in, result.coordinator.terminal_visits) && portable::read_u64(in, result.coordinator.leaf_visits) && portable::read_u64(in, result.coordinator.missing_lookup_requests) && portable::read_u64(in, result.coordinator.last_merged_stream_fingerprint));
		for (auto& value : result.coordinator.street_visits)
			require(portable::read_u64(in, value));
		std::uint32_t states = 0U;
		require(portable::read_u32(in, states) && states <= kMaxStates);
		result.coordinator.public_states.resize(states);
		for (auto& state : result.coordinator.public_states)
			require(read_state(in, state));
		std::uint32_t shapes = 0U;
		require(portable::read_u32(in, shapes) && shapes <= kMaxRows);
		result.coordinator.storage.shapes.resize(shapes);
		std::size_t expected_values = 0U;
		for (auto& shape : result.coordinator.storage.shapes)
		{
			require(portable::read_u64(in, shape.infoset.public_state.value) && portable::read_i32(in, shape.infoset.seat) && portable::read_u32(in, shape.bucket_count) && portable::read_u8(in, shape.action_count));
			const auto count = static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
			if (shape.bucket_count == 0U || shape.action_count == 0U || count > std::numeric_limits<std::size_t>::max() - expected_values)
				throw std::length_error("multiway training checkpoint row shapes are invalid");
			expected_values += count;
		}
		std::uint32_t active_pages = 0U;
		require(portable::read_u32(in, active_pages) && active_pages <= kMaxRows);
		result.coordinator.storage.active_pages.resize(active_pages);
		for (auto& page : result.coordinator.storage.active_pages)
			require(portable::read_u64(in, page.infoset.public_state.value) && portable::read_i32(in, page.infoset.seat) && portable::read_u32(in, page.page_index));
		std::uint64_t values = 0U;
		const auto maximum_compact_values = kMaxFileBytes / sizeof(std::uint64_t);
		require(portable::read_u64(in, values) && values <= maximum_compact_values);
		result.coordinator.storage.compact_regrets.resize(static_cast<std::size_t>(values));
		for (auto& value : result.coordinator.storage.compact_regrets)
			require(portable::read_i32(in, value));
		require(portable::read_u64(in, values) && values <= maximum_compact_values);
		result.coordinator.storage.compact_strategy_masses.resize(static_cast<std::size_t>(values));
		for (auto& value : result.coordinator.storage.compact_strategy_masses)
			require(portable::read_u64(in, value));
		const auto expected_u64 = static_cast<std::uint64_t>(expected_values);
		const auto maximum_values = kMaxFileBytes / sizeof(double);
		require(portable::read_u64(in, values) && values == expected_u64 && values <= maximum_values);
		result.coordinator.storage.regrets.resize(expected_values);
		for (auto& value : result.coordinator.storage.regrets)
			require(read_double(in, value));
		require(portable::read_u64(in, values) && values == expected_u64);
		result.coordinator.storage.strategy_sums.resize(expected_values);
		for (auto& value : result.coordinator.storage.strategy_sums)
			require(read_double(in, value));
		require(portable::read_u64(in, values) && values <= maximum_values);
		result.late_window_baseline.resize(static_cast<std::size_t>(values));
		for (auto& value : result.late_window_baseline)
			require(read_double(in, value));
		std::uint64_t stored_hash = 0U;
		require(portable::read_u64(in, stored_hash));
		if (in.peek() != std::char_traits<char>::eof())
			throw std::runtime_error("multiway training checkpoint has trailing data");
		result.validate();
		if (result.identity != expected_identity || result.training.schedule_hash != expected_schedule_hash || result.training.deterministic_seed != expected_seed || stored_hash != payload_hash(result))
			throw std::invalid_argument("multiway training checkpoint identity or payload does not match");
		return result;
	}
} // namespace texas::solver::multiway
