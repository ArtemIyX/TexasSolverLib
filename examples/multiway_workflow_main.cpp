#include <cstdlib>
#include <exception>
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"
#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"
#include "solver/multiway/workflow/multiway_artifact_preflight.hpp"
#include "solver/multiway/workflow/multiway_f1_acceptance.hpp"
#include "core/canonical_combo.hpp"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>
#include <memory>
#include "core/atomic_publish.hpp"

namespace {

enum class Workflow { Train, Buckets, Inspect, Evaluate, Finalize };

texas::solver::multiway::MultiwayEvidenceHeader make_evidence_header(
    std::uint32_t schema_version,
    texas::solver::multiway::MultiwayModelIdentity model_identity,
    std::uint64_t workflow_fingerprint) {
    using namespace texas::solver::multiway;
    const auto environment = [](const char* name) {
        const auto* value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string(value);
    };
    MultiwayEvidenceHeader header;
    header.schema_version = schema_version;
    header.producer.git_commit = environment("TEXASSOLVER_GIT_COMMIT");
    header.producer.build_configuration = environment("TEXASSOLVER_BUILD_CONFIGURATION");
    header.producer.compiler_identity = environment("TEXASSOLVER_COMPILER_IDENTITY");
    header.producer.model_identity = model_identity;
    header.producer.workflow_config_fingerprint = workflow_fingerprint;
    header.producer.artifact_schemas = {4U, 2U, 1U};
    return header;
}

Workflow workflow_from_name(std::string_view name) {
    if (name == "train") return Workflow::Train;
    if (name == "buckets") return Workflow::Buckets;
    if (name == "inspect") return Workflow::Inspect;
    if (name == "finalize") return Workflow::Finalize;
    return Workflow::Evaluate;
}

void print_help(std::string_view name, Workflow workflow) {
    std::cout << "TexasSolver multiway " << name << " workflow\n"
              << "Usage: " << name << " [options]\n\n"
              << "Options:\n"
              << "  --help                 Show this help\n"
              << "  --config <path>       Versioned workflow configuration\n"
              << "  --seed <integer>       Deterministic seed\n";
    std::cout << "  --tiny                 Run the bounded disk qualification pipeline\n";
    if (workflow == Workflow::Train) {
        std::cout << "  --batches <integer>    Bounded training batch count\n"
                  << "  --input <path>         Verified bucket artifact\n"
                  << "  --output <path>        Full blueprint artifact\n"
                  << "  --report <path>        JSON training report\n"
                  << "  --checkpoint-dir <dir> Output directory for incomplete runs\n"
                  << "  --resume <path>        Checkpoint to resume\n";
    } else if (workflow == Workflow::Buckets) {
        std::cout << "  --output <path>        Atomically published bucket artifact\n"
                  << "  --checkpoint-dir <dir> Resume/checkpoint directory\n"
                  << "  --threads <integer>   Bucket worker count (default physical-core cap)\n";
        std::cout << "  --benchmark-start <integer>   Global catalog start index\n"
                  << "  --benchmark-tables <integer>  Run bounded benchmark\n"
                  << "  --benchmark-mode <mode>       build-only or end-to-end\n";
    } else if (workflow == Workflow::Inspect) {
        std::cout << "  --input <path>         Artifact to inspect\n"
                  << "  --report <path>        Atomically published JSON inspection report\n";
    } else if (workflow == Workflow::Finalize) {
        std::cout << "  --bucket-report <path>       Bucket inspection report\n"
                  << "  --training-report <path>     Training report\n"
                  << "  --equivalence-report <path> Checkpoint equivalence report\n"
                  << "  --lookup-first <path>        First lookup report\n"
                  << "  --lookup-second <path>       Second lookup report\n"
                  << "  --operator <id>              Human operator identifier\n"
                  << "  --start-utc <timestamp>      Human-run start timestamp\n"
                  << "  --end-utc <timestamp>        Human-run end timestamp\n"
                  << "  --command <text>             Exact human command\n"
                  << "  --machine-report <path>      Machine provenance report\n"
                  << "  --exit-code <integer>        Human command exit code\n"
                  << "  --output <path>              final_acceptance.json\n";
    } else {
        std::cout << "  --artifacts <dir>      Verified artifact directory\n"
                  << "  --duplicates <integer> Duplicate deal count\n";
    }
}

int run_tiny_pipeline() {
    using namespace texas;
    using namespace texas::solver::multiway;
    const auto directory = std::filesystem::temp_directory_path() / "texas_solver_f2_tiny";
    std::filesystem::create_directories(directory);
    const auto identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    MultiwayFutureBucketProfile profile;
    profile.flop_bucket_count = 2U;
    profile.turn_bucket_count = 2U;
    profile.river_bucket_count = 2U;
    const std::vector<MultiwayBucketBoardRequest> boards = {
        {Street::Flop, {0U, 5U, 10U}}, {Street::Turn, {0U, 5U, 10U, 15U}}};
    const auto bucket = build_multiway_future_bucket_artifact(identity, boards, profile);
    const auto bucket_path = directory / "future-buckets.bin";
    save_multiway_future_bucket_artifact_atomic(bucket_path, bucket);
    const auto loaded_bucket = load_multiway_future_bucket_artifact(bucket_path, identity);
    if (loaded_bucket.registry().identity() != identity) throw std::runtime_error("bucket identity mismatch");

    MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = identity;
    snapshot.public_state = {71U};
    snapshot.infoset = {{71U}, 0};
    snapshot.trajectories = 1U;
    snapshot.training.trajectories = 1U;
    snapshot.training.deterministic_seed = 7U;
    snapshot.actions = {{{MultiwayAction::Check, 0U, 0, 17U}, 65535U}};
    snapshot.validate();
    const auto root_path = directory / "root.bin";
    MultiwayBlueprintArtifacts::save_atomic(root_path, snapshot);
    const auto loaded_root = MultiwayBlueprintArtifacts::load_verified(root_path, identity);
    if (MultiwayBlueprintArtifacts::snapshot_hash(loaded_root.snapshot) !=
        MultiwayBlueprintArtifacts::snapshot_hash(snapshot)) {
        throw std::runtime_error("root artifact hash mismatch");
    }

    MultiwayFullBlueprintArtifact full;
    full.identity = identity;
    full.training = snapshot.training;
    full.rows = {{{{71U}, 0}, 0U, 17U, {{{MultiwayAction::Check, 0U, 0, 17U}, 65535U}}}};
    const auto full_path = directory / "full-blueprint.bin";
    MultiwayFullBlueprintArtifacts::save_atomic(full_path, full);
    const auto loaded_full = MultiwayFullBlueprintArtifacts::load_verified(full_path, identity);
    if (loaded_full.rows.size() != 1U) throw std::runtime_error("full blueprint row count mismatch");
    std::cout << "tiny F2 pipeline passed: buckets=1 root_rows=1 full_rows="
              << loaded_full.rows.size() << "\n";
    return EXIT_SUCCESS;
}

void save_bucket_manifest_atomic(const std::filesystem::path& path,
                                 const texas::solver::multiway::MultiwayWorkflowConfig& workflow,
                                 const texas::solver::multiway::MultiwayBucketArtifactInspection& report,
                                 std::uintmax_t byte_length) {
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open bucket manifest");
    output << "{\n  \"config_fingerprint\": " << workflow.fingerprint()
           << ",\n  \"identity\": " << report.identity.combined_hash
           << ",\n  \"byte_length\": " << byte_length
           << ",\n  \"flop_tables\": " << report.flop_tables
           << ",\n  \"turn_tables\": " << report.turn_tables
           << ",\n  \"river_tables\": " << report.river_tables
           << ",\n  \"live_assignments\": " << report.live_assignments
           << ",\n  \"payload_hash\": " << report.payload_hash << "\n}\n";
    output.close();
    texas::core::publish_atomic_replace(temporary, path, "cannot publish bucket manifest");
}

std::uint64_t estimate_full_blueprint_bytes(
    const texas::solver::multiway::MultiwayWorkflowConfig& workflow) {
    using namespace texas::solver::multiway;
    constexpr std::uint64_t max_actions_per_row = 64U;
    constexpr std::uint64_t fixed_bytes = 256U;
    const auto row_bytes = static_cast<std::uint64_t>(sizeof(MultiwayBlueprintRow));
    const auto action_bytes = static_cast<std::uint64_t>(sizeof(MultiwayQuantizedRootAction));
    if (action_bytes != 0U && max_actions_per_row >
        (std::numeric_limits<std::uint64_t>::max() - row_bytes) / action_bytes) {
        throw std::length_error("blueprint size estimate overflows");
    }
    const auto per_row = row_bytes + max_actions_per_row * action_bytes;
    if (workflow.maximum_sparse_rows >
        (std::numeric_limits<std::uint64_t>::max() - fixed_bytes) / per_row) {
        throw std::length_error("blueprint size estimate overflows");
    }
    return fixed_bytes + workflow.maximum_sparse_rows * per_row;
}

int run_buckets(const std::filesystem::path& config_path, const std::filesystem::path& output_path,
                const std::filesystem::path& checkpoint_dir, std::uint32_t requested_threads) {
    using namespace texas::solver::multiway;
    std::cout << "texas_multiway_buckets: starting bucket generation\n"
              << "  config=" << config_path.string() << "\n"
              << "  output=" << output_path.string() << "\n"
              << "  checkpoint_dir="
              << (checkpoint_dir.empty() ? std::string("<disabled>") : checkpoint_dir.string()) << "\n"
              << "  requested_threads=" << requested_threads << std::endl;
    const auto workflow = load_multiway_workflow_config(config_path);
    const auto identity = make_multiway_model_identity(workflow.model);
    MultiwayBucketBaselineProfile profile = MultiwayBucketBaselineProfile::standard();
    profile.flop_bucket_count = workflow.model.flop_bucket_count;
    profile.turn_bucket_count = workflow.model.turn_bucket_count;
    profile.river_bucket_count = workflow.model.river_bucket_count;
    const auto temporary = output_path.string() + ".tmp";
    if (std::filesystem::exists(output_path)) throw std::runtime_error("bucket artifact already exists; refusing overwrite");
    if (!output_path.parent_path().empty()) std::filesystem::create_directories(output_path.parent_path());
    const auto expected_tables = multiway_bucket_board_count(texas::core::Street::Flop) +
        multiway_bucket_board_count(texas::core::Street::Turn) + multiway_bucket_board_count(texas::core::Street::River);
    const auto hardware_threads = multiway_bucket_hardware_thread_count();
    const auto effective_threads = resolve_multiway_bucket_thread_count(
        requested_threads, hardware_threads);
    std::cout << "bucket generation goal:\n"
              << "  profile=" << workflow.profile_id
              << " kind=" << static_cast<unsigned>(workflow.profile_kind)
              << " schema=" << workflow.schema_version << "\n"
              << "  model_identity=" << identity.combined_hash
              << " config_fingerprint=" << workflow.fingerprint() << "\n"
              << "  players=" << static_cast<unsigned>(workflow.model.player_count)
              << " buckets=" << workflow.model.flop_bucket_count << '/'
              << workflow.model.turn_bucket_count << '/'
              << workflow.model.river_bucket_count
              << " seed=" << workflow.deterministic_seed << "\n"
              << "  tables=" << expected_tables
              << " chunk_size=" << MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE
              << " hardware_threads=" << hardware_threads
              << " physical_cores=" << multiway_bucket_physical_core_count()
              << " effective_threads=" << effective_threads << std::endl;
    if (!checkpoint_dir.empty()) std::filesystem::create_directories(checkpoint_dir);
    constexpr std::uint64_t max_table_bytes = 2U + 5U + 4U +
        static_cast<std::uint64_t>(MULTIWAY_HOLE_COMBINATION_COUNT) * 4U;
    if (expected_tables > (std::numeric_limits<std::uint64_t>::max() - 116U) / max_table_bytes) {
        throw std::length_error("bucket artifact size estimate overflows");
    }
    const auto estimated_bytes = 116U + expected_tables * max_table_bytes;
    const auto progress_path = checkpoint_dir.empty() ? std::filesystem::path{} : checkpoint_dir / "latest.progress";
    preflight_multiway_artifact({
        output_path,
        output_path.string() + ".tmp",
        progress_path,
        identity,
        expected_tables,
        estimated_bytes,
        workflow.disk_space_requirement_bytes == 0U
            ? estimated_bytes : workflow.disk_space_requirement_bytes,
        estimate_multiway_bucket_generation_process_memory_bytes(
            requested_threads, hardware_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE),
        workflow.process_memory_limit_bytes});
    std::unique_ptr<MultiwayBucketArtifactWriter> writer;
    if (!checkpoint_dir.empty() && std::filesystem::exists(temporary) && std::filesystem::exists(progress_path)) {
        writer = std::make_unique<MultiwayBucketArtifactWriter>(MultiwayBucketArtifactWriter::resume(
            temporary, identity, expected_tables,
            load_multiway_bucket_progress(progress_path, identity, expected_tables)));
    } else {
        if (std::filesystem::exists(temporary)) {
            throw std::runtime_error("bucket temporary artifact has no verified resume sidecar");
        }
        writer = std::make_unique<MultiwayBucketArtifactWriter>(temporary, identity, expected_tables);
    }
    const auto already_written = writer->progress().table_count;
    const auto progress_start = std::chrono::steady_clock::now();
    const auto publish_checkpoint = [&]() {
        if (checkpoint_dir.empty()) return;
        writer->flush_checkpoint();
        save_multiway_bucket_progress_atomic(checkpoint_dir / "latest.progress", identity,
            expected_tables, writer->progress());
        const auto completed = writer->progress().table_count;
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - progress_start).count();
        const auto rate = elapsed > 0.0
            ? static_cast<double>(completed - already_written) / elapsed : 0.0;
        const auto remaining = rate > 0.0
            ? static_cast<double>(expected_tables - completed) / rate : 0.0;
        const auto percentage = expected_tables == 0U
            ? 100.0 : 100.0 * static_cast<double>(completed) /
                static_cast<double>(expected_tables);
        std::cout << "bucket progress: tables=" << completed << '/' << expected_tables
                  << " (" << std::fixed << std::setprecision(2) << percentage << "%)"
                  << " rate=" << rate << "/s elapsed=" << elapsed
                  << "s eta=" << remaining << "s\n";
    };
    const auto build_chunk = [identity, profile](
                                 std::uint64_t begin_index,
                                 std::uint64_t end_index,
                                 std::vector<MultiwayBucketTable>& tables) {
        build_multiway_baseline_bucket_chunk(
            identity, profile, begin_index, end_index, tables);
    };
    const auto direct_build_chunk = [identity, profile](std::uint64_t begin_index,
                                                         std::uint64_t end_index,
                                                         std::vector<std::uint8_t>& payload) {
        build_multiway_baseline_direct_serialized_chunk(identity, profile, begin_index, end_index, payload);
    };
    try {
        generate_multiway_bucket_serialized_chunks(
            already_written, expected_tables,
            {requested_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE, 0U, nullptr, direct_build_chunk},
            build_chunk,
            [&](std::uint64_t begin_index, std::uint64_t table_count, std::vector<std::uint8_t>& payload) {
                if (begin_index != writer->progress().table_count) {
                    throw std::logic_error("bucket chunks were published out of order");
                }
                writer->append_serialized_chunk(table_count, payload);
                const auto completed = begin_index + table_count;
                    const auto flop_end = multiway_bucket_board_count(texas::core::Street::Flop);
                    const auto turn_end = flop_end +
                        multiway_bucket_board_count(texas::core::Street::Turn);
                    if (!checkpoint_dir.empty() &&
                        (completed % 4096U == 0U || completed == flop_end ||
                         completed == turn_end || completed == expected_tables)) {
                        publish_checkpoint();
                    }
            });
    } catch (...) {
        try {
            publish_checkpoint();
        } catch (...) {
        }
        throw;
    }
    writer->finish(output_path);
    const auto inspection = inspect_multiway_bucket_artifact(output_path, identity);
    if (inspection.flop_tables != multiway_bucket_board_count(texas::core::Street::Flop) ||
        inspection.turn_tables != multiway_bucket_board_count(texas::core::Street::Turn) ||
        inspection.river_tables != multiway_bucket_board_count(texas::core::Street::River)) {
        throw std::runtime_error("published bucket artifact has incomplete canonical coverage");
    }
    auto manifest_path = output_path;
    manifest_path.replace_extension(".manifest");
    save_bucket_manifest_atomic(manifest_path, workflow, inspection, std::filesystem::file_size(output_path));
    std::cout << "published bucket artifact: tables=" << writer->progress().table_count
              << " bytes=" << writer->progress().byte_length << "\n";
    return EXIT_SUCCESS;
}

