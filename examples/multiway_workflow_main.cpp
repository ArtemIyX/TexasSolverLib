#include <cstdlib>
#include <exception>
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "core/canonical_combo.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>
#include <memory>
#include "core/atomic_publish.hpp"

namespace {

enum class Workflow { Train, Buckets, Inspect, Evaluate };

Workflow workflow_from_name(std::string_view name) {
    if (name == "train") return Workflow::Train;
    if (name == "buckets") return Workflow::Buckets;
    if (name == "inspect") return Workflow::Inspect;
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
        std::cout << "  --output <path>        Atomically published bucket artifact\n";
    } else if (workflow == Workflow::Inspect) {
        std::cout << "  --input <path>         Artifact to inspect\n"
                  << "  --report <path>        Atomically published JSON inspection report\n";
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

int run_buckets(const std::filesystem::path& config_path, const std::filesystem::path& output_path,
                const std::filesystem::path& checkpoint_dir) {
    using namespace texas::solver::multiway;
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
    std::unique_ptr<MultiwayBucketArtifactWriter> writer;
    const auto progress_path = checkpoint_dir.empty() ? std::filesystem::path{} : checkpoint_dir / "latest.progress";
    if (!checkpoint_dir.empty() && std::filesystem::exists(temporary) && std::filesystem::exists(progress_path)) {
        writer = std::make_unique<MultiwayBucketArtifactWriter>(MultiwayBucketArtifactWriter::resume(
            temporary, identity, expected_tables,
            load_multiway_bucket_progress(progress_path, identity, expected_tables)));
    } else {
        writer = std::make_unique<MultiwayBucketArtifactWriter>(temporary, identity, expected_tables);
    }
    const auto already_written = writer->progress().table_count;
    std::uint64_t global_index = 0U;
    for (const auto street : {texas::core::Street::Flop, texas::core::Street::Turn, texas::core::Street::River}) {
        MultiwayBucketBoardCatalog catalog(street);
        const auto begin = already_written > global_index ? std::min(catalog.size(), already_written - global_index) : 0U;
        catalog.for_each(begin, catalog.size(), [&](const MultiwayBucketBoardRequest& request) {
            writer->append(build_multiway_baseline_bucket_table(identity, request.street, request.canonical_board, profile));
        });
        if (!checkpoint_dir.empty()) {
            std::filesystem::create_directories(checkpoint_dir);
            save_multiway_bucket_progress_atomic(checkpoint_dir / "latest.progress", identity,
                expected_tables, writer->progress());
        }
        global_index += catalog.size();
    }
    writer->finish(output_path);
    std::cout << "published bucket artifact: tables=" << writer->progress().table_count
              << " bytes=" << writer->progress().byte_length << "\n";
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
        ",\n  \"flop_tables\": " + std::to_string(report.flop_tables) +
        ",\n  \"turn_tables\": " + std::to_string(report.turn_tables) +
        ",\n  \"river_tables\": " + std::to_string(report.river_tables) +
        ",\n  \"live_assignments\": " + std::to_string(report.live_assignments) +
        ",\n  \"payload_hash\": " + std::to_string(report.payload_hash) + "\n}\n";
    if (!report_path.empty()) {
        if (!report_path.parent_path().empty()) std::filesystem::create_directories(report_path.parent_path());
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
              const std::filesystem::path& report_path) {
    using namespace texas;
    using namespace texas::solver::multiway;
    if (batches == 0U || input_path.empty() || output_path.empty()) throw std::invalid_argument("train requires --input, --output, and --batches");
    const auto workflow = load_multiway_workflow_config(config_path);
    std::ifstream input(input_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open training bucket artifact");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    const auto buckets = deserialize_multiway_bucket_registry(bytes);
    MultiwayBlueprintTrainingConfig config;
    config.blueprint = workflow.model;
    config.bucket_profile.flop_bucket_count = workflow.model.flop_bucket_count;
    config.bucket_profile.turn_bucket_count = workflow.model.turn_bucket_count;
    config.bucket_profile.river_bucket_count = workflow.model.river_bucket_count;
    config.rules = MultiwayGameRules::standard_6max();
    config.max_decision_depth = workflow.max_decision_depth;
    config.max_public_chance_depth = workflow.max_public_chance_depth;
    config.deterministic_seed = workflow.deterministic_seed;
    config.limits.max_batches = batches;
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
    session.run_batches(batches);
    const auto& status = session.status();
    if (status.discarded_trajectories != 0U || status.preflop_rows == 0U ||
        status.flop_rows == 0U || status.turn_rows == 0U || status.river_rows == 0U ||
        status.terminal_visits == 0U) {
        throw std::runtime_error("training failed the F1 coverage or discard gate");
    }
    const auto artifact = session.export_full_policy();
    MultiwayFullBlueprintArtifacts::save_atomic(output_path, artifact);
    save_multiway_training_report_atomic(report_path.empty() ? output_path.string() + ".json" : report_path,
        make_multiway_training_report(session.status(), session.status().merged_stream_fingerprint));
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
    std::uint64_t batches = 0U;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            print_help(name, workflow);
            return EXIT_SUCCESS;
        }
        if (argument == "--tiny") return run_tiny_pipeline();
        if (argument == "--config" || argument == "--seed" || argument == "--batches" ||
            argument == "--checkpoint-dir" || argument == "--resume" || argument == "--output" ||
            argument == "--input" || argument == "--report" || argument == "--artifacts" || argument == "--duplicates") {
            if (!consume_value(index, argc, argv, argument)) {
                std::cerr << argument << " requires a value\n";
                return EXIT_FAILURE;
            }
            if (argument == "--config") config_path = argv[index];
            if (argument == "--output") output_path = argv[index];
            if (argument == "--input") input_path = argv[index];
            if (argument == "--report") report_path = argv[index];
            if (argument == "--checkpoint-dir") checkpoint_dir = argv[index];
            if (argument == "--batches") batches = std::stoull(argv[index]);
            continue;
        }
        std::cerr << "unknown option: " << argument << "\n";
        return EXIT_FAILURE;
    }
    if (workflow == Workflow::Buckets && !config_path.empty() && !output_path.empty()) {
        return run_buckets(config_path, output_path, checkpoint_dir);
    }
    if (workflow == Workflow::Inspect && !config_path.empty() && !input_path.empty()) {
        return run_inspect(config_path, input_path, report_path);
    }
    if (workflow == Workflow::Train && !config_path.empty()) {
        return run_train(config_path, input_path, output_path, batches, report_path);
    }
    std::cerr << name << " requires an explicit workflow configuration; use --help\n";
    return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) noexcept {
    try {
    const std::string_view executable = argc == 0 ? "multiway" : argv[0];
    const auto slash = executable.find_last_of("/\\");
    const auto name = executable.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (name == "texas_multiway_train") return run("train", argc, argv);
    if (name == "texas_multiway_buckets") return run("buckets", argc, argv);
    if (name == "texas_multiway_inspect") return run("inspect", argc, argv);
    return run("evaluate", argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "workflow failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "workflow failed: unknown exception\n";
        return EXIT_FAILURE;
    }
}
