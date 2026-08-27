#include <cstdlib>
#include <exception>
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

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
                  << "  --checkpoint-dir <dir> Output directory for incomplete runs\n"
                  << "  --resume <path>        Checkpoint to resume\n";
    } else if (workflow == Workflow::Buckets) {
        std::cout << "  --output <path>        Atomically published bucket artifact\n";
    } else if (workflow == Workflow::Inspect) {
        std::cout << "  --input <path>         Artifact to inspect\n";
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

bool consume_value(int& index, int argc, char** argv, std::string_view option) {
    if (std::string_view(argv[index]) != option || index + 1 >= argc) return false;
    ++index;
    return true;
}

int run(std::string_view name, int argc, char** argv) {
    const Workflow workflow = workflow_from_name(name);
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            print_help(name, workflow);
            return EXIT_SUCCESS;
        }
        if (argument == "--tiny") return run_tiny_pipeline();
        if (argument == "--config" || argument == "--seed" || argument == "--batches" ||
            argument == "--checkpoint-dir" || argument == "--resume" || argument == "--output" ||
            argument == "--input" || argument == "--artifacts" || argument == "--duplicates") {
            if (!consume_value(index, argc, argv, argument)) {
                std::cerr << argument << " requires a value\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        std::cerr << "unknown option: " << argument << "\n";
        return EXIT_FAILURE;
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