int run_bucket_benchmark(const std::filesystem::path& config_path, std::uint64_t start_index,
                         std::uint64_t table_count, std::uint32_t requested_threads, bool publish) {
    using namespace texas::solver::multiway;
    const auto total_tables = multiway_bucket_board_count(texas::core::Street::Flop) +
        multiway_bucket_board_count(texas::core::Street::Turn) + multiway_bucket_board_count(texas::core::Street::River);
    if (table_count == 0U || start_index > total_tables || table_count > total_tables - start_index) {
        throw std::invalid_argument("benchmark range exceeds the bucket catalog");
    }
    const auto workflow = load_multiway_workflow_config(config_path);
    const auto identity = make_multiway_model_identity(workflow.model);
    auto profile = MultiwayBucketBaselineProfile::standard();
    profile.flop_bucket_count = workflow.model.flop_bucket_count;
    profile.turn_bucket_count = workflow.model.turn_bucket_count;
    profile.river_bucket_count = workflow.model.river_bucket_count;
    std::uint64_t checksum = 0U;
    MultiwayBucketGenerationStats stats;
    const auto output = std::filesystem::temp_directory_path() /
        ("texas_solver_bucket_benchmark_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    const auto temporary = output.string() + ".tmp";
    std::unique_ptr<MultiwayBucketArtifactWriter> writer;
    if (publish) writer = std::make_unique<MultiwayBucketArtifactWriter>(temporary, identity, table_count);
    const auto start = std::chrono::steady_clock::now();
    const auto direct_build = [identity, profile](std::uint64_t begin, std::uint64_t end,
                                                   std::vector<std::uint8_t>& payload) {
        build_multiway_baseline_direct_serialized_chunk(identity, profile, begin, end, payload);
    };
    generate_multiway_bucket_serialized_chunks(start_index, start_index + table_count,
        {requested_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE, 0U, &stats, direct_build},
        [identity, profile](std::uint64_t begin, std::uint64_t end,
                            std::vector<MultiwayBucketTable>& tables) {
            build_multiway_baseline_bucket_chunk(identity, profile, begin, end, tables);
        },
        [&checksum, &writer, publish](std::uint64_t, std::uint64_t count, std::vector<std::uint8_t>& payload) {
            for (const auto byte : payload) checksum = checksum * 131U + byte;
            if (publish) writer->append_serialized_chunk(count, payload);
        });
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (publish) {
        writer->finish(output);
        std::filesystem::remove(output);
    }
    std::cout << "bucket benchmark,mode=" << (publish ? "end-to-end" : "build-only") << ",start=" << start_index
              << ",tables=" << table_count
              << ",threads=" << requested_threads << ",seconds=" << seconds
              << ",tables_per_second=" << static_cast<double>(table_count) / seconds
              << ",hardware_threads=" << multiway_bucket_hardware_thread_count()
              << ",physical_cores=" << multiway_bucket_physical_core_count()
              << ",checksum=" << checksum << ",chunks_built=" << stats.chunks_built
              << ",chunks_published=" << stats.chunks_published
              << ",ready_high_watermark=" << stats.ready_queue_high_watermark
              << ",worker_wait_ns=" << stats.worker_wait_nanoseconds
              << ",publish_wait_ns=" << stats.ordered_publish_wait_nanoseconds
              << ",worker_active_ns=" << stats.worker_active_nanoseconds << "\n";
    return EXIT_SUCCESS;
}

int run_inspect(const std::filesystem::path& config_path, const std::filesystem::path& input_path,
                const std::filesystem::path& report_path) {
    using namespace texas::solver::multiway;
    const auto workflow = load_multiway_workflow_config(config_path);
    const auto report = inspect_multiway_bucket_artifact(input_path, make_multiway_model_identity(workflow.model));
    if (report.flop_tables == 0U || report.turn_tables == 0U || report.river_tables == 0U) {
        throw std::runtime_error("bucket artifact has incomplete street coverage");
    }
    const auto text = std::string("{\n  \"identity\": ") + std::to_string(report.identity.combined_hash) +
        ",\n  \"evidence\": " + serialize_multiway_evidence_header(
            make_evidence_header(MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION,
                make_multiway_model_identity(workflow.model), workflow.fingerprint())) +
        ",\n  \"flop_tables\": " + std::to_string(report.flop_tables) +
        ",\n  \"turn_tables\": " + std::to_string(report.turn_tables) +
        ",\n  \"river_tables\": " + std::to_string(report.river_tables) +
        ",\n  \"live_assignments\": " + std::to_string(report.live_assignments) +
        ",\n  \"payload_hash\": " + std::to_string(report.payload_hash) +
        ",\n  \"identity_matches\": " +
        (report.identity == make_multiway_model_identity(workflow.model) ? "true" : "false") +
        ",\n  \"payload_hash_matches\": " +
        (report.payload_hash != 0U ? "true" : "false") + "\n}\n";
    if (!report_path.empty()) {
        if (!report_path.parent_path().empty()) std::filesystem::create_directories(report_path.parent_path());
        preflight_multiway_artifact({
            report_path,
            report_path.string() + ".tmp",
            {},
            make_multiway_model_identity(workflow.model),
            1U,
            4096U,
            4096U,
            0U,
            0U});
        const auto temporary = report_path.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open inspection report");
        output << text;
        output.close();
        texas::core::publish_atomic_replace(temporary, report_path, "cannot publish inspection report");
    }
    std::cout << "flop_tables=" << report.flop_tables << "\n"
              << "turn_tables=" << report.turn_tables << "\n"
              << "river_tables=" << report.river_tables << "\n"
              << "live_assignments=" << report.live_assignments << "\n"
              << "payload_hash=" << report.payload_hash << "\n";
    return EXIT_SUCCESS;
}

int run_train(const std::filesystem::path& config_path, const std::filesystem::path& input_path,
              const std::filesystem::path& output_path, std::uint64_t batches,
              const std::filesystem::path& report_path,
              const std::filesystem::path& checkpoint_dir,
              const std::filesystem::path& resume_path) {
    using namespace texas;
    using namespace texas::solver::multiway;
    if (batches == 0U || input_path.empty() || output_path.empty()) throw std::invalid_argument("train requires --input, --output, and --batches");
    const auto workflow = load_multiway_workflow_config(config_path);
    if (!workflow.capacities_resolved()) {
        throw std::invalid_argument("train requires a sizing-frozen workflow configuration");
    }
    const auto buckets = load_multiway_bucket_registry(input_path);
    if (!output_path.parent_path().empty()) std::filesystem::create_directories(output_path.parent_path());
    const auto identity = make_multiway_model_identity(workflow.model);
    const auto estimated_blueprint_bytes = estimate_full_blueprint_bytes(workflow);
    preflight_multiway_artifact({
        output_path,
        output_path.string() + ".tmp",
        {},
        identity,
        1U,
        estimated_blueprint_bytes,
        workflow.disk_space_requirement_bytes,
        estimated_blueprint_bytes,
        workflow.process_memory_limit_bytes});
    MultiwayBlueprintTrainingConfig config;
    config.blueprint = workflow.model;
    config.bucket_profile.flop_bucket_count = workflow.model.flop_bucket_count;
    config.bucket_profile.turn_bucket_count = workflow.model.turn_bucket_count;
    config.bucket_profile.river_bucket_count = workflow.model.river_bucket_count;
    config.rules = MultiwayGameRules::standard_6max();
    config.max_decision_depth = workflow.max_decision_depth;
    config.max_public_chance_depth = workflow.max_public_chance_depth;
    config.deterministic_seed = workflow.deterministic_seed;
    config.limits.seed = workflow.deterministic_seed;
    config.limits.worker_count = workflow.reference_worker_count;
    config.limits.trajectories_per_batch = static_cast<std::uint32_t>(workflow.trajectories_per_batch);
    config.limits.max_public_states = static_cast<std::size_t>(workflow.maximum_public_states);
    config.limits.max_sparse_rows = static_cast<std::size_t>(workflow.maximum_sparse_rows);
    config.limits.max_sparse_values = static_cast<std::size_t>(workflow.maximum_sparse_values);
    config.limits.max_worker_delta_entries = static_cast<std::size_t>(workflow.worker_delta_capacity);
    config.limits.max_batches = batches;
    config.limits.storage_backend = workflow.storage_backend == "CompactInt32"
        ? MultiwaySolverLimits::StorageBackend::CompactInt32
        : MultiwaySolverLimits::StorageBackend::Float64Reference;
    MultiwayPrivateConfig ranges;
    ranges.ranges.resize(6U);
    for (std::size_t seat = 0U; seat < ranges.ranges.size(); ++seat) {
        ranges.ranges[seat].reserve(core::CANONICAL_HOLE_COMBINATION_COUNT);
        for (core::CanonicalComboId id = 0U; id < core::CANONICAL_HOLE_COMBINATION_COUNT; ++id)
            ranges.ranges[seat].push_back({core::canonical_combos().cards(id), 1.0});
    }
    MultiwayActionAbstraction abstraction;
    const auto root = make_multiway_initial_blueprint_root(config.rules, std::move(ranges), abstraction,
        config.blueprint.action_abstraction_version, config.blueprint.terminal_model_version);
    MultiwayBlueprintTrainingSession session(config, root, buckets);
    const auto checkpoint_path = !resume_path.empty() ? resume_path :
        (checkpoint_dir.empty() ? std::filesystem::path{} : checkpoint_dir / "latest.bin");
    if (!resume_path.empty()) {
        session.resume_from_checkpoint(MultiwayTrainingCheckpointArtifacts::load_verified(
            resume_path, config.identity(), config.schedule.identity(), config.deterministic_seed));
    }
    std::uint64_t remaining_batches = batches;
    std::uint64_t checkpoint_bytes = 0U;
    std::uint64_t checkpoint_write_nanoseconds = 0U;
    const auto publish_training_failure = [&]() {
        auto failure_report = make_multiway_training_report(
            session.status(), session.status().merged_stream_fingerprint,
            make_evidence_header(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
                config.identity(), workflow.fingerprint()),
            workflow.process_memory_limit_bytes);
        failure_report.failed = true;
        save_multiway_training_report_atomic(
            report_path.empty() ? output_path.string() + ".json" : report_path,
            failure_report);
    };
    try {
        while (remaining_batches != 0U) {
            const auto next = std::min(remaining_batches, workflow.checkpoint_interval);
            session.run_batches(next);
            remaining_batches -= next;
            if (!checkpoint_path.empty()) {
                if (!checkpoint_path.parent_path().empty()) {
                    std::filesystem::create_directories(checkpoint_path.parent_path());
                }
                const auto checkpoint_start = std::chrono::steady_clock::now();
                MultiwayTrainingCheckpointArtifacts::save_atomic(checkpoint_path, session.checkpoint());
                checkpoint_write_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - checkpoint_start).count());
                std::error_code checkpoint_error;
                const auto bytes = std::filesystem::file_size(checkpoint_path, checkpoint_error);
                if (checkpoint_error) throw std::runtime_error("cannot measure training checkpoint");
                checkpoint_bytes = std::max(checkpoint_bytes, static_cast<std::uint64_t>(bytes));
            }
        }
    } catch (...) {
        publish_training_failure();
        throw;
    }
    const auto& status = session.status();
    auto training_report = make_multiway_training_report(
        status, status.merged_stream_fingerprint,
        make_evidence_header(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
            config.identity(), workflow.fingerprint()),
        workflow.process_memory_limit_bytes);
    if (status.discarded_trajectories != 0U || status.preflop_rows == 0U ||
        status.flop_rows == 0U || status.turn_rows == 0U || status.river_rows == 0U ||
        status.terminal_visits == 0U) {
        training_report.failed = true;
        save_multiway_training_report_atomic(
            report_path.empty() ? output_path.string() + ".json" : report_path,
            training_report);
        throw std::runtime_error("training failed the F1 coverage or discard gate");
    }
    const auto blueprint_start = std::chrono::steady_clock::now();
    const auto artifact = session.export_full_policy();
    MultiwayFullBlueprintArtifacts::save_atomic(output_path, artifact);
    const auto blueprint_export_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - blueprint_start).count());
    std::error_code blueprint_error;
    const auto blueprint_bytes = std::filesystem::file_size(output_path, blueprint_error);
    if (blueprint_error) throw std::runtime_error("cannot measure blueprint artifact");
    training_report.checkpoint_bytes = checkpoint_bytes;
    training_report.checkpoint_write_nanoseconds = checkpoint_write_nanoseconds;
    training_report.blueprint_bytes = static_cast<std::uint64_t>(blueprint_bytes);
    training_report.blueprint_export_nanoseconds = blueprint_export_nanoseconds;
    save_multiway_training_report_atomic(
        report_path.empty() ? output_path.string() + ".json" : report_path,
        training_report);
    return EXIT_SUCCESS;
}

