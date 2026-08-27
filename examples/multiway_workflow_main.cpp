#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

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
