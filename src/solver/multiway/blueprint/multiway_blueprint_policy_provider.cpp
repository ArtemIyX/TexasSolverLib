#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"

#include "core/fingerprint.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"

#include <limits>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		std::uint64_t lookup_diagnostic_priority(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint64_t action_menu_id) noexcept
		{
			std::uint64_t priority = 1469598103934665603ULL;
			core::fingerprint::append_u64(priority, infoset.public_state.value);
			core::fingerprint::append_u64(priority, static_cast<std::uint64_t>(infoset.seat));
			core::fingerprint::append_u64(priority, bucket);
			core::fingerprint::append_u64(priority, action_menu_id);
			return priority;
		}
	} // namespace

	void MultiwayBlueprintLookupAudit::set_diagnostic_sample_limit(
		std::size_t limit, std::uint64_t trajectory_count)
	{
		if (limit > MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_SAMPLES)
			throw std::invalid_argument("multiway lookup diagnostic sample limit exceeds the configured maximum");
		if (limit != 0U && trajectory_count == 0U)
			throw std::invalid_argument("multiway lookup diagnostics require a non-zero trajectory count");
		diagnostic_sample_limit_ = limit;
		diagnostic_sample_priorities_.fill(0U);
		diagnostic_samples_.clear();
		diagnostic_samples_.reserve(diagnostic_sample_limit_);
	}

	void MultiwayBlueprintLookupAudit::record(
		MultiwayBlueprintLookupStatus status, MultiwayInfosetId infoset,
		std::uint32_t bucket, std::uint64_t action_menu_id,
		std::uint64_t trajectory_id,
		const MultiwayPublicStateDescriptor* public_state) noexcept
	{
		const auto lookup_ordinal = lookup_ordinal_;
		if (diagnostic_sample_limit_ != 0U
			&& lookup_ordinal_ != std::numeric_limits<std::uint64_t>::max())
			++lookup_ordinal_;
		core::fingerprint::append_u64(fingerprint_state_, static_cast<std::uint8_t>(status));
		core::fingerprint::append_u64(fingerprint_state_, infoset.public_state.value);
		core::fingerprint::append_u64(fingerprint_state_,
			static_cast<std::uint64_t>(static_cast<std::int64_t>(infoset.seat)));
		core::fingerprint::append_u64(fingerprint_state_, bucket);
		core::fingerprint::append_u64(fingerprint_state_, action_menu_id);
		switch (status)
		{
			case MultiwayBlueprintLookupStatus::Hit:
				++lookup_hits;
				break;
			case MultiwayBlueprintLookupStatus::Missing:
				++missing_infosets;
				break;
			case MultiwayBlueprintLookupStatus::MissingBucket:
				++missing_buckets;
				break;
			case MultiwayBlueprintLookupStatus::IncompatibleMenu:
				++action_menu_mismatches;
				break;
		}

		if (diagnostic_sample_limit_ == 0U || public_state == nullptr
			|| status != MultiwayBlueprintLookupStatus::Missing)
			return;

		for (const auto& existing : diagnostic_samples_)
		{
			if (existing.infoset == infoset && existing.bucket == bucket
				&& existing.action_menu_id == action_menu_id)
				return;
		}
		const auto priority = lookup_diagnostic_priority(infoset, bucket, action_menu_id);
		std::size_t sample_slot = diagnostic_samples_.size();
		if (sample_slot == diagnostic_sample_limit_)
		{
			sample_slot = 0U;
			for (std::size_t index = 1U; index < diagnostic_samples_.size(); ++index)
				if (diagnostic_sample_priorities_[index] > diagnostic_sample_priorities_[sample_slot])
					sample_slot = index;
			if (priority >= diagnostic_sample_priorities_[sample_slot])
				return;
		}

		MultiwayBlueprintLookupDiagnostic sample;
		sample.status = status;
		sample.trajectory_id = trajectory_id;
		sample.replay_lookup_ordinal = lookup_ordinal;
		sample.infoset = infoset;
		sample.bucket = bucket;
		sample.action_menu_id = action_menu_id;
		sample.canonical_history_id = public_state->canonical_history_id;
		sample.street = static_cast<std::uint8_t>(public_state->betting.street);
		sample.identity_context_complete = true;
		if (public_state->id != infoset.public_state
			|| public_state->legal_actions.empty()
			|| public_state->legal_actions.front().action_menu_id != action_menu_id)
			sample.identity_context_complete = false;

		if (public_state->board.size() > sample.board.size())
			sample.identity_context_complete = false;
		sample.board_count = static_cast<std::uint8_t>(public_state->board.size() < sample.board.size()
			? public_state->board.size() : sample.board.size());
		for (std::size_t index = 0U; index < sample.board_count; ++index)
			sample.board[index] = public_state->board[index];

		const auto seat_count = public_state->betting.stacks.size();
		const bool seat_vectors_match = public_state->betting.contributions.size() == seat_count
			&& public_state->betting.street_contributions.size() == seat_count
			&& public_state->betting.folded.size() == seat_count
			&& public_state->betting.all_in.size() == seat_count
			&& public_state->betting.may_raise.size() == seat_count
			&& public_state->betting.pending.size() == seat_count
			&& public_state->betting.has_acted.size() == seat_count
			&& public_state->betting.bet_faced_when_acted.size() == seat_count;
		if (seat_count > sample.stacks.size() || !seat_vectors_match)
			sample.identity_context_complete = false;
		if (seat_count > sample.stacks.size() || !seat_vectors_match)
			sample.seat_count = 0U;
		else
			sample.seat_count = static_cast<std::uint8_t>(seat_count);
		for (std::size_t index = 0U; index < sample.seat_count; ++index)
		{
			sample.stacks[index] = public_state->betting.stacks[index];
			sample.contributions[index] = public_state->betting.contributions[index];
			sample.street_contributions[index] = public_state->betting.street_contributions[index];
			sample.folded[index] = static_cast<std::uint8_t>(public_state->betting.folded[index]);
			sample.all_in[index] = static_cast<std::uint8_t>(public_state->betting.all_in[index]);
			sample.may_raise[index] = static_cast<std::uint8_t>(public_state->betting.may_raise[index]);
			sample.pending[index] = static_cast<std::uint8_t>(public_state->betting.pending[index]);
			sample.has_acted[index] = static_cast<std::uint8_t>(public_state->betting.has_acted[index]);
			sample.bet_faced_when_acted[index] = public_state->betting.bet_faced_when_acted[index];
		}
		sample.current_player = public_state->betting.current_player;
		sample.last_aggressor = public_state->betting.last_aggressor;
		sample.current_bet = public_state->betting.current_bet;
		sample.last_full_raise_size = public_state->betting.last_full_raise_size;
		sample.big_blind = public_state->betting.big_blind;

		if (public_state->history.size() > sample.history.size()
			|| public_state->history.size() > std::numeric_limits<std::uint32_t>::max())
			sample.identity_context_complete = false;
		sample.history_count = static_cast<std::uint32_t>(public_state->history.size() <= std::numeric_limits<std::uint32_t>::max()
			? public_state->history.size() : std::numeric_limits<std::uint32_t>::max());
		sample.history_stored_count = static_cast<std::uint8_t>(public_state->history.size() < sample.history.size()
			? public_state->history.size() : sample.history.size());
		for (std::size_t index = 0U; index < sample.history_stored_count; ++index)
			sample.history[index] = public_state->history[index];

		if (public_state->legal_actions.size() > sample.legal_actions.size())
			sample.identity_context_complete = false;
		sample.legal_action_count = static_cast<std::uint8_t>(public_state->legal_actions.size() < sample.legal_actions.size()
			? public_state->legal_actions.size() : sample.legal_actions.size());
		for (std::size_t index = 0U; index < sample.legal_action_count; ++index)
			sample.legal_actions[index] = public_state->legal_actions[index];
		if (sample.identity_context_complete)
		{
			sample.reconstructed_public_state_id = MultiwayPublicBuilder::stable_public_state_id(
				public_state->betting, public_state->board, public_state->history,
				public_state->legal_actions);
			sample.identity_context_complete =
				sample.reconstructed_public_state_id == infoset.public_state.value;
		}
		if (sample_slot == diagnostic_samples_.size())
			diagnostic_samples_.push_back(sample);
		else
			diagnostic_samples_[sample_slot] = sample;
		diagnostic_sample_priorities_[sample_slot] = priority;
	}

	std::uint64_t MultiwayBlueprintLookupAudit::fingerprint() const noexcept
	{
		return core::fingerprint::finish(fingerprint_state_);
	}

	MultiwayBlueprintLookupStatus MultiwayBlueprintPolicyProvider::strategy_into(
		MultiwayInfosetId infoset,
		std::uint32_t bucket,
		const MultiwayActionDescriptor* legal_actions,
		std::size_t action_count,
		Probability* output) const noexcept
	{
		if (mapped_store_ != nullptr)
			return mapped_store_->strategy_into(infoset, bucket, legal_actions, action_count, output);
		if (store_ == nullptr || legal_actions == nullptr || output == nullptr || action_count == 0U)
		{
			return MultiwayBlueprintLookupStatus::Missing;
		}
		const auto menu_id = legal_actions[0].action_menu_id;
		bool infoset_present = false;
		bool bucket_present = false;
		const auto row = store_->find(infoset, bucket, menu_id, infoset_present, bucket_present);
		if (!row.valid())
		{
			if (bucket_present)
			{
				return MultiwayBlueprintLookupStatus::IncompatibleMenu;
			}
			return infoset_present
				? MultiwayBlueprintLookupStatus::MissingBucket
				: MultiwayBlueprintLookupStatus::Missing;
		}
		if (row.action_count != action_count)
			return MultiwayBlueprintLookupStatus::IncompatibleMenu;
		for (std::size_t index = 0U; index < action_count; ++index)
		{
			if (row.actions[index].action != legal_actions[index])
			{
				return MultiwayBlueprintLookupStatus::IncompatibleMenu;
			}
		}
		constexpr auto total = static_cast<Probability>(std::numeric_limits<std::uint16_t>::max());
		for (std::size_t index = 0U; index < action_count; ++index)
		{
			output[index] = static_cast<Probability>(row.actions[index].probability) / total;
		}
		return MultiwayBlueprintLookupStatus::Hit;
	}

} // namespace texas::solver::multiway