int run_evaluate(const std::filesystem::path& config_path,
                 const std::filesystem::path& artifacts_path,
                 std::uint64_t trajectories,
                 const std::filesystem::path& report_path) {
    using namespace texas;
    using namespace texas::solver::multiway;
    if (artifacts_path.empty() || trajectories == 0U) {
        throw std::invalid_argument("evaluate requires --artifacts and --duplicates");
    }
    const auto workflow = load_multiway_workflow_config(config_path);
    if (!workflow.capacities_resolved()) {
        throw std::invalid_argument("evaluate requires a sizing-frozen workflow configuration");
    }
    const auto identity = make_multiway_model_identity(workflow.model);
    const auto buckets = load_multiway_bucket_registry(artifacts_path / "buckets.bin");
    const auto blueprint = MultiwayFullBlueprintArtifacts::load_verified(
        artifacts_path / "blueprint.bin", identity);
    MultiwayBlueprintTrainingConfig config;
    config.blueprint = workflow.model;
    config.bucket_profile.flop_bucket_count = workflow.model.flop_bucket_count;
    config.bucket_profile.turn_bucket_count = workflow.model.turn_bucket_count;
    config.bucket_profile.river_bucket_count = workflow.model.river_bucket_count;
    config.rules = MultiwayGameRules::standard_6max();
    config.max_decision_depth = workflow.max_decision_depth;
    config.max_public_chance_depth = workflow.max_public_chance_depth;
    config.deterministic_seed = workflow.deterministic_seed;
    config.limits.seed = workflow.deterministic_seed;
    config.limits.worker_count = workflow.reference_worker_count;
    config.limits.trajectories_per_batch = static_cast<std::uint32_t>(workflow.trajectories_per_batch);
    config.limits.max_public_states = static_cast<std::size_t>(workflow.maximum_public_states);
    config.limits.max_sparse_rows = static_cast<std::size_t>(workflow.maximum_sparse_rows);
    config.limits.max_sparse_values = static_cast<std::size_t>(workflow.maximum_sparse_values);
    config.limits.max_worker_delta_entries = static_cast<std::size_t>(workflow.worker_delta_capacity);
    config.limits.storage_backend = MultiwaySolverLimits::StorageBackend::CompactInt32;
    MultiwayPrivateConfig ranges;
    ranges.ranges.resize(6U);
    for (auto& range : ranges.ranges) {
        range.reserve(core::CANONICAL_HOLE_COMBINATION_COUNT);
        for (core::CanonicalComboId id = 0U; id < core::CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
            range.push_back({core::canonical_combos().cards(id), 1.0});
        }
    }
    MultiwayActionAbstraction abstraction;
    const auto root = make_multiway_initial_blueprint_root(
        config.rules, std::move(ranges), abstraction,
        config.blueprint.action_abstraction_version, config.blueprint.terminal_model_version);
    const auto evidence = make_evidence_header(
        MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION, config.identity(), workflow.fingerprint());
    auto report = qualify_multiway_required_lookups(
        config, root, buckets, blueprint, trajectories, evidence);
    const auto repeat = qualify_multiway_required_lookups(
        config, root, buckets, blueprint, trajectories, evidence);
    report.second_replay_fingerprint = repeat.replay_fingerprint;
    const auto output = report_path.empty() ? artifacts_path / "lookup_report.json" : report_path;
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    save_multiway_lookup_qualification_report_atomic(output, report);
    if (!report.passed() || repeat.lookup_hits != report.lookup_hits ||
        repeat.missing_infosets != report.missing_infosets ||
        repeat.missing_buckets != report.missing_buckets ||
        repeat.action_menu_mismatches != report.action_menu_mismatches) {
        throw std::runtime_error("required blueprint lookup qualification failed");
    }
    return EXIT_SUCCESS;
}

