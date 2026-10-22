#include "games/multiway_state.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

	struct BenchmarkConfig
	{
		std::vector<std::uint32_t> workers{ 1U, 2U, 4U, 8U };
		std::vector<std::uint32_t> batch_sizes{ 1000U };
		std::uint32_t warmup_batches = 1U;
		std::uint32_t timed_batches = 4U;
		std::uint32_t repeats = 5U;
		std::uint64_t seed = 0x77U;
		texas::MultiwaySchedulerMode scheduler_mode = texas::MultiwaySchedulerMode::Fixed;
		std::vector<std::uint32_t> dynamic_chunk_sizes{ texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE };
		bool pin_workers = true;
	};

	struct BenchmarkResult
	{
		std::uint32_t workers = 0U;
		std::uint32_t batch_size = 0U;
		std::uint32_t sample = 0U;
		std::uint64_t trajectories = 0U;
		std::uint64_t elapsed_nanoseconds = 0U;
		std::uint64_t worker_active_nanoseconds = 0U;
		std::uint64_t coordinator_wait_nanoseconds = 0U;
		std::uint64_t coordinator_lock_wait_nanoseconds = 0U;
		std::uint64_t sort_nanoseconds = 0U;
		std::uint64_t merge_nanoseconds = 0U;
		std::uint64_t minimum_worker_trajectories = 0U;
		std::uint64_t maximum_worker_trajectories = 0U;
		std::uint64_t peak_worker_delta_entries = 0U;
		std::uint64_t fingerprint = 0U;

		[[nodiscard]] double trajectories_per_second() const noexcept
		{
			return elapsed_nanoseconds == 0U ? 0.0 : static_cast<double>(trajectories) * 1.0e9 / static_cast<double>(elapsed_nanoseconds);
		}
	};

	constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit)
	{
		return texas::card_to_int(rank, suit);
	}

	const std::vector<std::uint8_t> kBoard = {
		card(2, 0),
		card(7, 1),
		card(9, 2),
	};

	texas::MultiwayBucketRegistry benchmark_buckets(const texas::MultiwayModelIdentity& identity)
	{
		std::vector<texas::MultiwayBucketBoardRequest> requests;
		const auto add_request = [&requests](texas::Street street, std::vector<std::uint8_t> board) {
			if (std::none_of(requests.begin(), requests.end(), [street, &board](const auto& existing) {
					return existing.street == street && existing.canonical_board == board;
				}))
			{
				requests.push_back({ street, std::move(board) });
			}
		};
		add_request(texas::Street::Flop, kBoard);
		for (std::uint8_t turn = 0U; turn < 52U; ++turn)
		{
			if (std::find(kBoard.begin(), kBoard.end(), turn) != kBoard.end())
				continue;
			auto turn_board = kBoard;
			turn_board.push_back(turn);
			std::sort(turn_board.begin(), turn_board.end());
			add_request(texas::Street::Turn, turn_board);
			for (std::uint8_t river = 0U; river < 52U; ++river)
			{
				if (std::find(turn_board.begin(), turn_board.end(), river) != turn_board.end())
					continue;
				auto river_board = turn_board;
				river_board.push_back(river);
				std::sort(river_board.begin(), river_board.end());
				add_request(texas::Street::River, std::move(river_board));
			}
		}
		texas::MultiwayBucketBaselineProfile profile;
		profile.flop_bucket_count = 1024U;
		profile.turn_bucket_count = 1024U;
		profile.river_bucket_count = 1024U;
		return texas::build_multiway_baseline_bucket_registry(identity, requests, profile);
	}

	texas::MultiwayRootSnapshot make_root(const texas::MultiwayActionAbstraction& abstraction)
	{
		texas::MultiwayGameConfig game;
		game.starting_stacks = { 10000, 10000, 10000 };
		game.initial_contributions = { 100, 100, 100 };
		game.initial_street_contributions = { 0, 0, 0 };
		game.first_player = 0;
		game.big_blind = 100;
		game.street = texas::Street::Flop;
		const auto betting = texas::MultiwayState::initial(game).snapshot();
		texas::MultiwayRootSnapshot root;
		root.public_state = texas::MultiwayPublicBuilder::make_root(
			betting, kBoard, abstraction.make_legal_actions(betting));
		root.root_infoset = { root.public_state.id, 0 };
		root.root_bucket = 0U;
		root.seat_order = { 0, 1, 2 };
		root.next_street_first_seat = 0;
		root.odd_chip_first_seat = 0;
		root.private_ranges.board = kBoard;
		root.private_ranges.ranges = {
			{ { { card(14, 0), card(13, 0) }, 1.0 } },
			{ { { card(12, 0), card(11, 0) }, 1.0 } },
			{ { { card(10, 0), card(8, 0) }, 1.0 } },
		};
		root.action_abstraction_version = 1U;
		root.leaf_model_version = 1U;
		return root;
	}

	texas::Value deterministic_leaf(
		const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept
	{
		const auto traverser = static_cast<std::size_t>(request.traverser);
		return static_cast<texas::Value>(
			request.betting->contributions[traverser] - request.betting->current_bet);
	}

	texas::MultiwayModelIdentity training_identity(
		std::uint32_t workers,
		std::uint32_t batch_size,
		std::size_t delta_capacity,
		std::uint64_t seed)
	{
		texas::MultiwayBlueprintTrainingConfig config;
		config.limits.worker_count = workers;
		config.limits.trajectories_per_batch = batch_size;
		config.limits.max_public_states = 100000U;
		config.limits.max_sparse_rows = 100000U;
		config.limits.max_sparse_values = 1000000U;
		config.limits.max_worker_delta_entries = delta_capacity;
		config.limits.max_discovery_entries = delta_capacity * 4U;
		config.limits.max_batches = 16U;
		config.deterministic_seed = seed;
		config.limits.seed = seed;
		return config.identity();
	}

	class BenchmarkFixture
	{
	public:
		BenchmarkFixture(std::uint32_t workers, std::uint32_t batch_size, std::uint64_t seed,
			texas::MultiwaySchedulerMode scheduler_mode, bool pin_workers, std::uint32_t dynamic_chunk_size)
			: delta_capacity_(checked_delta_capacity(batch_size)), root_(make_root(abstraction_)), request_(root_, make_cfr(), make_limits(workers, batch_size, delta_capacity_)), coordinator_(request_), buckets_(benchmark_buckets(bucket_identity())), continuation_selector_(std::make_shared<texas::MultiwayFixedContinuationSelector>(texas::MultiwayContinuationPolicyKind::Blueprint)), evaluator_{ deterministic_leaf, nullptr }, traversal_(coordinator_, request_.root(), abstraction_, buckets_, &evaluator_, 3U, 2U, nullptr, continuation_selector_.get()), runner_(traversal_, coordinator_, workers, delta_capacity_, texas::MultiwaySearchProfileMode::Disabled, pin_workers, true, scheduler_mode, dynamic_chunk_size), trainer_(training_identity(workers, batch_size, delta_capacity_, seed), runner_, coordinator_, {}, seed, workers, 0U) {}

		BenchmarkResult run(
			std::uint32_t workers,
			std::uint32_t batch_size,
			std::uint32_t warmup_batches,
			std::uint32_t timed_batches,
			std::uint32_t sample,
			std::uint64_t seed)
		{
			trainer_.run_batches(warmup_batches, batch_size, seed);
			const auto before = trainer_.status();
			const auto started = std::chrono::steady_clock::now();
			trainer_.run_batches(timed_batches, batch_size, seed);
			const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started)
									 .count();
			const auto& after = trainer_.status();
			return {
				workers,
				batch_size,
				sample,
				static_cast<std::uint64_t>(timed_batches) * batch_size,
				static_cast<std::uint64_t>(elapsed),
				after.worker_active_nanoseconds - before.worker_active_nanoseconds,
				after.coordinator_wait_nanoseconds - before.coordinator_wait_nanoseconds,
				after.coordinator_lock_wait_nanoseconds - before.coordinator_lock_wait_nanoseconds,
				after.delta_sort_nanoseconds - before.delta_sort_nanoseconds,
				after.merge_nanoseconds - before.merge_nanoseconds,
				after.minimum_worker_trajectories,
				after.maximum_worker_trajectories,
				after.peak_worker_delta_entries,
				after.merged_stream_fingerprint,
			};
		}

	private:
		// The depth-three fixture can emit more than four deltas per trajectory.
		// This is a per-worker reserve; the runtime multiplies it for admission.
		static constexpr std::size_t kDeltaCapacityEntriesPerTrajectory = 8U;

		static std::size_t checked_delta_capacity(std::uint32_t batch_size)
		{
			if (batch_size > std::numeric_limits<std::size_t>::max() / kDeltaCapacityEntriesPerTrajectory)
			{
				throw std::overflow_error("batch size overflows worker delta capacity");
			}
			return static_cast<std::size_t>(batch_size) * kDeltaCapacityEntriesPerTrajectory;
		}

		static texas::MultiwayCFRConfig make_cfr()
		{
			texas::MultiwayCFRConfig cfr;
			cfr.player_count = 3U;
			return cfr;
		}

		static texas::MultiwaySolverLimits make_limits(
			std::uint32_t workers,
			std::uint32_t batch_size,
			std::size_t delta_capacity)
		{
			texas::MultiwaySolverLimits limits;
			limits.worker_count = workers;
			limits.trajectories_per_batch = batch_size;
			limits.max_public_states = 100000U;
			limits.max_sparse_rows = 100000U;
			limits.max_sparse_values = 1000000U;
			limits.max_worker_delta_entries = delta_capacity;
			limits.max_discovery_entries = delta_capacity * 4U;
			limits.max_batches = 16U;
			limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
			return limits;
		}

		static texas::MultiwayModelIdentity bucket_identity()
		{
			texas::MultiwayBlueprintConfig config;
			config.player_count = 3U;
			config.flop_bucket_count = 1024U;
			config.turn_bucket_count = 1024U;
			config.river_bucket_count = 1024U;
			return texas::make_multiway_model_identity(config);
		}

		std::size_t delta_capacity_ = 0U;
		texas::MultiwayActionAbstraction abstraction_;
		texas::MultiwayRootSnapshot root_;
		texas::MultiwaySolveRequest request_;
		texas::MultiwaySolverCoordinator coordinator_;
		texas::MultiwayBucketRegistry buckets_;
		std::shared_ptr<texas::MultiwayFixedContinuationSelector> continuation_selector_;
		texas::MultiwayLeafEvaluator evaluator_;
		texas::MultiwayRootExternalSamplingTraversal traversal_;
		texas::MultiwayRootBatchRunner runner_;
		texas::MultiwayBlueprintTrainer trainer_;
	};

	// Tiny batches measure thread startup and affinity overhead, not scaling.
	constexpr std::uint32_t kScalingGateMinimumBatchSize = 1000U;

	bool parse_positive_u32(std::string_view text, std::uint32_t& output)
	{
		try
		{
			const auto value = std::stoull(std::string(text));
			if (value == 0U || value > std::numeric_limits<std::uint32_t>::max())
				return false;
			output = static_cast<std::uint32_t>(value);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool parse_u64(std::string_view text, std::uint64_t& output)
	{
		try
		{
			output = std::stoull(std::string(text));
			return output != 0U;
		}
		catch (...)
		{
			return false;
		}
	}

	bool parse_scheduler(std::string_view text, texas::MultiwaySchedulerMode& output)
	{
		if (text == "fixed")
			output = texas::MultiwaySchedulerMode::Fixed;
		else if (text == "dynamic")
			output = texas::MultiwaySchedulerMode::Dynamic;
		else
			return false;
		return true;
	}

	bool parse_csv(std::string_view text, std::vector<std::uint32_t>& output)
	{
		std::vector<std::uint32_t> parsed;
		std::size_t begin = 0U;
		while (begin <= text.size())
		{
			const auto end = text.find(',', begin);
			const auto token = text.substr(begin,
				end == std::string_view::npos ? text.size() - begin : end - begin);
			std::uint32_t value = 0U;
			if (!parse_positive_u32(token, value))
				return false;
			parsed.push_back(value);
			if (end == std::string_view::npos)
				break;
			begin = end + 1U;
		}
		std::sort(parsed.begin(), parsed.end());
		parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
		output = std::move(parsed);
		return !output.empty();
	}

	BenchmarkConfig parse_args(int argc, char** argv)
	{
		BenchmarkConfig config;
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option == "--help")
			{
				std::cout << "Usage: texas_solver_multiway_training_scaling [options]\n"
						  << "  --workers 1,2,4,8,16\n"
						  << "  --batch-sizes 1000,4000,16000\n"
						  << "  --warmup-batches N\n"
						  << "  --timed-batches N\n"
						  << "  --repeats N\n"
						  << "  --seed N\n"
						  << "  --scheduler fixed|dynamic\n"
				  << "  --dynamic-chunk-sizes 1,2,4,8,16\n"
						  << "  --pin-workers true|false\n";
				std::exit(EXIT_SUCCESS);
			}
			if (index + 1 >= argc)
				throw std::invalid_argument("benchmark option requires a value");
			const std::string_view value(argv[++index]);
			if (option == "--workers")
			{
				if (!parse_csv(value, config.workers))
					throw std::invalid_argument("invalid workers list");
				for (const auto workers : config.workers)
				{
					if (workers > 16U)
						throw std::invalid_argument("workers must be in [1, 16]");
				}
			}
			else if (option == "--batch-sizes")
			{
				if (!parse_csv(value, config.batch_sizes))
					throw std::invalid_argument("invalid batch-size list");
			}
			else if (option == "--warmup-batches")
			{
				if (!parse_positive_u32(value, config.warmup_batches))
					throw std::invalid_argument("invalid warmup batch count");
			}
			else if (option == "--timed-batches")
			{
				if (!parse_positive_u32(value, config.timed_batches))
					throw std::invalid_argument("invalid timed batch count");
			}
			else if (option == "--repeats")
			{
				if (!parse_positive_u32(value, config.repeats))
					throw std::invalid_argument("invalid repeat count");
			}
			else if (option == "--seed")
			{
				if (!parse_u64(value, config.seed))
					throw std::invalid_argument("invalid seed");
			}
			else if (option == "--scheduler")
			{
				if (!parse_scheduler(value, config.scheduler_mode))
					throw std::invalid_argument("invalid scheduler");
			}
			else if (option == "--dynamic-chunk-sizes")
			{
				if (!parse_csv(value, config.dynamic_chunk_sizes))
					throw std::invalid_argument("invalid dynamic chunk-size list");
			}
			else if (option == "--pin-workers")
			{
				if (value == "true" || value == "1")
					config.pin_workers = true;
				else if (value == "false" || value == "0")
					config.pin_workers = false;
				else
					throw std::invalid_argument("invalid pin-worker setting");
			}
			else
			{
				throw std::invalid_argument("unknown benchmark option");
			}
		}
		if (static_cast<std::uint64_t>(config.warmup_batches) + config.timed_batches > 16U)
		{
			throw std::invalid_argument("warmup plus timed batches exceeds benchmark capacity");
		}
		return config;
	}

	double median_throughput(std::vector<BenchmarkResult> results)
	{
		std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
			return left.trajectories_per_second() < right.trajectories_per_second();
		});
		return results[results.size() / 2U].trajectories_per_second();
	}

	void print_sample(const BenchmarkResult& result)
	{
		std::cout << "sample"
				  << " workers=" << result.workers
				  << " batch_size=" << result.batch_size
				  << " repeat=" << result.sample
				  << " trajectories=" << result.trajectories
				  << " elapsed_ns=" << result.elapsed_nanoseconds
				  << " trajectories_per_second=" << std::fixed << std::setprecision(1)
				  << result.trajectories_per_second()
				  << " coordinator_wait_ns=" << result.coordinator_wait_nanoseconds
				  << " coordinator_lock_wait_ns=" << result.coordinator_lock_wait_nanoseconds
				  << " worker_active_ns=" << result.worker_active_nanoseconds
				  << " sort_ns=" << result.sort_nanoseconds
				  << " merge_ns=" << result.merge_nanoseconds
				  << " worker_trajectories_min=" << result.minimum_worker_trajectories
				  << " worker_trajectories_max=" << result.maximum_worker_trajectories
				  << " peak_worker_delta_entries=" << result.peak_worker_delta_entries
				  << " fingerprint=" << result.fingerprint << '\n';
	}

	int run(const BenchmarkConfig& config)
	{
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.train.run");
		std::cout << "fixture=three_player_river_depth_one"
				  << " hardware_concurrency=" << std::thread::hardware_concurrency()
				  << " worker_affinity=" << (config.pin_workers ? "pinned_logical_processor" : "unrestricted")
				  << " scheduler=" << (config.scheduler_mode == texas::MultiwaySchedulerMode::Fixed ? "fixed" : "dynamic")
				  << " dynamic_chunk_sizes=";
		for (std::size_t index = 0U; index < config.dynamic_chunk_sizes.size(); ++index)
		{
			if (index != 0U)
				std::cout << ',';
			std::cout << config.dynamic_chunk_sizes[index];
		}
		std::cout
				  << " pin_workers=" << (config.pin_workers ? "true" : "false")
				  << " checkpoint_output=disabled blueprint_output=disabled"
				  << " warmup_batches=" << config.warmup_batches
				  << " timed_batches=" << config.timed_batches
				  << " repeats=" << config.repeats
				  << " seed=" << config.seed << '\n';
		const auto chunk_sizes = config.scheduler_mode == texas::MultiwaySchedulerMode::Dynamic
			? config.dynamic_chunk_sizes
			: std::vector<std::uint32_t>{ texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE };
		for (const auto batch_size : config.batch_sizes)
		{
			for (const auto dynamic_chunk_size : chunk_sizes)
			{
				std::uint64_t reference_fingerprint = 0U;
				double previous_median = 0.0;
				std::uint32_t previous_workers = 0U;
				for (const auto workers : config.workers)
				{
					std::vector<BenchmarkResult> samples;
					samples.reserve(config.repeats);
					for (std::uint32_t sample = 1U; sample <= config.repeats; ++sample)
					{
						BenchmarkFixture fixture(workers, batch_size, config.seed, config.scheduler_mode, config.pin_workers, dynamic_chunk_size);
						auto result = fixture.run(
							workers, batch_size, config.warmup_batches,
							config.timed_batches, sample, config.seed);
						print_sample(result);
						if (reference_fingerprint == 0U)
							reference_fingerprint = result.fingerprint;
						if (result.fingerprint != reference_fingerprint)
						{
							throw std::logic_error(
								"benchmark worker counts produced different merge fingerprints: expected="
								+ std::to_string(reference_fingerprint) + " actual=" + std::to_string(result.fingerprint));
						}
						samples.push_back(result);
					}
					const auto median = median_throughput(samples);
					std::cout << "summary"
							  << " workers=" << workers
							  << " batch_size=" << batch_size
							  << " dynamic_chunk_size=" << dynamic_chunk_size
							  << " median_trajectories_per_second=" << std::fixed << std::setprecision(1)
							  << median
							  << " fingerprint=" << reference_fingerprint << '\n';
					if (batch_size >= kScalingGateMinimumBatchSize && previous_workers != 0U && median <= previous_median)
					{
						throw std::runtime_error(
							"worker scaling gate failed: throughput must increase strictly with worker count");
					}
					previous_workers = workers;
					previous_median = median;
				}
			}
		}
		return EXIT_SUCCESS;
	}

} // namespace

int main(int argc, char** argv) noexcept
{
	try
	{
		TEXASSOLVER_TRACY_SET_THREAD_NAME("mw.train.main");
		return run(parse_args(argc, argv));
	}
	catch (const std::exception& error)
	{
		std::cerr << "benchmark failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
