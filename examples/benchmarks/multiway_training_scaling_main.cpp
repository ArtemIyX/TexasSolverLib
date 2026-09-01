#include "games/multiway_state.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct BenchmarkConfig {
    std::vector<std::uint32_t> workers{4U, 8U, 16U};
    std::vector<std::uint32_t> batch_sizes{1000U, 4000U, 16000U};
    std::uint32_t warmup_batches = 1U;
    std::uint32_t timed_batches = 1U;
    std::uint32_t repeats = 3U;
    std::uint64_t seed = 0x77U;
};

struct BenchmarkResult {
    std::uint32_t workers = 0U;
    std::uint32_t batch_size = 0U;
    std::uint32_t sample = 0U;
    std::uint64_t trajectories = 0U;
    std::uint64_t elapsed_nanoseconds = 0U;
    std::uint64_t worker_active_nanoseconds = 0U;
    std::uint64_t coordinator_wait_nanoseconds = 0U;
    std::uint64_t sort_nanoseconds = 0U;
    std::uint64_t merge_nanoseconds = 0U;
    std::uint64_t minimum_worker_trajectories = 0U;
    std::uint64_t maximum_worker_trajectories = 0U;
    std::uint64_t peak_worker_delta_entries = 0U;
    std::uint64_t fingerprint = 0U;

    [[nodiscard]] double trajectories_per_second() const noexcept {
        return elapsed_nanoseconds == 0U ? 0.0 :
            static_cast<double>(trajectories) * 1.0e9 /
                static_cast<double>(elapsed_nanoseconds);
    }
};

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kBoard = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3), card(6, 0),
};