int run_finalize(const std::filesystem::path& output_path,
                 const std::filesystem::path& bucket_report,
                 const std::filesystem::path& training_report,
                 const std::filesystem::path& equivalence_report,
                 const std::filesystem::path& lookup_first,
                 const std::filesystem::path& lookup_second,
                 texas::solver::multiway::MultiwayF1HumanRunRecord human_run) {
    using namespace texas::solver::multiway;
    if (output_path.empty()) throw std::invalid_argument("finalize requires --output");
    MultiwayF1AcceptanceInputPaths paths;
    paths.bucket_inspection_report = bucket_report;
    paths.training_report = training_report;
    paths.checkpoint_equivalence_report = equivalence_report;
    paths.first_lookup_report = lookup_first;
    paths.second_lookup_report = lookup_second;
    paths.human_run = std::move(human_run);
    finalize_multiway_f1_acceptance_atomic(output_path, paths);
    std::cout << "published F1 acceptance report: " << output_path.string() << "\n";
    return EXIT_SUCCESS;
}

bool consume_value(int& index, int argc, char** argv, std::string_view option) {
    if (std::string_view(argv[index]) != option || index + 1 >= argc) return false;
    ++index;
    return true;
}

int run(std::string_view name, int argc, char** argv) {
    const Workflow workflow = workflow_from_name(name);
    std::filesystem::path config_path;
    std::filesystem::path output_path;
    std::filesystem::path input_path;
    std::filesystem::path report_path;
    std::filesystem::path checkpoint_dir;
    std::filesystem::path resume_path;
    std::filesystem::path artifacts_path;
    std::filesystem::path bucket_report;
    std::filesystem::path training_report;
    std::filesystem::path equivalence_report;
    std::filesystem::path lookup_first;
    std::filesystem::path lookup_second;
    std::string operator_id;
    std::string start_utc;
    std::string end_utc;
    std::string command;
    std::filesystem::path machine_report;
    std::int32_t exit_code = -1;
    std::uint64_t batches = 0U;
    std::uint64_t duplicates = 0U;
    std::uint32_t threads = (std::min)(
        texas::solver::multiway::MULTIWAY_BUCKET_DEFAULT_THREADS,
        texas::solver::multiway::multiway_bucket_physical_core_count());
    std::uint64_t benchmark_tables = 0U;
    std::uint64_t benchmark_start = 0U;
    std::string benchmark_mode = "build-only";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            print_help(name, workflow);
            return EXIT_SUCCESS;
        }
        if (argument == "--tiny") return run_tiny_pipeline();
        if (argument == "--config" || argument == "--seed" || argument == "--batches" ||
            argument == "--checkpoint-dir" || argument == "--resume" || argument == "--output" ||
            argument == "--input" || argument == "--report" || argument == "--artifacts" || argument == "--duplicates" ||
            argument == "--threads" || argument == "--benchmark-start" || argument == "--benchmark-tables" || argument == "--benchmark-mode" ||
            argument == "--bucket-report" || argument == "--training-report" ||
            argument == "--equivalence-report" || argument == "--lookup-first" ||
            argument == "--lookup-second" || argument == "--operator" || argument == "--start-utc" ||
            argument == "--end-utc" || argument == "--command" || argument == "--machine-report" ||
            argument == "--exit-code") {
            if (!consume_value(index, argc, argv, argument)) {
                std::cerr << argument << " requires a value\n";
                return EXIT_FAILURE;
            }
            if (argument == "--config") config_path = argv[index];
            if (argument == "--output") output_path = argv[index];
            if (argument == "--input") input_path = argv[index];
            if (argument == "--report") report_path = argv[index];
            if (argument == "--checkpoint-dir") checkpoint_dir = argv[index];
            if (argument == "--resume") resume_path = argv[index];
            if (argument == "--artifacts") artifacts_path = argv[index];
            if (argument == "--batches") batches = std::stoull(argv[index]);
            if (argument == "--duplicates") duplicates = std::stoull(argv[index]);
            if (argument == "--threads") {
                const auto parsed = std::stoull(argv[index]);
                if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::invalid_argument("--threads exceeds uint32 range");
                }
                threads = static_cast<std::uint32_t>(parsed);
            }
            if (argument == "--benchmark-tables") benchmark_tables = std::stoull(argv[index]);
            if (argument == "--benchmark-start") benchmark_start = std::stoull(argv[index]);
            if (argument == "--benchmark-mode") benchmark_mode = argv[index];
            if (argument == "--bucket-report") bucket_report = argv[index];
            if (argument == "--training-report") training_report = argv[index];
            if (argument == "--equivalence-report") equivalence_report = argv[index];
            if (argument == "--lookup-first") lookup_first = argv[index];
            if (argument == "--lookup-second") lookup_second = argv[index];
            if (argument == "--operator") operator_id = argv[index];
            if (argument == "--start-utc") start_utc = argv[index];
            if (argument == "--end-utc") end_utc = argv[index];
            if (argument == "--command") command = argv[index];
            if (argument == "--machine-report") machine_report = argv[index];
            if (argument == "--exit-code") exit_code = static_cast<std::int32_t>(std::stoll(argv[index]));
            continue;
        }
        std::cerr << "unknown option: " << argument << "\n";
        return EXIT_FAILURE;
    }
    if (workflow == Workflow::Buckets && !config_path.empty() && benchmark_tables != 0U) {
        if (benchmark_mode != "build-only" && benchmark_mode != "end-to-end") {
            throw std::invalid_argument("--benchmark-mode must be build-only or end-to-end");
        }
        return run_bucket_benchmark(config_path, benchmark_start, benchmark_tables, threads, benchmark_mode == "end-to-end");
    }
    if (workflow == Workflow::Buckets && !config_path.empty() && !output_path.empty()) {
        return run_buckets(config_path, output_path, checkpoint_dir, threads);
    }
    if (workflow == Workflow::Inspect && !config_path.empty() && !input_path.empty()) {
        return run_inspect(config_path, input_path, report_path);
    }
    if (workflow == Workflow::Train && !config_path.empty()) {
        return run_train(config_path, input_path, output_path, batches, report_path, checkpoint_dir, resume_path);
    }
    if (workflow == Workflow::Evaluate && !config_path.empty()) {
        return run_evaluate(config_path, artifacts_path, duplicates, report_path);
    }
    if (workflow == Workflow::Finalize) {
        return run_finalize(output_path, bucket_report, training_report, equivalence_report,
            lookup_first, lookup_second,
            {operator_id, start_utc, end_utc, command, machine_report.string(), exit_code});
    }
    std::cerr << name << " requires an explicit workflow configuration; use --help\n";
    return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) noexcept {
    try {
    const std::string_view executable = argc == 0 ? "multiway" : argv[0];
    const auto slash = executable.find_last_of("/\\");
    auto name = executable.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (name.size() > 4U && name.substr(name.size() - 4U) == ".exe") {
        name.remove_suffix(4U);
    }
    if (name == "texas_multiway_train") return run("train", argc, argv);
    if (name == "texas_multiway_buckets") return run("buckets", argc, argv);
    if (name == "texas_multiway_inspect") return run("inspect", argc, argv);
    if (name == "texas_multiway_finalize") return run("finalize", argc, argv);
    return run("evaluate", argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "workflow failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "workflow failed: unknown exception\n";
        return EXIT_FAILURE;
    }
}
