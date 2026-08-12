#include "games/hunl.hpp"
#include "ranges/cache.hpp"
#include "ranges/source.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct CliConfig {
    std::filesystem::path cache_dir = std::filesystem::current_path();
    std::uint32_t iterations = 1000;
    bool benchmark = false;
};

std::optional<CliConfig> parse_args(int argc, char* argv[]) {
    CliConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--cache-dir" && i + 1 < argc) {
            cfg.cache_dir = argv[++i];
            continue;
        }
        if (arg == "--iterations" && i + 1 < argc) {
            try {
                cfg.iterations = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--benchmark") {
            cfg.benchmark = true;
            continue;
        }
        return std::nullopt;
    }
    return cfg;
}

void print_usage(const char* exe) {
    std::cerr << "Usage:\n"
              << "  " << exe << " [--cache-dir PATH] [--iterations N] [--benchmark]\n";
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        print_usage(argv[0]);
        return 1;
    }

    const auto cli = *parsed;
    const auto config = texas::benchmark_turn_subgame();
    const auto key = texas::make_range_cache_key(config, 0, texas::RangeVector::Kind::Combo);
    const auto basename = texas::range_cache_basename(key) + ".tsrcache";
    const auto path = cli.cache_dir / basename;

    texas::RangeCacheEntry entry;
    entry.key = key;
    entry.range = texas::make_uniform_canonical_range(1326, texas::RangeVector::Kind::Combo);
    entry.iterations = cli.iterations;
    entry.exploitability = 0.0;
    entry.label = "uniform-benchmark-turn";

    const auto export_start = std::chrono::steady_clock::now();
    if (!texas::save_range_cache_entry(path, entry)) {
        std::cerr << "failed to save cache entry to " << path.string() << "\n";
        return 1;
    }
    const auto export_seconds = seconds_since(export_start);

    const auto import_start = std::chrono::steady_clock::now();
    const auto loaded = texas::load_range_cache_if_compatible(
        path,
        config,
        0,
        texas::RangeVector::Kind::Combo);
    const auto import_seconds = seconds_since(import_start);

    if (!loaded.has_value()) {
        std::cerr << "failed to load compatible cache entry from " << path.string() << "\n";
        return 1;
    }

    std::cout << "range cache file: " << path.string() << "\n";
    std::cout << "exported weights: " << loaded->range.range.size() << "\n";
    std::cout << "board: " << texas::sorted_card_string(config.initial_board) << "\n";
    std::cout << "street: " << texas::street_token(config.starting_street) << "\n";
    std::cout << "iterations tag: " << loaded->iterations << "\n";
    std::cout << "export_seconds: " << export_seconds << "\n";
    std::cout << "import_seconds: " << import_seconds << "\n";

    if (cli.benchmark) {
        const auto benchmark_start = std::chrono::steady_clock::now();
        for (std::uint32_t i = 0; i < cli.iterations; ++i) {
            texas::RangeCacheEntry loop_entry = entry;
            loop_entry.iterations = i + 1U;
            if (!texas::save_range_cache_entry(path, loop_entry)) {
                std::cerr << "benchmark save failed\n";
                return 1;
            }
            const auto loop_loaded = texas::load_range_cache_if_compatible(
                path,
                config,
                0,
                texas::RangeVector::Kind::Combo);
            if (!loop_loaded.has_value()) {
                std::cerr << "benchmark load failed\n";
                return 1;
            }
        }
        std::cout << "benchmark_seconds: " << seconds_since(benchmark_start) << "\n";
    }

    return 0;
}
