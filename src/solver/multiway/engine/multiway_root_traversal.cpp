#include "solver/multiway/engine/multiway_traversal.hpp"
#include "core/fingerprint.hpp"
#include "games/multiway_fixed.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{
	namespace
	{
		void hash_u64(std::uint64_t value, std::uint64_t& hash) noexcept
		{
			texas::core::fingerprint::append_u64(hash, value);
		}

		std::uint64_t private_range_identity(const MultiwayPrivateConfig& ranges) noexcept
		{
			std::uint64_t hash = 1469598103934665603ULL;
			hash_u64(ranges.board.size(), hash);
			for (const auto card : ranges.board)
				hash_u64(card, hash);
			hash_u64(ranges.ranges.size(), hash);
			for (const auto& seat : ranges.ranges)
			{
				hash_u64(seat.size(), hash);
				for (const auto& entry : seat)
				{
					hash_u64(entry.hole[0], hash);
					hash_u64(entry.hole[1], hash);
					std::uint64_t weight_bits = 0;
					static_assert(sizeof(weight_bits) == sizeof(entry.weight));
					std::memcpy(&weight_bits, &entry.weight, sizeof(weight_bits));
					hash_u64(weight_bits, hash);
				}
			}
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t range_context_identity(
			std::uint64_t range_model_identity,
			const Probability* reaches,
			std::size_t count) noexcept
		{
			auto hash = range_model_identity;
			hash_u64(count, hash);
			for (std::size_t player = 0; player < count; ++player)
			{
				std::uint64_t reach_bits = 0;
				static_assert(sizeof(reach_bits) == sizeof(reaches[player]));
				std::memcpy(&reach_bits, reaches + player, sizeof(reach_bits));
				hash_u64(reach_bits, hash);
			}
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t private_deal_identity(
			const MultiwayTerminalAdapter& terminal,
			const MultiwaySamplerDealToken& deal,
			std::size_t player_count)
		{
			std::uint64_t hash = 1469598103934665603ULL;
			hash_u64(player_count, hash);
			for (std::size_t player = 0; player < player_count; ++player)
			{
				auto hole = terminal.sampled_hole(deal, static_cast<PlayerId>(player));
				if (hole[1] < hole[0])
					std::swap(hole[0], hole[1]);
				hash_u64(hole[0], hash);
				hash_u64(hole[1], hash);
			}
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t next_random(std::uint64_t& state) noexcept
		{
			state += 0x9e3779b97f4a7c15ULL;
			auto value = state;
			value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
			value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
			return value ^ (value >> 31U);
		}

		std::size_t sample_action(
			const Probability* strategy,
			std::size_t action_count,
			std::uint64_t& random_state) noexcept
		{
			const auto bits = next_random(random_state) >> 11U;
			const auto sample = static_cast<Probability>(bits) * (1.0 / 9007199254740992.0);
			Probability cumulative = 0.0;
			for (std::size_t action = 0; action + 1U < action_count; ++action)
			{
				cumulative += strategy[action];
				if (sample < cumulative)
					return action;
			}
			return action_count - 1U;
		}

		std::uint32_t preflop_class(const std::array<std::uint8_t, 2>& hole) noexcept
		{
			const auto high = std::max(core::rank_of(hole[0]), core::rank_of(hole[1]));
			const auto low = std::min(core::rank_of(hole[0]), core::rank_of(hole[1]));
			const auto high_pos = static_cast<std::size_t>(14U - high);
			const auto low_pos = static_cast<std::size_t>(14U - low);
			if (high == low)
				return static_cast<std::uint32_t>(high_pos);
			const auto pair_index = high_pos * 12U - (high_pos * (high_pos - 1U)) / 2U + (low_pos - high_pos - 1U);
			return static_cast<std::uint32_t>((core::suit_of(hole[0]) == core::suit_of(hole[1]) ? 13U : 91U) + pair_index);
		}

		bool append_infoset_update_noalloc(
			MultiwayWorkerDeltaStream& stream,
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			std::uint64_t trajectory_id,
			const Probability* player_reaches,
			std::size_t player_count,
			PlayerId traverser,
			Probability chance_reach,
			Probability sampling_reach,
			const Probability* strategy,
			const Value* action_values,
			std::size_t action_count,
			double iteration_weight,
			Value& node_value)
		{
			if (infoset.public_state.value == 0U || infoset.seat != traverser || player_reaches == nullptr || player_count < 2U || player_count > 6U || traverser < 0 || static_cast<std::size_t>(traverser) >= player_count || strategy == nullptr || action_values == nullptr || action_count == 0U || !std::isfinite(chance_reach) || chance_reach < 0.0 || chance_reach > 1.0 || !std::isfinite(sampling_reach) || sampling_reach <= 0.0 || sampling_reach > 1.0 || !std::isfinite(iteration_weight) || iteration_weight <= 0.0)
			{
				throw std::invalid_argument("multiway allocation-free update has invalid inputs");
			}
			if (stream.capacity() - stream.size() < action_count)
			{
				stream.mark_capacity_exhausted();
				return false;
			}

			Probability counterfactual_reach = chance_reach;
			for (std::size_t player = 0; player < player_count; ++player)
			{
				const auto reach = player_reaches[player];
				if (!std::isfinite(reach) || reach < 0.0 || reach > 1.0)
				{
					throw std::invalid_argument("multiway allocation-free update has an invalid player reach");
				}
				if (static_cast<PlayerId>(player) != traverser)
					counterfactual_reach *= reach;
			}
			const auto importance_weight = counterfactual_reach / sampling_reach;
			const auto average_strategy_weight =
				player_reaches[static_cast<std::size_t>(traverser)] / sampling_reach;
			if (!std::isfinite(importance_weight) || !std::isfinite(average_strategy_weight))
			{
				throw std::overflow_error("multiway allocation-free importance weight is non-finite");
			}

			node_value = 0.0;
			Probability strategy_total = 0.0;
			for (std::size_t action = 0; action < action_count; ++action)
			{
				if (!std::isfinite(strategy[action]) || strategy[action] < 0.0 || strategy[action] > 1.0 || !std::isfinite(action_values[action]))
				{
					throw std::invalid_argument("multiway allocation-free update has invalid action data");
				}
				strategy_total += strategy[action];
				node_value += strategy[action] * action_values[action];
			}
			if (std::fabs(strategy_total - 1.0) > 1e-12 || !std::isfinite(node_value))
			{
				throw std::overflow_error("multiway allocation-free update has invalid normalization");
			}
			for (std::size_t action = 0; action < action_count; ++action)
			{
				const auto regret = importance_weight * (action_values[action] - node_value);
				const auto strategy_sum = average_strategy_weight * strategy[action] * iteration_weight;
				if (!std::isfinite(regret) || !std::isfinite(strategy_sum))
				{
					throw std::overflow_error("multiway allocation-free update contains a non-finite delta");
				}
			}
			for (std::size_t action = 0; action < action_count; ++action)
			{
				const auto appended = stream.try_append({
					infoset,
					bucket,
					static_cast<std::uint8_t>(action),
					importance_weight * (action_values[action] - node_value),
					average_strategy_weight * strategy[action] * iteration_weight,
					trajectory_id,
				});
				if (!appended)
				{
					throw std::logic_error("multiway delta capacity changed during allocation-free append");
				}
			}
			return true;
		}
	}

	void MultiwayTraversalPruningConfig::validate() const
	{
		if (!std::isfinite(exploration_probability) || exploration_probability < 0.0 || exploration_probability > 1.0 || !std::isfinite(action_probability_threshold) || action_probability_threshold < 0.0 || action_probability_threshold > 1.0 || !std::isfinite(regret_threshold) || regret_threshold > 0.0)
		{
			throw std::invalid_argument("multiway traversal pruning exploration probability is invalid");
		}
	}

	bool MultiwayTraversalPruningConfig::recovery_batch(std::uint64_t batch_number) const noexcept
	{
		return enabled && batch_number > warmup_batches && recovery_interval_batches != 0U && batch_number % recovery_interval_batches == 0U;
	}

	bool MultiwayTraversalPruningConfig::should_explore_action(
		std::uint64_t batch_number, bool below_threshold,
		bool immediate_terminal, bool river) const noexcept
	{
		if (!enabled || batch_number <= warmup_batches || immediate_terminal || river)
			return false;
		if (recovery_batch(batch_number))
			return true;
		return below_threshold;
	}

	MultiwayRootExternalSamplingTraversal::MultiwayRootExternalSamplingTraversal(
		MultiwaySolverCoordinator& coordinator,
		const MultiwayRootSnapshot& root,
		const MultiwayActionAbstraction& action_abstraction,
		const MultiwayBucketRegistry& buckets,
		const MultiwayLeafEvaluator* leaf_evaluator,
		std::uint32_t max_decision_depth,
		std::uint32_t max_public_chance_depth,
		const MultiwayBlueprintPolicyProvider* blueprint_policy,
		const MultiwayFixedContinuationSelector* continuation_selector,
		const MultiwayFutureBucketArtifact* future_bucket_artifact,
		MultiwayTraversalPruningConfig pruning)
		: coordinator_(&coordinator), root_(&root), action_abstraction_(&action_abstraction), buckets_(&buckets), leaf_evaluator_(leaf_evaluator), max_decision_depth_(max_decision_depth), max_public_chance_depth_(max_public_chance_depth), blueprint_policy_(blueprint_policy), continuation_selector_(continuation_selector), future_bucket_artifact_(future_bucket_artifact), pruning_(pruning), range_model_identity_(private_range_identity(root.private_ranges)), terminal_(coordinator)
	{
		root.validate();
		pruning_.validate();
		if (root.public_state.betting.street < Street::Preflop || root.public_state.betting.street > Street::River)
		{
			throw std::invalid_argument("multiway root traversal requires a valid street");
		}
		if (max_decision_depth_ == 0U || max_decision_depth_ > MULTIWAY_MAX_DECISION_DEPTH)
		{
			throw std::invalid_argument("multiway traversal decision depth is outside the supported range");
		}
		if (max_public_chance_depth_ > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH)
		{
			throw std::invalid_argument("multiway traversal public chance depth is outside the supported range");
		}
		if (root.public_state.legal_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS)
		{
			throw std::invalid_argument("multiway root action menu exceeds the compact traversal limit");
		}
	}

struct MultiwayRootExternalSamplingTraversal::TraversalContext
	{
		const MultiwayTerminalAdapter* terminal = nullptr;
		const MultiwaySamplerDealToken* deal = nullptr;
		MultiwayWorkerDeltaStream* stream = nullptr;
		std::array<Probability, 6> player_reaches{};
		std::size_t player_count = 0;
		PlayerId traverser = -1;
		std::uint64_t trajectory_id = 0;
		std::uint32_t continuation_sequence = 0U;
		MultiwayContinuationDeltaStream* continuation_stream = nullptr;
		std::uint64_t random_state = 0;
		Probability private_chance_reach = 0.0;
		Probability private_sampling_reach = 0.0;
		Probability public_chance_reach = 1.0;
		Probability public_sampling_reach = 1.0;
		double iteration_weight = 1.0;
		std::uint64_t batch_number = 0;
		MultiwaySearchProfile* profile = nullptr;
		MultiwayBlueprintLookupAudit* lookup_audit = nullptr;
		const MultiwayBucketTable* cached_bucket_table = nullptr;
		Street cached_bucket_street = Street::Preflop;
		std::array<std::uint8_t, 5> cached_bucket_board{};
		std::size_t cached_bucket_board_size = 0U;
		std::vector<MultiwayActionDescriptor>* action_menu_scratch = nullptr;
		MultiwayDiscoveryStream* discovery_stream = nullptr;
		bool accepted = true;
	};

	struct MultiwayRootExternalSamplingTraversal::DecisionNodeContext
	{
		const MultiwayPublicStateDescriptor* state = nullptr;
		TraversalContext* traversal = nullptr;
		std::uint32_t decision_depth = 0U;
		std::uint32_t public_chance_depth = 0U;
		PlayerId actor = -1;
		std::size_t action_count = 0U;
		std::uint32_t bucket = 0U;
		std::uint32_t blueprint_bucket = 0U;
		MultiwayInfosetId infoset{};
		bool row_was_admitted = false;
		std::array<Probability, MULTIWAY_MAX_TRAVERSAL_ACTIONS> strategy{};
		std::array<std::optional<games::multiway::MultiwayFixedState>, MULTIWAY_MAX_TRAVERSAL_ACTIONS> successors;
		std::array<Value, MULTIWAY_MAX_TRAVERSAL_ACTIONS> action_values{};
	};

	Value MultiwayRootExternalSamplingTraversal::evaluate_leaf(
		const MultiwayPublicStateDescriptor& state,
		TraversalContext& context) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.evaluate_leaf");
		if (context.discovery_stream != nullptr)
			context.discovery_stream->record_leaf_visit();
		else
			coordinator_->record_leaf_visit();
		if (leaf_evaluator_ == nullptr || !leaf_evaluator_->valid())
		{
			throw std::logic_error("multiway recursive traversal requires a leaf evaluator at its boundary");
		}
		const auto actor = state.betting.current_player >= 0 ? state.betting.current_player : context.traverser;
		const auto hole = context.terminal->sampled_hole(*context.deal, actor);
		std::uint32_t bucket = 0U;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.evaluate_leaf.bucket_lookup");
			bucket = future_bucket_artifact_ != nullptr
				? future_bucket_artifact_->lookup(state.betting.street, state.board, hole)
				: buckets_->lookup(state.betting.street, state.board, hole);
		}
		const MultiwayContinuationSelectionKey continuation_key = {
			state.id,
			actor,
			state.betting.street,
			bucket,
			root_->action_abstraction_version,
			root_->leaf_model_version,
		};
		MultiwaySearchProfileScope profile_scope(
			context.profile, MultiwaySearchProfileStage::ContinuationLeaf);
		const MultiwayLeafEvaluationRequest request = {
			&state.betting,
			&state.board,
			context.traverser,
			state.id,
			actor,
			bucket,
			root_->action_abstraction_version,
			root_->leaf_model_version,
			MultiwayContinuationPolicyKind::Blueprint,
			context.deal,
			context.terminal,
			context.player_reaches.data(),
			context.player_count,
			range_context_identity(
				range_model_identity_, context.player_reaches.data(), context.player_count),
			private_deal_identity(*context.terminal, *context.deal, context.player_count),
		};
		if (continuation_selector_ == nullptr)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.evaluate_leaf.model");
			const auto value = (*leaf_evaluator_)(request);
			if (!std::isfinite(value))
			{
				throw std::logic_error("multiway leaf evaluator returned a non-finite value");
			}
			return value;
		}
		TEXASSOLVER_TRACY_ZONE_NAMED(tracy_continuation_zone, "mw.worker.evaluate_leaf.continuation", true);
		const auto mixture = continuation_selector_->strategy(continuation_key);
		std::array<Value, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
		Value value = 0.0;
		for (std::size_t index = 0U; index < values.size(); ++index)
		{
			auto policy_request = request;
			policy_request.continuation_policy = MULTIWAY_FIXED_CONTINUATION_POLICIES[index];
			values[index] = (*leaf_evaluator_)(policy_request);
			if (!std::isfinite(values[index]))
			{
				throw std::logic_error("multiway leaf evaluator returned a non-finite value");
			}
			value += mixture[index] * values[index];
		}
		if (request.continuation_actor != context.traverser)
			return value;
		const auto sampling_reach = context.private_sampling_reach * context.public_sampling_reach;
		Probability counterfactual_reach = context.private_chance_reach * context.public_chance_reach;
		for (std::size_t player = 0U; player < context.player_count; ++player)
		{
			if (static_cast<PlayerId>(player) != context.traverser)
			{
				counterfactual_reach *= context.player_reaches[player];
			}
		}
		const auto importance_weight = counterfactual_reach / sampling_reach;
		if (!std::isfinite(importance_weight) || importance_weight < 0.0)
		{
			throw std::overflow_error("multiway continuation importance weight is non-finite");
		}
		if (context.continuation_stream != nullptr)
		{
			if (!context.continuation_stream->try_append({ continuation_key, mixture, values, context.trajectory_id,
					context.continuation_sequence++, importance_weight }))
			{
				context.accepted = false;
				return 0.0;
			}
		}
		else
		{
			continuation_selector_->update_regrets_weighted(continuation_key, mixture, values, importance_weight);
		}
		if (!std::isfinite(value))
		{
			throw std::logic_error("multiway leaf evaluator returned a non-finite value");
		}
		return value;
	}

	bool MultiwayRootExternalSamplingTraversal::prepare_decision_row(DecisionNodeContext& decision) const
	{
		auto& context = *decision.traversal;
		const auto& state = *decision.state;
		if (!context.accepted)
			return false;
		const auto actor = state.betting.current_player;
		if (actor < 0 || state.legal_actions.empty())
		{
			throw std::logic_error("multiway recursive traversal requires a decision state");
		}
		const auto action_count = state.legal_actions.size();
		if (action_count > MULTIWAY_MAX_TRAVERSAL_ACTIONS)
		{
			throw std::length_error("multiway traversal action menu exceeds the compact traversal limit");
		}
		const MultiwayBucketTable* table_ptr = nullptr;
		std::uint32_t bucket = 0U;
		std::uint32_t blueprint_bucket = 0U;
		{
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::RowLookup);
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_decision.bucket_lookup");
			const auto same_root_street = state.betting.street == root_->public_state.betting.street;
			const auto sampled_hole = context.terminal->sampled_hole(*context.deal, actor);
			if (state.betting.street == Street::Preflop)
			{
				blueprint_bucket = preflop_class(sampled_hole);
			}
			else
			{
				const bool cache_hit = context.cached_bucket_table != nullptr && context.cached_bucket_street == state.betting.street && context.cached_bucket_board_size == state.board.size() && std::equal(state.board.begin(), state.board.end(), context.cached_bucket_board.begin());
				if (cache_hit)
				{
					table_ptr = context.cached_bucket_table;
				}
				else
				{
					table_ptr = &buckets_->table(state.betting.street, state.board);
					context.cached_bucket_table = table_ptr;
					context.cached_bucket_street = state.betting.street;
					context.cached_bucket_board_size = state.board.size();
					std::copy(state.board.begin(), state.board.end(), context.cached_bucket_board.begin());
				}
				blueprint_bucket = table_ptr->lookup(sampled_hole);
			}
			bucket = root_->root_uses_exact_private_hand && same_root_street
				? static_cast<std::uint32_t>(MultiwayBucketTable::hole_index(
					  sampled_hole))
				: (state.id == root_->public_state.id
						  ? root_->root_bucket
						  : blueprint_bucket);
		}
		const auto bucket_count = state.betting.street == Street::Preflop
			? 169U
			: table_ptr->bucket_count();
		const MultiwayInfosetId infoset = { state.id, actor };
		const MultiwaySparseRowShape row_shape{
			infoset,
			root_->root_uses_exact_private_hand && state.betting.street == root_->public_state.betting.street
				? static_cast<std::uint32_t>(MULTIWAY_HOLE_COMBINATION_COUNT)
				: bucket_count,
			static_cast<std::uint8_t>(action_count),
		};
		const bool row_was_admitted = context.discovery_stream != nullptr
			? coordinator_->has_infoset_row_epoch(infoset)
			: coordinator_->has_infoset_row(infoset);
		if (!row_was_admitted && context.discovery_stream != nullptr)
		{
			if (!context.discovery_stream->try_append_row(row_shape))
			{
				context.accepted = false;
				return false;
			}
		}
		else if (!row_was_admitted)
		{
			MultiwaySearchProfileScope profile_scope(context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
			coordinator_->admit_infoset_row(row_shape);
		}
		decision.actor = actor;
		decision.action_count = action_count;
		decision.bucket = bucket;
		decision.blueprint_bucket = blueprint_bucket;
		decision.infoset = infoset;
		decision.row_was_admitted = row_was_admitted;
		return true;
	}

	void MultiwayRootExternalSamplingTraversal::select_decision_strategy(DecisionNodeContext& decision) const
	{
		auto& context = *decision.traversal;
		const auto& state = *decision.state;
		const auto actor = decision.actor;
		const auto action_count = decision.action_count;
		const auto bucket = decision.bucket;
		const auto blueprint_bucket = decision.blueprint_bucket;
		const auto infoset = decision.infoset;
		const auto row_was_admitted = decision.row_was_admitted;
		auto& strategy = decision.strategy;
		const auto lookup = !row_was_admitted || actor == context.traverser || blueprint_policy_ == nullptr
			? MultiwayBlueprintLookupStatus::Missing
			: blueprint_policy_->strategy_into(
				  infoset, blueprint_bucket, state.legal_actions.data(), action_count, strategy.data());
		if (context.lookup_audit != nullptr && blueprint_policy_ != nullptr && actor != context.traverser)
		{
			const auto audited_lookup = row_was_admitted
				? lookup
				: blueprint_policy_->strategy_into(
					  infoset, blueprint_bucket, state.legal_actions.data(), action_count, strategy.data());
			context.lookup_audit->record(audited_lookup, infoset, blueprint_bucket,
				state.legal_actions.front().action_menu_id, context.trajectory_id, &state);
		}
		if (lookup != MultiwayBlueprintLookupStatus::Hit && row_was_admitted)
		{
			if (blueprint_policy_ != nullptr && actor != context.traverser)
			{
				if (context.discovery_stream != nullptr)
					context.discovery_stream->record_missing_lookup();
				else
					coordinator_->record_missing_lookup();
			}
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::RegretMatching);
			if (context.discovery_stream != nullptr)
				coordinator_->regret_matched_strategy_into_epoch(
					infoset, bucket, strategy.data(), action_count);
			else
				coordinator_->regret_matched_strategy_into(
					infoset, bucket, strategy.data(), action_count);
		}
		if (!row_was_admitted)
			std::fill(strategy.begin(), strategy.begin() + static_cast<std::ptrdiff_t>(action_count), 1.0 / static_cast<double>(action_count));
	}

	void MultiwayRootExternalSamplingTraversal::build_decision_successors(DecisionNodeContext& decision) const
	{
		const auto& state = *decision.state;
		const auto action_count = decision.action_count;
		const auto betting_state = make_multiway_fixed_state(state.betting);
		auto& successors = decision.successors;
		for (std::size_t action = 0U; action < action_count; ++action)
		{
			successors[action].emplace(betting_state.apply(
				state.legal_actions[action].action,
				state.legal_actions[action].target_street_contribution));
		}
	}

	void MultiwayRootExternalSamplingTraversal::adjust_decision_strategy_for_pruning(DecisionNodeContext& decision) const
	{
		auto& context = *decision.traversal;
		const auto& state = *decision.state;
		const auto actor = decision.actor;
		const auto action_count = decision.action_count;
		const auto bucket = decision.bucket;
		const auto infoset = decision.infoset;
		auto& strategy = decision.strategy;
		const auto& successors = decision.successors;
		const bool pruning_recovery = pruning_.recovery_batch(context.batch_number);
		if (pruning_.enabled && context.batch_number > pruning_.warmup_batches && !pruning_recovery && actor != context.traverser && state.betting.street != Street::River && action_count > 0U)
		{
			const auto explore = pruning_.exploration_probability;
			std::array<bool, MULTIWAY_MAX_TRAVERSAL_ACTIONS> below_regret_by_action{};
			if (context.discovery_stream != nullptr)
				coordinator_->actions_below_regret_epoch(
					infoset, bucket, pruning_.regret_threshold,
					below_regret_by_action.data(), action_count);
			else
				coordinator_->actions_below_regret(
					infoset, bucket, pruning_.regret_threshold,
					below_regret_by_action.data(), action_count);
			for (std::size_t action = 0; action < action_count; ++action)
			{
				const auto& next = *successors[action];
				const bool immediate_terminal = next.next_node_kind() == MultiwayNextNodeKind::FoldTerminal || next.next_node_kind() == MultiwayNextNodeKind::ShowdownTerminal;
				const bool below_regret = below_regret_by_action[action];
				const bool random_recovery = !immediate_terminal && !below_regret && (static_cast<double>(next_random(context.random_state) % 1000000U) / 1000000.0) < explore;
				if (pruning_.should_explore_action(context.batch_number,
						below_regret || strategy[action] <= pruning_.action_probability_threshold || random_recovery,
						immediate_terminal, state.betting.street == Street::River))
				{
					strategy[action] = explore / action_count;
				}
				else if (!immediate_terminal)
				{
					strategy[action] *= (1.0 - explore);
				}
			}
			Probability total = 0.0;
			for (std::size_t action = 0; action < action_count; ++action)
				total += strategy[action];
			if (total > 0.0)
				for (std::size_t action = 0; action < action_count; ++action)
					strategy[action] /= total;
		}
	}

