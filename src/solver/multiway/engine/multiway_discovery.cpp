#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{
	namespace
	{
#pragma pack(push, 8)
		struct DiscoveryMergeDiagnosticsDelta
		{
			std::uint64_t public_state_merge_nanoseconds = 0U;
			std::uint64_t row_merge_nanoseconds = 0U;
			std::uint64_t public_state_source_elements = 0U;
			std::uint64_t row_source_elements = 0U;
			std::uint64_t public_descriptor_copy_count = 0U;
			std::uint64_t public_descriptor_copy_bytes = 0U;
			std::uint64_t nested_descriptor_copy_count = 0U;
			std::uint64_t nested_descriptor_copy_bytes = 0U;
			std::uint64_t output_capacity_reuse_events = 0U;
			std::uint64_t output_capacity_reuse_bytes = 0U;
			std::uint64_t public_state_duplicate_candidates = 0U;
			std::uint64_t row_duplicate_candidates = 0U;
		};
#pragma pack(pop)

		[[nodiscard]] bool action_descriptor_less(
			const MultiwayActionDescriptor& left,
			const MultiwayActionDescriptor& right) noexcept
		{
			if (left.action != right.action)
				return static_cast<std::uint8_t>(left.action) < static_cast<std::uint8_t>(right.action);
			if (left.action_index != right.action_index)
				return left.action_index < right.action_index;
			if (left.target_street_contribution != right.target_street_contribution)
				return left.target_street_contribution < right.target_street_contribution;
			return left.action_menu_id < right.action_menu_id;
		}

		[[nodiscard]] bool public_state_candidate_less(
			const MultiwayPublicStateDescriptor& left,
			const MultiwayPublicStateDescriptor& right) noexcept
		{
			if (left.id != right.id)
				return left.id < right.id;
			if (left.parent_id != right.parent_id)
				return left.parent_id < right.parent_id;
			if (left.incoming_edge.kind != right.incoming_edge.kind)
				return static_cast<std::uint8_t>(left.incoming_edge.kind) < static_cast<std::uint8_t>(right.incoming_edge.kind);
			if (action_descriptor_less(left.incoming_edge.action, right.incoming_edge.action))
				return true;
			if (action_descriptor_less(right.incoming_edge.action, left.incoming_edge.action))
				return false;
			if (left.incoming_edge.dealt_card != right.incoming_edge.dealt_card)
				return left.incoming_edge.dealt_card < right.incoming_edge.dealt_card;
			if (left.incoming_edge.dealt_cards != right.incoming_edge.dealt_cards)
				return std::lexicographical_compare(
					left.incoming_edge.dealt_cards.begin(), left.incoming_edge.dealt_cards.end(),
					right.incoming_edge.dealt_cards.begin(), right.incoming_edge.dealt_cards.end());
			if (left.incoming_edge.transition_board != right.incoming_edge.transition_board)
				return std::lexicographical_compare(
					left.incoming_edge.transition_board.begin(), left.incoming_edge.transition_board.end(),
					right.incoming_edge.transition_board.begin(), right.incoming_edge.transition_board.end());
			return false;
		}

		[[nodiscard]] bool row_shape_less(
			const MultiwaySparseRowShape& left,
			const MultiwaySparseRowShape& right) noexcept
		{
			if (left.infoset != right.infoset)
				return left.infoset < right.infoset;
			if (left.bucket_count != right.bucket_count)
				return left.bucket_count < right.bucket_count;
			return left.action_count < right.action_count;
		}

		void record_discovery_descriptor_copy(
			const MultiwayPublicStateDescriptor& state,
			DiscoveryMergeDiagnosticsDelta& diagnostics) noexcept
		{
			++diagnostics.public_descriptor_copy_count;
			diagnostics.public_descriptor_copy_bytes += sizeof(MultiwayPublicStateDescriptor);
			const auto record = [&diagnostics](std::size_t count, std::size_t bytes) {
				diagnostics.nested_descriptor_copy_count += static_cast<std::uint64_t>(count);
				diagnostics.nested_descriptor_copy_bytes += static_cast<std::uint64_t>(count * bytes);
			};
			record(state.board.size(), sizeof(std::uint8_t));
			record(state.incoming_edge.dealt_cards.size(), sizeof(std::uint8_t));
			record(state.incoming_edge.transition_board.size(), sizeof(std::uint8_t));
			record(state.betting.stacks.size(), sizeof(int));
			record(state.betting.contributions.size(), sizeof(int));
			record(state.betting.street_contributions.size(), sizeof(int));
			record(state.betting.folded.size(), sizeof(std::uint8_t));
			record(state.betting.all_in.size(), sizeof(std::uint8_t));
			record(state.betting.may_raise.size(), sizeof(std::uint8_t));
			record(state.betting.pending.size(), sizeof(std::uint8_t));
			record(state.betting.has_acted.size(), sizeof(std::uint8_t));
			record(state.betting.bet_faced_when_acted.size(), sizeof(int));
			record(state.history.size(), sizeof(MultiwayPublicHistoryEntry));
			record(state.legal_actions.size(), sizeof(MultiwayActionDescriptor));
		}

		void merge_public_state_streams_reference(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwayPublicStateDescriptor>& output,
			std::vector<std::size_t>& cursors,
			DiscoveryMergeDiagnosticsDelta& diagnostics)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states");
			const auto merge_started = std::chrono::steady_clock::now();
			if (streams.size() == 1U)
			{
				const auto& stream = *streams.front();
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.single_stream");
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.single_stream.capacity");
					if (output.capacity() >= stream.public_state_count())
					{
						++diagnostics.output_capacity_reuse_events;
						diagnostics.output_capacity_reuse_bytes += static_cast<std::uint64_t>(stream.public_state_count() * sizeof(MultiwayPublicStateDescriptor));
					}
				}
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.single_stream.telemetry");
					for (const auto& state : stream.public_states())
						record_discovery_descriptor_copy(state, diagnostics);
				}
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.single_stream.copy");
					output.assign(stream.public_states().begin(), stream.public_states().end());
				}
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.single_stream.cursor");
					cursors.assign(1U, stream.public_state_count());
				}
				diagnostics.public_state_merge_nanoseconds = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - merge_started)
						.count());
				diagnostics.public_state_source_elements = static_cast<std::uint64_t>(stream.public_state_count());
				diagnostics.public_state_duplicate_candidates = 0U;
				return;
			}
			output.clear();
			std::size_t total = 0U;
			for (const auto* stream : streams)
				total += stream->public_state_count();
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.prepare_output");
				if (output.capacity() >= total)
				{
					++diagnostics.output_capacity_reuse_events;
					diagnostics.output_capacity_reuse_bytes += static_cast<std::uint64_t>(total * sizeof(MultiwayPublicStateDescriptor));
				}
				output.reserve(total);
			}
			cursors.assign(streams.size(), 0U);
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.multi_stream");
				while (true)
				{
					std::size_t first_stream = streams.size();
					for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
					{
						if (cursors[stream_index] == streams[stream_index]->public_state_count())
							continue;
						if (first_stream == streams.size() || streams[stream_index]->public_states()[cursors[stream_index]].id < streams[first_stream]->public_states()[cursors[first_stream]].id)
							first_stream = stream_index;
					}
					if (first_stream == streams.size())
						break;
					const auto id = streams[first_stream]->public_states()[cursors[first_stream]].id;
					const auto& first_candidate = streams[first_stream]->public_states()[cursors[first_stream]];
					record_discovery_descriptor_copy(first_candidate, diagnostics);
					output.push_back(first_candidate);
					for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
					{
						const auto& stream = *streams[stream_index];
						if (cursors[stream_index] == stream.public_state_count() || stream.public_states()[cursors[stream_index]].id != id)
							continue;
						const auto& candidate = stream.public_states()[cursors[stream_index]];
						if (!MultiwaySolverCoordinator::public_state_candidates_compatible(output.back(), candidate))
							throw std::invalid_argument("multiway discovery merge found conflicting public-state descriptors");
						if (public_state_candidate_less(candidate, output.back()))
						{
							record_discovery_descriptor_copy(candidate, diagnostics);
							output.back() = candidate;
						}
						++cursors[stream_index];
					}
				}
			}
			diagnostics.public_state_merge_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - merge_started)
					.count());
			diagnostics.public_state_source_elements = static_cast<std::uint64_t>(total);
			diagnostics.public_state_duplicate_candidates = static_cast<std::uint64_t>(total - output.size());
		}

		void merge_row_streams_reference(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwaySparseRowShape>& output,
			std::vector<std::size_t>& cursors,
			DiscoveryMergeDiagnosticsDelta& diagnostics)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows");
			const auto merge_started = std::chrono::steady_clock::now();
			if (streams.size() == 1U)
			{
				const auto& stream = *streams.front();
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.single_stream");
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.single_stream.copy");
					output.assign(stream.rows().begin(), stream.rows().end());
				}
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.single_stream.cursor");
					cursors.assign(1U, stream.row_count());
				}
				diagnostics.row_merge_nanoseconds = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - merge_started)
						.count());
				diagnostics.row_source_elements = static_cast<std::uint64_t>(stream.row_count());
				diagnostics.row_duplicate_candidates = 0U;
				return;
			}
			output.clear();
			std::size_t total = 0U;
			for (const auto* stream : streams)
				total += stream->row_count();
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.prepare_output");
				output.reserve(total);
			}
			cursors.assign(streams.size(), 0U);
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.multi_stream");
				while (true)
				{
					std::size_t first_stream = streams.size();
					for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
					{
						if (cursors[stream_index] == streams[stream_index]->row_count())
							continue;
						if (first_stream == streams.size() || streams[stream_index]->rows()[cursors[stream_index]].infoset < streams[first_stream]->rows()[cursors[first_stream]].infoset)
							first_stream = stream_index;
					}
					if (first_stream == streams.size())
						break;
					const auto infoset = streams[first_stream]->rows()[cursors[first_stream]].infoset;
					output.push_back(streams[first_stream]->rows()[cursors[first_stream]]);
					for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
					{
						const auto& stream = *streams[stream_index];
						if (cursors[stream_index] == stream.row_count() || stream.rows()[cursors[stream_index]].infoset != infoset)
							continue;
						const auto& candidate = stream.rows()[cursors[stream_index]];
						if (candidate.bucket_count != output.back().bucket_count || candidate.action_count != output.back().action_count)
							throw std::invalid_argument("multiway discovery merge found conflicting row shapes");
						++cursors[stream_index];
					}
				}
			}
			diagnostics.row_merge_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - merge_started)
					.count());
			diagnostics.row_source_elements = static_cast<std::uint64_t>(total);
			diagnostics.row_duplicate_candidates = static_cast<std::uint64_t>(total - output.size());
		}

		template <typename StreamKeyLess>
		void tournament_push(
			std::vector<std::size_t>& tournament,
			std::size_t& active_count,
			std::size_t stream_index,
			StreamKeyLess less)
		{
			tournament[active_count] = stream_index;
			std::size_t current = active_count;
			++active_count;
			while (current != 0U)
			{
				const auto parent = (current - 1U) / 2U;
				if (!less(tournament[current], tournament[parent]))
					break;
				std::swap(tournament[current], tournament[parent]);
				current = parent;
			}
		}

		template <typename StreamKeyLess>
		void tournament_advance(
			std::vector<std::size_t>& tournament,
			std::size_t& active_count,
			bool exhausted,
			StreamKeyLess less)
		{
			if (exhausted)
			{
				--active_count;
				if (active_count == 0U)
					return;
				tournament[0U] = tournament[active_count];
			}
			std::size_t current = 0U;
			while (true)
			{
				const auto left = current * 2U + 1U;
				if (left >= active_count)
					break;
				const auto right = left + 1U;
				const auto child = right < active_count && less(tournament[right], tournament[left])
					? right
					: left;
				if (!less(tournament[child], tournament[current]))
					break;
				std::swap(tournament[current], tournament[child]);
				current = child;
			}
		}

		void merge_public_state_streams_tournament(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwayPublicStateDescriptor>& output,
			std::vector<std::size_t>& cursors,
			std::vector<std::size_t>& tournament,
			DiscoveryMergeDiagnosticsDelta& diagnostics)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.public_states.tournament");
			if (streams.size() <= 1U)
			{
				merge_public_state_streams_reference(streams, output, cursors, diagnostics);
				return;
			}
			const auto merge_started = std::chrono::steady_clock::now();
			output.clear();
			std::size_t total = 0U;
			for (const auto* stream : streams)
				total += stream->public_state_count();
			if (output.capacity() >= total)
			{
				++diagnostics.output_capacity_reuse_events;
				diagnostics.output_capacity_reuse_bytes += static_cast<std::uint64_t>(total * sizeof(MultiwayPublicStateDescriptor));
			}
			output.reserve(total);
			cursors.resize(streams.size());
			std::fill(cursors.begin(), cursors.end(), 0U);
			std::size_t active_count = 0U;
			const auto less = [&streams, &cursors](std::size_t left, std::size_t right) {
				const auto& left_stream = *streams[left];
				const auto& right_stream = *streams[right];
				const auto& left_state = left_stream.public_states()[cursors[left]];
				const auto& right_state = right_stream.public_states()[cursors[right]];
				return left_state.id != right_state.id ? left_state.id < right_state.id : left < right;
			};
			for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
			{
				if (streams[stream_index]->public_state_count() != 0U)
					tournament_push(tournament, active_count, stream_index, less);
			}
			while (active_count != 0U)
			{
				const auto id = streams[tournament[0U]]->public_states()[cursors[tournament[0U]]].id;
				const auto first_stream = tournament[0U];
				const auto& first_candidate = streams[first_stream]->public_states()[cursors[first_stream]];
				record_discovery_descriptor_copy(first_candidate, diagnostics);
				output.push_back(first_candidate);
				bool first_candidate_in_group = true;
				while (active_count != 0U)
				{
					const auto stream_index = tournament[0U];
					const auto& stream = *streams[stream_index];
					if (stream.public_states()[cursors[stream_index]].id != id)
						break;
					const auto& candidate = stream.public_states()[cursors[stream_index]];
					if (!first_candidate_in_group)
					{
						if (!MultiwaySolverCoordinator::public_state_candidates_compatible(output.back(), candidate))
							throw std::invalid_argument("multiway discovery merge found conflicting public-state descriptors");
						if (public_state_candidate_less(candidate, output.back()))
						{
							record_discovery_descriptor_copy(candidate, diagnostics);
							output.back() = candidate;
						}
					}
					first_candidate_in_group = false;
					++cursors[stream_index];
					tournament_advance(
						tournament, active_count,
						cursors[stream_index] == stream.public_state_count(), less);
				}
			}
			diagnostics.public_state_merge_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - merge_started)
					.count());
			diagnostics.public_state_source_elements = static_cast<std::uint64_t>(total);
			diagnostics.public_state_duplicate_candidates = static_cast<std::uint64_t>(total - output.size());
		}

		void merge_row_streams_tournament(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwaySparseRowShape>& output,
			std::vector<std::size_t>& cursors,
			std::vector<std::size_t>& tournament,
			DiscoveryMergeDiagnosticsDelta& diagnostics)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge.rows.tournament");
			if (streams.size() <= 1U)
			{
				merge_row_streams_reference(streams, output, cursors, diagnostics);
				return;
			}
			const auto merge_started = std::chrono::steady_clock::now();
			output.clear();
			std::size_t total = 0U;
			for (const auto* stream : streams)
				total += stream->row_count();
			output.reserve(total);
			cursors.resize(streams.size());
			std::fill(cursors.begin(), cursors.end(), 0U);
			std::size_t active_count = 0U;
			const auto less = [&streams, &cursors](std::size_t left, std::size_t right) {
				const auto& left_stream = *streams[left];
				const auto& right_stream = *streams[right];
				const auto& left_row = left_stream.rows()[cursors[left]];
				const auto& right_row = right_stream.rows()[cursors[right]];
				return left_row.infoset != right_row.infoset ? left_row.infoset < right_row.infoset : left < right;
			};
			for (std::size_t stream_index = 0U; stream_index < streams.size(); ++stream_index)
			{
				if (streams[stream_index]->row_count() != 0U)
					tournament_push(tournament, active_count, stream_index, less);
			}
			while (active_count != 0U)
			{
				const auto infoset = streams[tournament[0U]]->rows()[cursors[tournament[0U]]].infoset;
				const auto first_stream = tournament[0U];
				output.push_back(streams[first_stream]->rows()[cursors[first_stream]]);
				bool first_candidate_in_group = true;
				while (active_count != 0U)
				{
					const auto stream_index = tournament[0U];
					const auto& stream = *streams[stream_index];
					if (stream.rows()[cursors[stream_index]].infoset != infoset)
						break;
					const auto& candidate = stream.rows()[cursors[stream_index]];
					if (!first_candidate_in_group && (candidate.bucket_count != output.back().bucket_count || candidate.action_count != output.back().action_count))
						throw std::invalid_argument("multiway discovery merge found conflicting row shapes");
					first_candidate_in_group = false;
					++cursors[stream_index];
					tournament_advance(
						tournament, active_count,
						cursors[stream_index] == stream.row_count(), less);
				}
			}
			diagnostics.row_merge_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - merge_started)
					.count());
			diagnostics.row_source_elements = static_cast<std::uint64_t>(total);
			diagnostics.row_duplicate_candidates = static_cast<std::uint64_t>(total - output.size());
		}

		void build_parent_first_order(
			const std::vector<MultiwayPublicStateDescriptor>& candidates,
			const MultiwaySolverCoordinator& coordinator,
			MultiwayAdmissionDiagnostics& diagnostics,
			std::vector<std::size_t>& order,
			std::vector<std::size_t>& parent_indexes,
			std::vector<std::uint32_t>& depths,
			std::vector<std::uint8_t>& colors,
			std::vector<std::size_t>& stack)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.parent_order.build");
			order.resize(candidates.size());
			parent_indexes.assign(candidates.size(), candidates.size());
			depths.assign(candidates.size(), 0U);
			colors.assign(candidates.size(), 0U);
			stack.clear();
			if (!candidates.empty())
				++diagnostics.parent_resolution_passes;
			const auto find_candidate = [&candidates](MultiwayPublicStateId id) {
				const auto it = std::lower_bound(candidates.begin(), candidates.end(), id, [](const auto& candidate, MultiwayPublicStateId value) {
					return candidate.id < value;
				});
				return it == candidates.end() || it->id != id
					? candidates.size()
					: static_cast<std::size_t>(it - candidates.begin());
			};
			for (std::size_t index = 0U; index < candidates.size(); ++index)
			{
				if (colors[index] != 0U)
					continue;
				stack.clear();
				std::size_t current = index;
				while (colors[current] == 0U)
				{
					colors[current] = 1U;
					stack.push_back(current);
					const auto parent_id = candidates[current].parent_id;
					if (parent_id.value == 0U)
					{
						if (candidates[current].id != coordinator.root().public_state.id && !coordinator.has_public_state_epoch(candidates[current].id))
							throw std::invalid_argument("multiway discovery contains a non-root state without a parent");
						depths[current] = 0U;
						colors[current] = 2U;
						break;
					}
					++diagnostics.parent_resolution_probes;
					const auto parent_index = find_candidate(parent_id);
					parent_indexes[current] = parent_index;
					if (parent_index == candidates.size())
					{
						if (!coordinator.has_public_state_epoch(parent_id))
							throw std::invalid_argument("multiway discovery contains an unresolved parent");
						depths[current] = 1U;
						colors[current] = 2U;
						break;
					}
					if (colors[parent_index] == 1U)
						throw std::invalid_argument("multiway discovery contains a parent cycle");
					if (colors[parent_index] == 2U)
					{
						if (depths[parent_index] == std::numeric_limits<std::uint32_t>::max())
							throw std::length_error("multiway discovery parent depth exceeds its limit");
						depths[current] = depths[parent_index] + 1U;
						colors[current] = 2U;
						break;
					}
					current = parent_index;
				}
				for (auto reverse = stack.rbegin(); reverse != stack.rend(); ++reverse)
				{
					const auto candidate_index = *reverse;
					if (colors[candidate_index] != 1U)
						continue;
					const auto parent_index = parent_indexes[candidate_index];
					if (parent_index == candidates.size())
						depths[candidate_index] = 1U;
					else
					{
						if (depths[parent_index] == std::numeric_limits<std::uint32_t>::max())
							throw std::length_error("multiway discovery parent depth exceeds its limit");
						depths[candidate_index] = depths[parent_index] + 1U;
					}
					colors[candidate_index] = 2U;
				}
			}
			for (std::size_t index = 0U; index < order.size(); ++index)
				order[index] = index;
			std::sort(order.begin(), order.end(), [&candidates, &depths](std::size_t left, std::size_t right) {
				if (depths[left] != depths[right])
					return depths[left] < depths[right];
				if (candidates[left].id != candidates[right].id)
					return candidates[left].id < candidates[right].id;
				return left < right;
			});
		}
	} // namespace

	namespace detail
	{
		MultiwayDiscoveryMergePath select_multiway_discovery_merge_path(
			std::size_t stream_count) noexcept
		{
			return stream_count > MULTIWAY_DISCOVERY_TOURNAMENT_STREAM_THRESHOLD
				? MultiwayDiscoveryMergePath::Tournament
				: MultiwayDiscoveryMergePath::Reference;
		}

		void merge_multiway_discovery_streams(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwayPublicStateDescriptor>& public_states,
			std::vector<MultiwaySparseRowShape>& rows,
			std::vector<std::size_t>& cursors,
			MultiwayAdmissionDiagnostics* diagnostics)
		{
			const auto one_stream = streams.size() == 1U;
			DiscoveryMergeDiagnosticsDelta delta;
			merge_public_state_streams_reference(streams, public_states, cursors, delta);
			merge_row_streams_reference(streams, rows, cursors, delta);
			if (diagnostics == nullptr)
				return;
			diagnostics->discovery_public_state_merge_nanoseconds += delta.public_state_merge_nanoseconds;
			diagnostics->discovery_row_merge_nanoseconds += delta.row_merge_nanoseconds;
			diagnostics->discovery_public_state_source_elements += delta.public_state_source_elements;
			diagnostics->discovery_row_source_elements += delta.row_source_elements;
			diagnostics->discovery_public_descriptor_copy_count += delta.public_descriptor_copy_count;
			diagnostics->discovery_public_descriptor_copy_bytes += delta.public_descriptor_copy_bytes;
			diagnostics->discovery_nested_descriptor_copy_count += delta.nested_descriptor_copy_count;
			diagnostics->discovery_nested_descriptor_copy_bytes += delta.nested_descriptor_copy_bytes;
			diagnostics->discovery_output_capacity_reuse_events += delta.output_capacity_reuse_events;
			diagnostics->discovery_output_capacity_reuse_bytes += delta.output_capacity_reuse_bytes;
			diagnostics->discovery_public_state_duplicate_candidates += delta.public_state_duplicate_candidates;
			diagnostics->discovery_row_duplicate_candidates += delta.row_duplicate_candidates;
			if (one_stream)
			{
				++diagnostics->discovery_single_stream_batches;
				++diagnostics->discovery_one_stream_fast_path_uses;
			}
		}

		void merge_multiway_discovery_streams(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwayPublicStateDescriptor>& public_states,
			std::vector<MultiwaySparseRowShape>& rows,
			MultiwayDiscoveryMergeScratch& scratch,
			MultiwayDiscoveryMergePath path,
			MultiwayAdmissionDiagnostics* diagnostics)
		{
			const auto one_stream = streams.size() == 1U;
			if (path == MultiwayDiscoveryMergePath::Automatic)
				path = select_multiway_discovery_merge_path(streams.size());
			if (scratch.cursors.capacity() < streams.size() || (path == MultiwayDiscoveryMergePath::Tournament && scratch.tournament.capacity() < streams.size()))
				throw std::length_error("multiway discovery merge scratch is not preallocated for the stream count");
			scratch.cursors.resize(streams.size());
			if (path == MultiwayDiscoveryMergePath::Tournament)
				scratch.tournament.resize(streams.size());
			DiscoveryMergeDiagnosticsDelta delta;
			if (path == MultiwayDiscoveryMergePath::Tournament)
			{
				merge_public_state_streams_tournament(streams, public_states, scratch.cursors, scratch.tournament, delta);
				merge_row_streams_tournament(streams, rows, scratch.cursors, scratch.tournament, delta);
			}
			else
			{
				merge_public_state_streams_reference(streams, public_states, scratch.cursors, delta);
				merge_row_streams_reference(streams, rows, scratch.cursors, delta);
			}
			if (diagnostics == nullptr)
				return;
			diagnostics->discovery_public_state_merge_nanoseconds += delta.public_state_merge_nanoseconds;
			diagnostics->discovery_row_merge_nanoseconds += delta.row_merge_nanoseconds;
			diagnostics->discovery_public_state_source_elements += delta.public_state_source_elements;
			diagnostics->discovery_row_source_elements += delta.row_source_elements;
			diagnostics->discovery_public_descriptor_copy_count += delta.public_descriptor_copy_count;
			diagnostics->discovery_public_descriptor_copy_bytes += delta.public_descriptor_copy_bytes;
			diagnostics->discovery_nested_descriptor_copy_count += delta.nested_descriptor_copy_count;
			diagnostics->discovery_nested_descriptor_copy_bytes += delta.nested_descriptor_copy_bytes;
			diagnostics->discovery_output_capacity_reuse_events += delta.output_capacity_reuse_events;
			diagnostics->discovery_output_capacity_reuse_bytes += delta.output_capacity_reuse_bytes;
			diagnostics->discovery_public_state_duplicate_candidates += delta.public_state_duplicate_candidates;
			diagnostics->discovery_row_duplicate_candidates += delta.row_duplicate_candidates;
			if (one_stream)
			{
				++diagnostics->discovery_single_stream_batches;
				++diagnostics->discovery_one_stream_fast_path_uses;
			}
		}

		void build_multiway_parent_first_order(
			const std::vector<MultiwayPublicStateDescriptor>& candidates,
			const MultiwaySolverCoordinator& coordinator,
			MultiwayAdmissionDiagnostics& diagnostics,
			MultiwayDiscoveryParentOrderScratch& scratch)
		{
			build_parent_first_order(
				candidates, coordinator, diagnostics, scratch.order, scratch.parent_indexes, scratch.depths,
				scratch.colors, scratch.stack);
		}
	} // namespace detail

	bool MultiwayDiscoveryStream::try_append_public_state(const MultiwayPublicStateDescriptor& state)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.append_public_state");
		if (public_states_.size() >= public_state_capacity_)
		{
			overflowed_ = true;
			public_state_capacity_exhausted_ = true;
			return false;
		}
		public_states_.push_back(state);
		return true;
	}

	bool MultiwayDiscoveryStream::try_append_row(const MultiwaySparseRowShape& shape) noexcept
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.append_row");
		if (rows_.size() >= row_capacity_)
		{
			overflowed_ = true;
			row_capacity_exhausted_ = true;
			return false;
		}
		rows_.push_back(shape);
		return true;
	}

	void MultiwayDiscoveryStream::rewind() noexcept
	{
		public_states_.clear();
		rows_.clear();
		overflowed_ = false;
		public_state_capacity_exhausted_ = false;
		row_capacity_exhausted_ = false;
		terminal_visits_ = 0U;
		leaf_visits_ = 0U;
		missing_lookup_requests_ = 0U;
	}

	void MultiwayDiscoveryStream::rewind_to(
		std::size_t public_state_count,
		std::size_t row_count,
		std::uint64_t terminal_visits,
		std::uint64_t leaf_visits,
		std::uint64_t missing_lookup_requests) noexcept
	{
		public_states_.resize(std::min(public_state_count, public_states_.size()));
		rows_.resize(std::min(row_count, rows_.size()));
		terminal_visits_ = terminal_visits;
		leaf_visits_ = leaf_visits;
		missing_lookup_requests_ = missing_lookup_requests;
	}

	MultiwayMemoryComponent MultiwayDiscoveryStream::memory_snapshot() const noexcept
	{
		MultiwayMemoryComponent result;
		result.add_buffer(public_states_.size(), public_states_.capacity(), sizeof(MultiwayPublicStateDescriptor));
		for (const auto& state : public_states_)
		{
			result.add_buffer(state.incoming_edge.dealt_cards.size(), state.incoming_edge.dealt_cards.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.incoming_edge.transition_board.size(), state.incoming_edge.transition_board.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.board.size(), state.board.capacity(), sizeof(std::uint8_t));
			result.add_buffer(state.history.size(), state.history.capacity(), sizeof(MultiwayPublicHistoryEntry));
			result.add_buffer(state.legal_actions.size(), state.legal_actions.capacity(), sizeof(MultiwayActionDescriptor));
		}
		result.add_buffer(rows_.size(), rows_.capacity(), sizeof(MultiwaySparseRowShape));
		return result;
	}

	void MultiwayDiscoveryStream::sort_fixed_order() noexcept
	{
		std::sort(public_states_.begin(), public_states_.end(), public_state_candidate_less);
		std::sort(rows_.begin(), rows_.end(), row_shape_less);
	}

	void MultiwayDiscoveryStream::prepare_for_merge()
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.prepare_for_merge");
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.sort");
			sort_fixed_order();
		}
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.deduplicate_public_states");
			auto public_state_output = public_states_.begin();
			for (auto candidate = public_states_.begin(); candidate != public_states_.end(); ++candidate)
			{
				if (public_state_output != public_states_.begin() && (public_state_output - 1)->id == candidate->id)
				{
					if (!MultiwaySolverCoordinator::public_state_candidates_compatible(*(public_state_output - 1), *candidate))
						throw std::invalid_argument("multiway worker discovery found conflicting public-state descriptors");
					continue;
				}
				if (public_state_output != candidate)
					*public_state_output = std::move(*candidate);
				++public_state_output;
			}
			public_states_.erase(public_state_output, public_states_.end());
		}

		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.discovery.deduplicate_rows");
			auto row_output = rows_.begin();
			for (auto candidate = rows_.begin(); candidate != rows_.end(); ++candidate)
			{
				if (row_output != rows_.begin() && (row_output - 1)->infoset == candidate->infoset)
				{
					if ((row_output - 1)->bucket_count != candidate->bucket_count || (row_output - 1)->action_count != candidate->action_count)
						throw std::invalid_argument("multiway worker discovery found conflicting row shapes");
					continue;
				}
				if (row_output != candidate)
					*row_output = *candidate;
				++row_output;
			}
			rows_.erase(row_output, rows_.end());
		}
	}
} // namespace texas::solver::multiway
