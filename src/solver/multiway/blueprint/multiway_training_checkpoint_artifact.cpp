#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
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

		class CountingCheckpointStreambuf final : public std::streambuf
		{
		protected:
			int_type overflow(int_type character) override
			{
				return traits_type::eq_int_type(character, traits_type::eof())
					? traits_type::not_eof(character)
					: character;
			}

			std::streamsize xsputn(const char*, std::streamsize count) override
			{
				return count;
			}
		};

		class TemporaryCheckpointFile final
		{
		public:
			explicit TemporaryCheckpointFile(std::filesystem::path path) noexcept
				: path_(std::move(path))
			{
			}

			~TemporaryCheckpointFile()
			{
				if (active_)
				{
					std::error_code error;
					std::filesystem::remove(path_, error);
				}
			}

			void release() noexcept { active_ = false; }

		private:
			std::filesystem::path path_;
			bool active_ = true;
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

	MultiwayTrainingCheckpointWriteResult MultiwayTrainingCheckpointArtifacts::save_atomic_streamed(
		const std::filesystem::path& path,
		const MultiwayTrainingCheckpointStreamSource& source,
		MultiwayCheckpointTelemetry* telemetry,
		MultiwayTrainingCheckpointWriteOptions options)
	{
		if (source.identity == nullptr || source.training == nullptr || source.coverage == nullptr || source.catalog == nullptr || source.storage == nullptr || source.late_window_baseline == nullptr)
		{
			throw std::invalid_argument("multiway training checkpoint stream source is incomplete");
		}
		source.identity->validate();
		if (source.training->trajectories < source.training->batches || source.coverage->admitted_rows != (source.compact_storage == nullptr ? source.storage->row_count() : source.compact_storage->row_count()) || source.coverage->terminal_visits != source.terminal_visits || source.coverage->leaf_visits != source.leaf_visits || source.coverage->missing_lookup_requests != source.missing_lookup_requests || source.coverage->street_visits != source.street_visits)
		{
			throw std::invalid_argument("multiway training checkpoint streamed metadata is invalid");
		}
		for (const auto value : *source.late_window_baseline)
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint baseline is non-finite");
		const auto state_count = source.catalog->checkpoint_node_count();
		if (state_count == 0U || state_count > kMaxStates || state_count > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("multiway training checkpoint state count is invalid");
		const auto& source_rows = source.compact_storage == nullptr
			? source.storage->rows()
			: source.compact_storage->rows();
		if (source_rows.size() > kMaxRows || source_rows.size() > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("multiway training checkpoint row count is invalid");

		std::vector<std::uint32_t> state_order(state_count);
		std::vector<std::uint32_t> state_depth(state_count, 0U);
		for (std::size_t index = 0U; index < state_count; ++index)
		{
			const auto* node = source.catalog->checkpoint_node_at(index);
			if (node == nullptr || node->id.value == 0U)
				throw std::invalid_argument("multiway training checkpoint packed state is invalid");
			std::uint32_t depth = 0U;
			for (auto current = node; current->parent_id.value != 0U; ++depth)
			{
				if (depth >= state_count)
					throw std::invalid_argument("multiway training checkpoint public-state parent chain is invalid");
				current = source.catalog->checkpoint_node(current->parent_id);
				if (current == nullptr)
					throw std::invalid_argument("multiway training checkpoint public-state parent is missing");
			}
			state_order[index] = static_cast<std::uint32_t>(index);
			state_depth[index] = depth;
		}
		std::sort(state_order.begin(), state_order.end(), [&source, &state_depth](std::uint32_t left, std::uint32_t right) {
			const auto* lhs = source.catalog->checkpoint_node_at(left);
			const auto* rhs = source.catalog->checkpoint_node_at(right);
			return state_depth[left] != state_depth[right]
				? state_depth[left] < state_depth[right]
				: lhs->id.value < rhs->id.value;
		});

		std::vector<std::uint32_t> row_order;
		if (source.compact_storage == nullptr)
		{
			row_order.resize(source_rows.size());
			for (std::size_t index = 0U; index < row_order.size(); ++index)
				row_order[index] = static_cast<std::uint32_t>(index);
			std::sort(row_order.begin(), row_order.end(), [&source](std::uint32_t left, std::uint32_t right) {
				const auto& lhs = source.storage->rows()[left].shape.infoset;
				const auto& rhs = source.storage->rows()[right].shape.infoset;
				return lhs.public_state.value != rhs.public_state.value
					? lhs.public_state.value < rhs.public_state.value
					: lhs.seat < rhs.seat;
			});
		}

		std::uint64_t dense_values = 0U;
		for (const auto& row : source_rows)
		{
			const auto values = static_cast<std::uint64_t>(row.shape.bucket_count) * row.shape.action_count;
			if (row.shape.bucket_count == 0U || row.shape.action_count == 0U || values > kMaxFileBytes / sizeof(double) || dense_values > kMaxFileBytes / sizeof(double) - values)
			{
				throw std::length_error("multiway training checkpoint value count is invalid");
			}
			dense_values += values;
		}
		if (source.compact_storage == nullptr)
		{
			for (std::size_t index = 0U; index < source.storage->row_count(); ++index)
			{
				const auto view = source.storage->checkpoint_row(index);
				if (view.metadata == nullptr || view.value_count == 0U)
					throw std::invalid_argument("multiway training checkpoint row view is invalid");
				for (std::size_t value = 0U; value < view.value_count; ++value)
					if (!std::isfinite(view.regrets[value]) || !std::isfinite(view.strategy_sums[value]))
						throw std::invalid_argument("multiway training checkpoint accumulator is non-finite");
			}
		}
		std::uint64_t compact_values = 0U;
		const auto page_count = source.compact_storage == nullptr ? 0U : source.compact_storage->page_count();
		for (std::size_t index = 0U; index < page_count; ++index)
		{
			const auto page = source.compact_storage->checkpoint_page(index);
			const auto* row = source.compact_storage->metadata(page.infoset);
			if (page.regrets == nullptr || page.strategy_masses == nullptr || row == nullptr || page.bucket_count == 0U || page.page_index * source.compact_storage->page_width() >= row->shape.bucket_count)
			{
				throw std::invalid_argument("multiway training checkpoint compact page is invalid");
			}
			const auto values = static_cast<std::uint64_t>(page.bucket_count) * row->shape.action_count;
			if (values > kMaxFileBytes / sizeof(std::uint64_t) || compact_values > kMaxFileBytes / sizeof(std::uint64_t) - values)
				throw std::length_error("multiway training checkpoint compact value count is invalid");
			compact_values += values;
		}

		MultiwayPublicStateDescriptor reusable_state;
		auto emit = [&](CheckpointPayloadWriter& payload, std::ostream& out) {
			out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
			portable::write_u32(out, kSchema);
			write_identity(payload, *source.identity);
			write_metadata(payload, *source.training);
			const auto& coverage = *source.coverage;
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
			payload.write_u64(source.terminal_visits);
			payload.write_u64(source.leaf_visits);
			payload.write_u64(source.missing_lookup_requests);
			payload.write_u64(source.last_merged_stream_fingerprint);
			for (const auto value : source.street_visits)
				payload.write_u64_unhashed(value);
			for (const auto value : coverage.street_visits)
				payload.hash_u64(value);
			for (const auto value : source.street_visits)
				payload.hash_u64(value);
			payload.write_u32(static_cast<std::uint32_t>(state_order.size()));
			for (const auto index : state_order)
			{
				const auto* node = source.catalog->checkpoint_node_at(index);
				if (node == nullptr || !source.catalog->reconstruct(node->id, reusable_state))
					throw std::runtime_error("multiway training checkpoint state reconstruction failed");
				write_state(payload, reusable_state);
			}
			payload.write_u32(static_cast<std::uint32_t>(source_rows.size()));
			const auto write_shape = [&payload](const MultiwaySparseRowShape& shape) {
				payload.write_u64(shape.infoset.public_state.value);
				payload.write_i32(shape.infoset.seat);
				payload.write_u32(shape.bucket_count);
				payload.write_u8(shape.action_count);
			};
			if (source.compact_storage != nullptr)
				for (const auto& row : source_rows)
					write_shape(row.shape);
			else
				for (const auto index : row_order)
					write_shape(source_rows[index].shape);
			payload.write_u32(static_cast<std::uint32_t>(page_count));
			for (std::size_t index = 0U; index < page_count; ++index)
			{
				const auto page = source.compact_storage->checkpoint_page(index);
				payload.write_u64(page.infoset.public_state.value);
				payload.write_i32(page.infoset.seat);
				payload.write_u32(page.page_index);
			}
			payload.write_u64(compact_values);
			for (std::size_t index = 0U; index < page_count; ++index)
			{
				const auto page = source.compact_storage->checkpoint_page(index);
				const auto actions = source.compact_storage->metadata(page.infoset)->shape.action_count;
				for (std::size_t value = 0U; value < static_cast<std::size_t>(page.bucket_count) * actions; ++value)
					payload.write_i32(page.regrets[value]);
			}
			payload.write_u64(compact_values);
			for (std::size_t index = 0U; index < page_count; ++index)
			{
				const auto page = source.compact_storage->checkpoint_page(index);
				const auto actions = source.compact_storage->metadata(page.infoset)->shape.action_count;
				for (std::size_t value = 0U; value < static_cast<std::size_t>(page.bucket_count) * actions; ++value)
					payload.write_u64(page.strategy_masses[value]);
			}
			payload.write_u64(dense_values);
			const auto write_dense = [&](bool strategies) {
				const auto emit_row = [&](const MultiwaySparseRowMetadata& row, const double* values) {
					for (std::size_t value = 0U; value < row.value_count(); ++value)
						payload.write_double(values[value]);
				};
				if (source.compact_storage == nullptr)
				{
					for (const auto index : row_order)
					{
						const auto view = source.storage->checkpoint_row(index);
						emit_row(*view.metadata, strategies ? view.strategy_sums : view.regrets);
					}
					return;
				}
				std::size_t page_cursor = 0U;
				for (const auto& row : source_rows)
				{
					const auto first_page = page_cursor;
					while (page_cursor < page_count && source.compact_storage->checkpoint_page(page_cursor).infoset == row.shape.infoset)
						++page_cursor;
					for (std::uint8_t action = 0U; action < row.shape.action_count; ++action)
					{
						for (std::uint32_t bucket = 0U; bucket < row.shape.bucket_count; ++bucket)
						{
							double value = 0.0;
							for (std::size_t page_index = first_page; page_index < page_cursor; ++page_index)
							{
								const auto page = source.compact_storage->checkpoint_page(page_index);
								const auto first_bucket = page.page_index * source.compact_storage->page_width();
								if (bucket >= first_bucket && bucket < first_bucket + page.bucket_count)
								{
									const auto offset = static_cast<std::size_t>(action) * page.bucket_count + bucket - first_bucket;
									value = strategies
										? static_cast<double>(page.strategy_masses[offset]) / static_cast<double>(MultiwayCompactStorage::kMassScale)
										: static_cast<double>(page.regrets[offset]) / 1024.0;
									break;
								}
							}
							payload.write_double(value);
						}
					}
				}
			};
			write_dense(false);
			payload.write_u64(dense_values);
			write_dense(true);
			payload.write_double_array(*source.late_window_baseline);
		};

		CountingCheckpointStreambuf counting_buffer;
		std::ostream counting_out(&counting_buffer);
		CheckpointPayloadWriter counting_payload(counting_out);
		emit(counting_payload, counting_out);
		const auto plan_bytes = 12U + counting_payload.bytes_written() + sizeof(std::uint64_t);
		if (!counting_out || plan_bytes > kMaxFileBytes)
			throw std::length_error("multiway training checkpoint planned size is invalid");
		MultiwayExportProcessGateResult process_gate;
		MultiwayExportSystemGateResult system_gate;
		if (source.gate != nullptr)
		{
			const MultiwayExportPlanMemoryValues gate_plan{
				MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION,
				static_cast<std::uint64_t>(state_count),
				static_cast<std::uint64_t>(source_rows.size()),
				dense_values,
				plan_bytes,
				static_cast<std::uint64_t>((state_order.size() + state_depth.size() + row_order.size()) * sizeof(std::uint32_t)),
				source.compact_storage != nullptr
			};
			const auto scratch = make_multiway_export_scratch_estimate(gate_plan, source.gate->process);
			const auto process = capture_multiway_process_memory_snapshot();
			process_gate = evaluate_multiway_export_process_gate(gate_plan, process, source.gate->process);
			const auto system = capture_multiway_system_memory_snapshot();
			system_gate = evaluate_multiway_export_system_gate(gate_plan, scratch, system, source.gate->system);
			if (process_gate.blocked() || system_gate.blocked())
				throw std::runtime_error("multiway training checkpoint memory gate rejected");
		}

		const auto temporary = path.string() + ".tmp";
		TemporaryCheckpointFile cleanup(temporary);
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
			throw std::runtime_error("multiway training checkpoint temporary file cannot be opened");
		BufferedCheckpointStreambuf buffer(file);
		std::ostream out(&buffer);
		CheckpointPayloadWriter payload(out);
		emit(payload, out);
		const auto hash = payload.finish_hash();
		portable::write_u64(out, hash);
		if (options.fail_before_close)
			throw std::runtime_error("multiway training checkpoint injected close failure");
		out.flush();
		file.close();
		std::error_code size_error;
		const auto actual_bytes = std::filesystem::file_size(temporary, size_error);
		if (!out || !file || size_error || actual_bytes != plan_bytes)
			throw std::runtime_error("multiway training checkpoint streamed write failed");
		if (options.fail_before_publication)
			throw std::runtime_error("multiway training checkpoint injected publication failure");
		const auto mode = core::publish_atomic_replace(temporary, path,
			"multiway training checkpoint publish failed",
			core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		cleanup.release();
		if (telemetry != nullptr)
			telemetry->atomic_publication_mode = mode;
		MultiwayTrainingCheckpointWriteResult result;
		result.plan = { static_cast<std::uint64_t>(state_count), static_cast<std::uint64_t>(source_rows.size()),
			static_cast<std::uint64_t>(page_count), compact_values, dense_values, plan_bytes,
			static_cast<std::uint64_t>((state_order.size() + state_depth.size() + row_order.size()) * sizeof(std::uint32_t)) };
		result.payload_hash = hash;
		result.serialized_bytes = actual_bytes;
		result.peak_scratch_bytes = result.plan.retained_scratch_bytes + kCheckpointWriteBufferBytes;
		result.publication_mode = mode;
		result.process_gate = process_gate;
		result.system_gate = system_gate;
		return result;
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
