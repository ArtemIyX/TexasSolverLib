#include "solver/multiway/evaluation/multiway_evaluation.hpp"
#include "core/fingerprint.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		constexpr double kProbabilityTolerance = 1e-12;
		double confidence_z_score(double confidence_level) noexcept
		{
			if (confidence_level == 0.90)
				return 1.6448536269514722;
			if (confidence_level == 0.95)
				return 1.959963984540054;
			return 2.5758293035489004;
		}

		using texas::core::fingerprint::append_u64;
		void append_double(std::uint64_t& hash, double value) noexcept
		{
			std::uint64_t bits = 0U;
			static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
			std::memcpy(&bits, &value, sizeof(bits));
			append_u64(hash, bits);
		}

		void append_action(std::uint64_t& hash, const MultiwayActionDescriptor& action) noexcept
		{
			append_u64(hash, static_cast<std::uint64_t>(action.action));
			append_u64(hash, action.action_index);
			append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(action.target_street_contribution)));
			append_u64(hash, action.action_menu_id);
		}

		void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
		}

		void append_history(std::uint64_t& hash, const MultiwayHandHistory& history) noexcept
		{
			append_u64(hash, history.schema_version);
			append_u64(hash, history.hand_seed);
			append_u64(hash, history.initial_config.starting_stacks.size());
			for (const auto value : history.initial_config.starting_stacks)
				append_u64(hash, value);
			append_u64(hash, history.initial_config.initial_contributions.size());
			for (const auto value : history.initial_config.initial_contributions)
				append_u64(hash, value);
			append_u64(hash, history.initial_config.initial_street_contributions.size());
			for (const auto value : history.initial_config.initial_street_contributions)
				append_u64(hash, value);
			append_u64(hash, static_cast<std::uint64_t>(history.initial_config.first_player));
			append_u64(hash, static_cast<std::uint64_t>(history.initial_config.big_blind));
			append_u64(hash, static_cast<std::uint64_t>(history.initial_config.street));
			append_u64(hash, history.initial_config.rake_policy.identity());
			append_u64(hash, history.events.size());
			for (const auto& event : history.events)
			{
				append_u64(hash, static_cast<std::uint64_t>(event.kind));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.acting_seat));
				append_u64(hash, static_cast<std::uint64_t>(event.decision.action));
				append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(event.decision.target_street_contribution)));
				append_u64(hash, event.decision.decision_seed);
				append_u64(hash, static_cast<std::uint64_t>(event.next_street));
				append_u64(hash, static_cast<std::uint64_t>(event.first_player));
			}
		}

		std::uint64_t aivat_record_hash(const MultiwayAivatEvaluationRecord& record) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, record.schema_version);
			append_identity(hash, record.identity);
			append_history(hash, record.public_history);
			append_u64(hash, record.raw_chip_outcome.seat_count);
			for (std::size_t seat = 0U; seat < record.raw_chip_outcome.seat_count; ++seat)
			{
				append_double(hash, record.raw_chip_outcome.values[seat]);
			}
			append_u64(hash, record.decisions.size());
			for (const auto& decision : record.decisions)
			{
				append_u64(hash, decision.decision_index);
				append_u64(hash, static_cast<std::uint64_t>(decision.acting_seat));
				append_action(hash, decision.sampled_action);
				append_u64(hash, decision.decision_seed);
				append_u64(hash, decision.action_values.size());
				for (const auto& value : decision.action_values)
				{
					append_action(hash, value.action);
					append_double(hash, value.probability);
					append_double(hash, value.estimated_value);
				}
			}
			return hash;
		}

	} // namespace

	void MultiwayAivatEvaluationRecord::seal() noexcept
	{
		integrity_hash = aivat_record_hash(*this);
	}

	void MultiwayAivatEvaluationRecord::validate() const
	{
		if (schema_version != MULTIWAY_AIVAT_EVALUATION_RECORD_SCHEMA_VERSION || integrity_hash == 0U)
		{
			throw std::invalid_argument("multiway AIVAT record has invalid schema or integrity hash");
		}
		identity.validate();
		public_history.validate();
		raw_chip_outcome.validate();
		if (raw_chip_outcome.seat_count != public_history.initial_config.starting_stacks.size())
		{
			throw std::invalid_argument("multiway AIVAT record outcome seat count is invalid");
		}
		std::size_t expected = 0U;
		for (const auto& event : public_history.events)
		{
			if (event.kind != MultiwayReplayEventKind::Decision)
				continue;
			if (expected >= decisions.size())
			{
				throw std::invalid_argument("multiway AIVAT record is missing a decision");
			}
			const auto& decision = decisions[expected];
			if (decision.decision_index != expected + 1U || decision.acting_seat != event.decision.acting_seat || decision.sampled_action.action != event.decision.action || decision.sampled_action.target_street_contribution != event.decision.target_street_contribution || decision.decision_seed != event.decision.decision_seed || decision.action_values.empty())
			{
				throw std::invalid_argument("multiway AIVAT record decision does not match public history");
			}
			double total_probability = 0.0;
			bool found_sampled = false;
			for (std::size_t index = 0U; index < decision.action_values.size(); ++index)
			{
				const auto& value = decision.action_values[index];
				if (!std::isfinite(value.probability) || value.probability < 0.0 || value.probability > 1.0 || !std::isfinite(value.estimated_value) || value.action.action_menu_id == 0U)
				{
					throw std::invalid_argument("multiway AIVAT record action value is invalid");
				}
				for (std::size_t prior = 0U; prior < index; ++prior)
				{
					if (value.action == decision.action_values[prior].action)
					{
						throw std::invalid_argument("multiway AIVAT record has duplicate action values");
					}
				}
				total_probability += value.probability;
				found_sampled = found_sampled || value.action == decision.sampled_action;
			}
			if (!found_sampled || std::fabs(total_probability - 1.0) > kProbabilityTolerance)
			{
				throw std::invalid_argument("multiway AIVAT record policy is invalid");
			}
			++expected;
		}
		if (expected != decisions.size() || integrity_hash != aivat_record_hash(*this))
		{
			throw std::invalid_argument("multiway AIVAT record integrity check failed");
		}
	}

	void MultiwayAivatEstimate::validate() const
	{
		if (seat_count < 2U || seat_count > kMultiwayEvaluationMaxSeats || (confidence_level != 0.90 && confidence_level != 0.95 && confidence_level != 0.99))
		{
			throw std::invalid_argument("multiway AIVAT estimate is invalid");
		}
		for (std::size_t seat = 0U; seat < seat_count; ++seat)
		{
			if (!std::isfinite(means[seat]) || !std::isfinite(standard_errors[seat]) || !std::isfinite(confidence_interval_half_widths[seat]) || standard_errors[seat] < 0.0 || confidence_interval_half_widths[seat] < 0.0)
			{
				throw std::invalid_argument("multiway AIVAT estimate contains invalid values");
			}
		}
	}

	MultiwayAivatEstimate estimate_multiway_aivat(
		const std::vector<MultiwayAivatEvaluationRecord>& records,
		double confidence_level)
	{
		if (records.empty() || (confidence_level != 0.90 && confidence_level != 0.95 && confidence_level != 0.99))
		{
			throw std::invalid_argument("multiway AIVAT estimator requires records and supported confidence");
		}
		records.front().validate();
		const auto identity = records.front().identity;
		const auto seats = static_cast<std::size_t>(records.front().raw_chip_outcome.seat_count);
		std::array<Value, kMultiwayEvaluationMaxSeats> mean{};
		std::array<Value, kMultiwayEvaluationMaxSeats> m2{};
		std::uint64_t result_count = 0U;
		for (const auto& record : records)
		{
			record.validate();
			if (record.identity != identity || record.raw_chip_outcome.seat_count != seats)
			{
				throw std::invalid_argument("multiway AIVAT records have incompatible identities");
			}
			std::array<Value, kMultiwayEvaluationMaxSeats> adjusted = record.raw_chip_outcome.values;
			for (const auto& decision : record.decisions)
			{
				Value expected = 0.0;
				Value sampled = 0.0;
				bool found = false;
				for (const auto& value : decision.action_values)
				{
					expected += value.probability * value.estimated_value;
					if (value.action == decision.sampled_action)
					{
						sampled = value.estimated_value;
						found = true;
					}
				}
				if (!found)
					throw std::invalid_argument("multiway AIVAT sampled action is missing");
				adjusted[static_cast<std::size_t>(decision.acting_seat)] -= sampled - expected;
			}
			const auto count = static_cast<Value>(++result_count);
			for (std::size_t seat = 0U; seat < seats; ++seat)
			{
				const auto delta = adjusted[seat] - mean[seat];
				mean[seat] += delta / count;
				m2[seat] += delta * (adjusted[seat] - mean[seat]);
			}
		}
		MultiwayAivatEstimate result;
		result.samples = records.size();
		result.seat_count = static_cast<std::uint8_t>(seats);
		result.confidence_level = confidence_level;
		for (std::size_t seat = 0U; seat < seats; ++seat)
		{
			result.means[seat] = mean[seat];
			if (result.samples > 1U)
				result.standard_errors[seat] = std::sqrt(m2[seat] / (result.samples - 1U) / result.samples);
			result.confidence_interval_half_widths[seat] = confidence_z_score(confidence_level) * result.standard_errors[seat];
		}
		result.validate();
		return result;
	}

	bool publish_multiway_aivat_evaluation_record(
		const MultiwayAivatEvaluationRecord& record,
		MultiwayAivatEvaluationRecordSinkFn sink,
		const void* context)
	{
		if (sink == nullptr)
			throw std::invalid_argument("multiway AIVAT record sink is required");
		record.validate();
		return sink(record, context);
	}


} // namespace texas::solver::multiway