Value MultiwayRootExternalSamplingTraversal::evaluate_decision_action(
		DecisionNodeContext& decision, std::size_t action) const
	{
		const auto& state = *decision.state;
		auto& context = *decision.traversal;
		const auto decision_depth = decision.decision_depth;
		const auto public_chance_depth = decision.public_chance_depth;
		const auto& successors = decision.successors;
		const auto& next = *successors[action];
		std::array<MultiwayActionDescriptor, MULTIWAY_MAX_TRAVERSAL_ACTIONS> child_actions{};
		std::size_t child_action_count = 0U;
		if (next.current_player >= 0)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_decision.action_menu_buffer");
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::ActionMenuGeneration);
			std::vector<MultiwayActionDescriptor> local_action_menu;
			auto& action_menu = context.action_menu_scratch != nullptr
				? *context.action_menu_scratch
				: local_action_menu;
			action_abstraction_->make_legal_actions_into(
				make_multiway_betting_snapshot(next), {}, action_menu);
			if (action_menu.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS)
			{
				throw std::length_error(
					"multiway generated action menu exceeds the compact traversal limit");
			}
			child_action_count = action_menu.size();
			std::copy(action_menu.begin(), action_menu.end(), child_actions.begin());
		}
		MultiwayPublicStateDescriptor child;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_decision.public_descriptor_build");
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
			child = MultiwayPublicBuilder::make_action_child(
				state,
				static_cast<std::uint32_t>(action),
				make_multiway_betting_snapshot(next),
				child_actions.data(),
				child_action_count);
			if (context.discovery_stream != nullptr)
			{
				if (!context.discovery_stream->try_append_public_state(child))
					context.accepted = false;
			}
			else
				coordinator_->admit_public_state(child);
		}
		if (next.next_node_kind() == MultiwayNextNodeKind::FoldTerminal || next.next_node_kind() == MultiwayNextNodeKind::ShowdownTerminal)
		{
			if (context.discovery_stream != nullptr)
				context.discovery_stream->record_terminal_visit();
			else
				coordinator_->record_terminal_visit();
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::TerminalSettlement);
			return context.terminal->resolve_admitted_terminal_value(
				child, *context.deal, context.traverser);
		}
		const auto next_depth = decision_depth + 1U;
		if (next.current_player >= 0 && next.street == state.betting.street && next_depth < max_decision_depth_)
		{
			return traverse_decision(child, next_depth, public_chance_depth, context);
		}
		if (next.next_node_kind() == MultiwayNextNodeKind::BoardRunout)
		{
			return traverse_public_chance(child, next_depth, public_chance_depth, context);
		}
		if (next.next_node_kind() == MultiwayNextNodeKind::StreetTransition && public_chance_depth < max_public_chance_depth_)
		{
			return traverse_public_chance(child, next_depth, public_chance_depth, context);
		}
		return evaluate_leaf(child, context);
	}

	Value MultiwayRootExternalSamplingTraversal::evaluate_decision_actions(DecisionNodeContext& decision) const
	{
		auto& context = *decision.traversal;
		const auto actor = decision.actor;
		const auto action_count = decision.action_count;
		auto& strategy = decision.strategy;
		auto& action_values = decision.action_values;
		if (actor != context.traverser)
		{
			const auto action = sample_action(strategy.data(), action_count, context.random_state);
			const auto probability = strategy[action];
			if (probability <= 0.0)
			{
				throw std::logic_error("multiway traversal sampled a zero-probability action");
			}
			auto& reach = context.player_reaches[static_cast<std::size_t>(actor)];
			const auto saved_reach = reach;
			const auto saved_sampling = context.public_sampling_reach;
			reach *= probability;
			context.public_sampling_reach *= probability;
			const auto value = evaluate_decision_action(decision, action);
			reach = saved_reach;
			context.public_sampling_reach = saved_sampling;
			return value;
		}

		auto& traverser_reach = context.player_reaches[static_cast<std::size_t>(actor)];
		const auto saved_reach = traverser_reach;
		for (std::size_t action = 0; action < action_count; ++action)
		{
			traverser_reach = saved_reach * strategy[action];
			action_values[action] = evaluate_decision_action(decision, action);
			if (!context.accepted)
				break;
		}
		traverser_reach = saved_reach;
		if (!context.accepted)
			return 0.0;

		return emit_decision_deltas(decision);
	}

	Value MultiwayRootExternalSamplingTraversal::emit_decision_deltas(DecisionNodeContext& decision) const
	{
		auto& context = *decision.traversal;
		const auto actor = decision.actor;
		const auto action_count = decision.action_count;
		const auto bucket = decision.bucket;
		const auto infoset = decision.infoset;
		auto& strategy = decision.strategy;
		auto& action_values = decision.action_values;
		const auto sampling_reach = context.private_sampling_reach * context.public_sampling_reach;
		if (!std::isfinite(sampling_reach) || sampling_reach <= 0.0)
		{
			throw std::overflow_error("multiway traversal sampling reach is non-finite");
		}
		Value node_value = 0.0;
		context.accepted = append_infoset_update_noalloc(
			*context.stream,
			infoset,
			bucket,
			context.trajectory_id,
			context.player_reaches.data(),
			context.player_count,
			actor,
			context.private_chance_reach * context.public_chance_reach,
			sampling_reach,
			strategy.data(),
			action_values.data(),
			action_count,
			context.iteration_weight,
			node_value);
		if (!context.accepted)
			return 0.0;
		return node_value;
	}

	Value MultiwayRootExternalSamplingTraversal::traverse_decision(
		const MultiwayPublicStateDescriptor& state,
		std::uint32_t decision_depth,
		std::uint32_t public_chance_depth,
		TraversalContext& context) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_decision");
		if (!context.accepted)
			return 0.0;
		DecisionNodeContext decision;
		decision.state = &state;
		decision.traversal = &context;
		decision.decision_depth = decision_depth;
		decision.public_chance_depth = public_chance_depth;
		if (!prepare_decision_row(decision))
			return 0.0;
		select_decision_strategy(decision);
		build_decision_successors(decision);
		adjust_decision_strategy_for_pruning(decision);
		return evaluate_decision_actions(decision);
	}

	Value MultiwayRootExternalSamplingTraversal::traverse_public_chance(
		const MultiwayPublicStateDescriptor& state,
		std::uint32_t decision_depth,
		std::uint32_t public_chance_depth,
		TraversalContext& context) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_public_chance");
		MultiwaySampledPublicBoardChance sampled;
		{
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::PublicChanceSampling);
			sampled = context.terminal->sample_admitted_public_board_chance(
				state, *context.deal, context.random_state);
		}
		MultiwayPublicStateDescriptor chance_child;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_public_chance.public_descriptor_build");
			MultiwaySearchProfileScope profile_scope(
				context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
			chance_child = MultiwayPublicBuilder::make_board_chance_child(
				state, sampled, {});
			if (context.discovery_stream != nullptr)
			{
				if (!context.discovery_stream->try_append_public_state(chance_child))
					context.accepted = false;
			}
			else
				coordinator_->admit_public_state(chance_child);
		}

		const auto saved_chance_reach = context.public_chance_reach;
		const auto saved_sampling_reach = context.public_sampling_reach;
		context.public_chance_reach *= sampled.probability;
		context.public_sampling_reach *= sampled.probability;
		const auto next_chance_depth = public_chance_depth + 1U;

		Value value = 0.0;
		if (chance_child.board_runout.chance_only_runout)
		{
			if (chance_child.board.size() == 5U)
			{
				if (context.discovery_stream != nullptr)
					context.discovery_stream->record_terminal_visit();
				else
					coordinator_->record_terminal_visit();
				MultiwaySearchProfileScope profile_scope(
					context.profile, MultiwaySearchProfileStage::TerminalSettlement);
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_public_chance.terminal");
				value = context.terminal->resolve_admitted_terminal_value(
					chance_child, *context.deal, context.traverser);
			}
			else
			{
				value = traverse_public_chance(
					chance_child, decision_depth, next_chance_depth, context);
			}
		}
		else
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_public_chance.street_transition");
			const auto transition =
				context.terminal->apply_admitted_public_street_transition(chance_child);
			auto next_actions = [&] {
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traverse_public_chance.action_menu");
				MultiwaySearchProfileScope profile_scope(
					context.profile, MultiwaySearchProfileStage::ActionMenuGeneration);
				return action_abstraction_->make_legal_actions(
					transition.transition.betting);
			}();
			if (next_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS)
			{
				throw std::length_error(
					"multiway generated action menu exceeds the compact traversal limit");
			}
			const auto transition_child = [&] {
				MultiwaySearchProfileScope profile_scope(
					context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
				auto child = MultiwayPublicBuilder::make_street_transition_child(
					chance_child, transition, std::move(next_actions));
				if (context.discovery_stream != nullptr)
				{
					if (!context.discovery_stream->try_append_public_state(child))
						context.accepted = false;
				}
				else
					coordinator_->admit_public_state(child);
				return child;
			}();
			if (decision_depth < max_decision_depth_)
			{
				value = traverse_decision(
					transition_child, decision_depth, next_chance_depth, context);
			}
			else
			{
				value = evaluate_leaf(transition_child, context);
			}
		}

		context.public_chance_reach = saved_chance_reach;
		context.public_sampling_reach = saved_sampling_reach;
		return value;
	}

	bool MultiwayRootExternalSamplingTraversal::run(
		PlayerId traverser,
		std::uint64_t trajectory_id,
		std::uint64_t seed,
		MultiwayWorkerDeltaStream& stream,
		double iteration_weight,
		MultiwaySearchProfile* profile,
		MultiwayContinuationDeltaStream* continuation_stream,
		std::uint64_t batch_number,
		MultiwayBlueprintLookupAudit* lookup_audit,
		std::vector<MultiwayActionDescriptor>* action_menu_scratch,
		MultiwayDiscoveryStream* discovery_stream) const
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.trajectory_run");
		const auto& root_state = root_->public_state;
		if (std::find(root_->seat_order.begin(), root_->seat_order.end(), traverser) == root_->seat_order.end() || root_state.legal_actions.empty())
		{
			throw std::invalid_argument("multiway root traversal requires a valid root seat traverser");
		}
		const auto deal = [&] {
			MultiwaySearchProfileScope profile_scope(
				profile, MultiwaySearchProfileStage::PrivateDealSampling);
			return terminal_.sample_private_deal(seed);
		}();
		if (!deal.valid())
			return false;
		const auto sampled_reach = terminal_.sampled_reach(deal);
		TraversalContext context;
		context.terminal = &terminal_;
		context.deal = &deal;
		context.stream = &stream;
		context.player_reaches.fill(1.0);
		context.player_count = root_state.betting.stacks.size();
		context.traverser = traverser;
		context.trajectory_id = trajectory_id;
		context.random_state = seed;
		context.private_chance_reach = sampled_reach.chance_reach;
		context.private_sampling_reach = sampled_reach.proposal_reach;
		context.iteration_weight = iteration_weight;
		context.profile = profile;
		context.lookup_audit = lookup_audit;
		context.action_menu_scratch = action_menu_scratch;
		context.discovery_stream = discovery_stream;
		const auto initial_size = stream.size();
		const auto initial_continuation_size = continuation_stream == nullptr ? 0U : continuation_stream->size();
		const auto initial_discovery_public_state_count = discovery_stream == nullptr ? 0U : discovery_stream->public_state_count();
		const auto initial_discovery_row_count = discovery_stream == nullptr ? 0U : discovery_stream->row_count();
		const auto initial_discovery_terminal_visits = discovery_stream == nullptr ? 0U : discovery_stream->terminal_visits();
		const auto initial_discovery_leaf_visits = discovery_stream == nullptr ? 0U : discovery_stream->leaf_visits();
		const auto initial_discovery_missing_lookup_requests = discovery_stream == nullptr ? 0U : discovery_stream->missing_lookup_requests();
		context.continuation_stream = continuation_stream;
		context.batch_number = batch_number;
		(void)traverse_decision(root_state, 0U, 0U, context);
		if (!context.accepted)
		{
			stream.rewind(initial_size);
			if (continuation_stream != nullptr)
				continuation_stream->rewind(initial_continuation_size);
			if (discovery_stream != nullptr)
				discovery_stream->rewind_to(
					initial_discovery_public_state_count,
					initial_discovery_row_count,
					initial_discovery_terminal_visits,
					initial_discovery_leaf_visits,
					initial_discovery_missing_lookup_requests);
		}
		return context.accepted;
	}
}