std::vector<std::uint32_t> one_bucket_assignments() {
    std::vector<std::uint32_t> result(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : kBoard) {
                if (first == board_card || second == board_card) {
                    result[texas::MultiwayBucketTable::hole_index({first, second})] =
                        texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return result;
}

texas::MultiwayRootSnapshot make_root(const texas::MultiwayActionAbstraction& abstraction) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000, 1000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = texas::Street::River;
    const auto betting = texas::MultiwayState::initial(game).snapshot();
    texas::MultiwayRootSnapshot root;
    root.public_state = texas::MultiwayPublicBuilder::make_root(
        betting, kBoard, abstraction.make_legal_actions(betting));
    root.root_infoset = {root.public_state.id, 0};
    root.root_bucket = 0U;
    root.seat_order = {0, 1, 2};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = kBoard;
    root.private_ranges.ranges = {
        {{{card(14, 0), card(13, 0)}, 1.0}},
        {{{card(12, 0), card(11, 0)}, 1.0}},
        {{{card(10, 0), card(8, 0)}, 1.0}},
    };
    root.action_abstraction_version = 1U;
    root.leaf_model_version = 1U;
    return root;
}

texas::Value deterministic_leaf(
    const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept {
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(
        request.betting->contributions[traverser] - request.betting->current_bet);
}

texas::MultiwayModelIdentity training_identity(
    std::uint32_t workers,
    std::uint32_t batch_size,
    std::size_t delta_capacity,
    std::uint64_t seed) {
    texas::MultiwayBlueprintTrainingConfig config;
    config.limits.worker_count = workers;
    config.limits.trajectories_per_batch = batch_size;
    config.limits.max_public_states = 256U;
    config.limits.max_sparse_rows = 128U;
    config.limits.max_sparse_values = 1024U;
    config.limits.max_worker_delta_entries = delta_capacity;
    config.limits.max_batches = 16U;
    config.deterministic_seed = seed;
    config.limits.seed = seed;
    return config.identity();
}

class BenchmarkFixture {
public:
    BenchmarkFixture(std::uint32_t workers, std::uint32_t batch_size, std::uint64_t seed)
        : delta_capacity_(checked_delta_capacity(batch_size)),
          root_(make_root(abstraction_)),
          request_(root_, make_cfr(), make_limits(workers, batch_size, delta_capacity_)),
          coordinator_(request_),
          buckets_({texas::MultiwayBucketTable(
              bucket_identity(), texas::Street::River, kBoard, 1U, one_bucket_assignments())}),
          evaluator_{deterministic_leaf, nullptr},
          traversal_(coordinator_, request_.root(), abstraction_, buckets_, &evaluator_, 1U),
          runner_(traversal_, coordinator_, workers, delta_capacity_),
          trainer_(training_identity(workers, batch_size, delta_capacity_, seed),
              runner_, coordinator_, {}, seed, workers, 0U) {}

    BenchmarkResult run(
        std::uint32_t workers,
        std::uint32_t batch_size,
        std::uint32_t warmup_batches,
        std::uint32_t timed_batches,
        std::uint32_t sample,
        std::uint64_t seed) {
        trainer_.run_batches(warmup_batches, batch_size, seed);
        const auto before = trainer_.status();
        const auto started = std::chrono::steady_clock::now();
        trainer_.run_batches(timed_batches, batch_size, seed);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto& after = trainer_.status();
        return {
            workers,
            batch_size,
            sample,
            static_cast<std::uint64_t>(timed_batches) * batch_size,
            static_cast<std::uint64_t>(elapsed),
            after.worker_active_nanoseconds - before.worker_active_nanoseconds,
            after.coordinator_wait_nanoseconds - before.coordinator_wait_nanoseconds,
            after.delta_sort_nanoseconds - before.delta_sort_nanoseconds,
            after.merge_nanoseconds - before.merge_nanoseconds,
            after.minimum_worker_trajectories,
            after.maximum_worker_trajectories,
            after.peak_worker_delta_entries,
            after.merged_stream_fingerprint,
        };
    }

private:
    static std::size_t checked_delta_capacity(std::uint32_t batch_size) {
        if (batch_size > std::numeric_limits<std::size_t>::max() / 4U) {
            throw std::overflow_error("batch size overflows worker delta capacity");
        }
        return static_cast<std::size_t>(batch_size) * 4U;
    }

    static texas::MultiwayCFRConfig make_cfr() {
        texas::MultiwayCFRConfig cfr;
        cfr.player_count = 3U;
        return cfr;
    }

    static texas::MultiwaySolverLimits make_limits(
        std::uint32_t workers,
        std::uint32_t batch_size,
        std::size_t delta_capacity) {
        texas::MultiwaySolverLimits limits;
        limits.worker_count = workers;
        limits.trajectories_per_batch = batch_size;
        limits.max_public_states = 256U;
        limits.max_sparse_rows = 128U;
        limits.max_sparse_values = 1024U;
        limits.max_worker_delta_entries = delta_capacity;
        limits.max_batches = 16U;
        limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
        return limits;
    }

    static texas::MultiwayModelIdentity bucket_identity() {
        texas::MultiwayBlueprintConfig config;
        config.player_count = 3U;
        return texas::make_multiway_model_identity(config);
    }

    std::size_t delta_capacity_ = 0U;
    texas::MultiwayActionAbstraction abstraction_;
    texas::MultiwayRootSnapshot root_;
    texas::MultiwaySolveRequest request_;
    texas::MultiwaySolverCoordinator coordinator_;
    texas::MultiwayBucketRegistry buckets_;
    texas::MultiwayLeafEvaluator evaluator_;
    texas::MultiwayRootExternalSamplingTraversal traversal_;
    texas::MultiwayRootBatchRunner runner_;
    texas::MultiwayBlueprintTrainer trainer_;
};

bool parse_positive_u32(std::string_view text, std::uint32_t& output) {
    try {
        const auto value = std::stoull(std::string(text));
        if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) return false;
        output = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_u64(std::string_view text, std::uint64_t& output) {
    try {
        output = std::stoull(std::string(text));
        return output != 0U;
    } catch (...) {
        return false;
    }
}

bool parse_csv(std::string_view text, std::vector<std::uint32_t>& output) {
    std::vector<std::uint32_t> parsed;
    std::size_t begin = 0U;
    while (begin <= text.size()) {
        const auto end = text.find(',', begin);
        const auto token = text.substr(begin,
            end == std::string_view::npos ? text.size() - begin : end - begin);
        std::uint32_t value = 0U;
        if (!parse_positive_u32(token, value)) return false;
        parsed.push_back(value);
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    std::sort(parsed.begin(), parsed.end());
    parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
    output = std::move(parsed);
    return !output.empty();
}

BenchmarkConfig parse_args(int argc, char** argv) {
    BenchmarkConfig config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help") {
            std::cout << "Usage: texas_solver_multiway_training_scaling [options]\n"
                      << "  --workers 4,8,16\n"
                      << "  --batch-sizes 1000,4000,16000\n"
                      << "  --warmup-batches N\n"
                      << "  --timed-batches N\n"
                      << "  --repeats N\n"
                      << "  --seed N\n";
            std::exit(EXIT_SUCCESS);
        }
        if (index + 1 >= argc) throw std::invalid_argument("benchmark option requires a value");
        const std::string_view value(argv[++index]);
        if (option == "--workers") {
            if (!parse_csv(value, config.workers)) throw std::invalid_argument("invalid workers list");
            for (const auto workers : config.workers) {
                if (workers > 16U) throw std::invalid_argument("workers must be in [1, 16]");
            }
        } else if (option == "--batch-sizes") {
            if (!parse_csv(value, config.batch_sizes)) throw std::invalid_argument("invalid batch-size list");
        } else if (option == "--warmup-batches") {
            if (!parse_positive_u32(value, config.warmup_batches)) throw std::invalid_argument("invalid warmup batch count");
        } else if (option == "--timed-batches") {
            if (!parse_positive_u32(value, config.timed_batches)) throw std::invalid_argument("invalid timed batch count");
        } else if (option == "--repeats") {
            if (!parse_positive_u32(value, config.repeats)) throw std::invalid_argument("invalid repeat count");
        } else if (option == "--seed") {
            if (!parse_u64(value, config.seed)) throw std::invalid_argument("invalid seed");
        } else {
            throw std::invalid_argument("unknown benchmark option");
        }
    }
    if (static_cast<std::uint64_t>(config.warmup_batches) + config.timed_batches > 16U) {
        throw std::invalid_argument("warmup plus timed batches exceeds benchmark capacity");
    }
    return config;
}

double median_throughput(std::vector<BenchmarkResult> results) {
    std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.trajectories_per_second() < right.trajectories_per_second();
    });
    return results[results.size() / 2U].trajectories_per_second();
}

void print_sample(const BenchmarkResult& result) {
    std::cout << "sample"
              << " workers=" << result.workers
              << " batch_size=" << result.batch_size
              << " repeat=" << result.sample
              << " trajectories=" << result.trajectories
              << " elapsed_ns=" << result.elapsed_nanoseconds
              << " trajectories_per_second=" << std::fixed << std::setprecision(1)
              << result.trajectories_per_second()
              << " coordinator_wait_ns=" << result.coordinator_wait_nanoseconds
              << " worker_active_ns=" << result.worker_active_nanoseconds
              << " sort_ns=" << result.sort_nanoseconds
              << " merge_ns=" << result.merge_nanoseconds
              << " worker_trajectories_min=" << result.minimum_worker_trajectories
              << " worker_trajectories_max=" << result.maximum_worker_trajectories
              << " peak_worker_delta_entries=" << result.peak_worker_delta_entries
              << " fingerprint=" << result.fingerprint << '\n';
}

int run(const BenchmarkConfig& config) {
    std::cout << "fixture=three_player_river_depth_one"
              << " warmup_batches=" << config.warmup_batches
              << " timed_batches=" << config.timed_batches
              << " repeats=" << config.repeats
              << " seed=" << config.seed << '\n';
    for (const auto batch_size : config.batch_sizes) {
        std::uint64_t reference_fingerprint = 0U;
        for (const auto workers : config.workers) {
            std::vector<BenchmarkResult> samples;
            samples.reserve(config.repeats);
            for (std::uint32_t sample = 1U; sample <= config.repeats; ++sample) {
                BenchmarkFixture fixture(workers, batch_size, config.seed);
                auto result = fixture.run(
                    workers, batch_size, config.warmup_batches,
                    config.timed_batches, sample, config.seed);
                if (reference_fingerprint == 0U) reference_fingerprint = result.fingerprint;
                if (result.fingerprint != reference_fingerprint) {
                    throw std::logic_error("benchmark worker counts produced different merge fingerprints");
                }
                print_sample(result);
                samples.push_back(result);
            }
            std::cout << "summary"
                      << " workers=" << workers
                      << " batch_size=" << batch_size
                      << " median_trajectories_per_second=" << std::fixed << std::setprecision(1)
                      << median_throughput(samples)
                      << " fingerprint=" << reference_fingerprint << '\n';
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) noexcept {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
