#include "util/profiling.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <limits>
#include <thread>
#include <unordered_map>
#include <vector>

namespace core::profiling {
namespace {

using clock = std::chrono::steady_clock;

struct ThreadBucket {
    std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> timings_ns;
};

std::mutex& mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::thread::id, ThreadBucket>& buckets() {
    static std::unordered_map<std::thread::id, ThreadBucket> map;
    return map;
}

std::string read_env_value(const char* name) {
#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) {
        return {};
    }
    std::string value(raw);
    free(raw);
    return value;
#else
    const char* raw = std::getenv(name);
    return raw != nullptr ? std::string(raw) : std::string{};
#endif
}

bool& enabled_flag() {
    static bool value = [] {
        const auto raw = read_env_value("TEXASSOLVER_PROFILE");
        if (raw.empty()) {
            return false;
        }
        std::string s(raw);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return !(s == "0" || s == "false" || s == "off" || s.empty());
    }();
    static const bool registered = [] {
        std::atexit(write_report);
        return true;
    }();
    (void)registered;
    return value;
}

bool& detail_enabled_flag() {
    static bool value = [] {
        const auto raw = read_env_value("TEXASSOLVER_PROFILE_DETAIL");
        if (raw.empty()) {
            return false;
        }
        std::string s(raw);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return !(s == "0" || s == "false" || s == "off" || s.empty());
    }();
    return value;
}

std::filesystem::path report_dir() {
    const auto raw = read_env_value("TEXASSOLVER_PROFILE_DIR");
    if (!raw.empty()) {
        return std::filesystem::path(raw);
    }
    return std::filesystem::path("artifacts") / "prof";
}

std::string timestamp_name() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_MSC_VER)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

double ns_to_ms(std::uint64_t ns) {
    return static_cast<double>(ns) / 1'000'000.0;
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void print_group(
    std::string_view title,
    const std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint64_t>>>& totals,
    std::string_view prefix,
    std::size_t max_rows = std::numeric_limits<std::size_t>::max()) {
    std::cout << "\n[TexasSolver profiler] " << title << "\n";
    std::cout << std::left << std::setw(34) << "name"
              << std::right << std::setw(14) << "total_ms"
              << std::setw(12) << "calls"
              << std::setw(14) << "avg_us"
              << "\n";

    std::size_t printed = 0;
    std::uint64_t matched_total_ns = 0;
    std::uint64_t matched_calls = 0;
    std::uint64_t min_total_ns = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_total_ns = 0;
    std::size_t shown = 0;
    for (const auto& [name, entry] : totals) {
        if (!prefix.empty() && !starts_with(name, prefix)) {
            continue;
        }
        matched_total_ns += entry.first;
        matched_calls += entry.second;
        min_total_ns = std::min(min_total_ns, entry.first);
        max_total_ns = std::max(max_total_ns, entry.first);
        if (shown >= max_rows) {
            ++printed;
            continue;
        }
        const auto total_ms = ns_to_ms(entry.first);
        const auto calls = entry.second;
        const auto avg_us = calls > 0 ? static_cast<double>(entry.first) / static_cast<double>(calls) / 1000.0 : 0.0;
        std::cout << std::left << std::setw(34) << name
                  << std::right << std::setw(14) << std::fixed << std::setprecision(3) << total_ms
                  << std::setw(12) << calls
                  << std::setw(14) << std::fixed << std::setprecision(3) << avg_us
                  << "\n";
        ++printed;
        ++shown;
    }
    if (printed == 0) {
        std::cout << "(no timers)\n";
    } else {
        const auto total_ms = ns_to_ms(matched_total_ns);
        const auto avg_ms = printed > 0 ? ns_to_ms(matched_total_ns / printed) : 0.0;
        const auto min_ms = ns_to_ms(min_total_ns);
        const auto max_ms = ns_to_ms(max_total_ns);
        const auto avg_us = matched_calls > 0
            ? static_cast<double>(matched_total_ns) / static_cast<double>(matched_calls) / 1000.0
            : 0.0;
        std::cout << std::left << std::setw(34) << "accounted_total"
                  << std::right << std::setw(14) << std::fixed << std::setprecision(3) << total_ms
                  << std::setw(12) << matched_calls
                  << std::setw(14) << std::fixed << std::setprecision(3) << avg_us
                  << "  [avg_ms=" << std::fixed << std::setprecision(3) << avg_ms
                  << ", min_ms=" << min_ms
                  << ", max_ms=" << max_ms
                  << "]\n";
    }
}

}  // namespace

void write_report();
void print_profiler_report();

bool enabled() noexcept {
    return enabled_flag();
}

bool detail_enabled() noexcept {
    return enabled_flag() && detail_enabled_flag();
}

void mark(std::string_view name, double seconds) noexcept {
    if (!enabled_flag()) {
        return;
    }
    const auto ns = static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
    std::lock_guard<std::mutex> lock(mutex());
    auto& bucket = buckets()[std::this_thread::get_id()];
    auto& entry = bucket.timings_ns[std::string(name)];
    entry.first += ns;
    entry.second += 1;
}

void mark_with_calls(std::string_view name, double seconds, std::uint64_t calls) noexcept {
    if (!enabled_flag()) {
        return;
    }
    const auto ns = static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
    std::lock_guard<std::mutex> lock(mutex());
    auto& bucket = buckets()[std::this_thread::get_id()];
    auto& entry = bucket.timings_ns[std::string(name)];
    entry.first += ns;
    entry.second += calls;
}

ScopedTimer::ScopedTimer(std::string_view name) noexcept : name_(name) {
    if (enabled_flag()) {
        start_ns_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
    }
}

ScopedTimer::~ScopedTimer() {
    if (!enabled_flag() || start_ns_ == 0) {
        return;
    }
    const auto end_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
    const auto elapsed_ns = end_ns - start_ns_;
    mark(name_, static_cast<double>(elapsed_ns) / 1'000'000'000.0);
}

void write_report() {
    if (!enabled_flag()) {
        return;
    }
    static bool written = false;
    if (written) {
        return;
    }
    written = true;

    std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint64_t>>> totals;
    {
        std::lock_guard<std::mutex> lock(mutex());
        std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> merged;
        for (const auto& [thread_id, bucket] : buckets()) {
            for (const auto& [name, entry] : bucket.timings_ns) {
                auto& total = merged[name];
                total.first += entry.first;
                total.second += entry.second;
            }
        }
        totals.reserve(merged.size());
        for (auto& [name, entry] : merged) {
            totals.emplace_back(std::move(name), entry);
        }
    }

    std::sort(totals.begin(), totals.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second.first > rhs.second.first;
    });

    const auto dir = report_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / (timestamp_name() + ".prof");
    std::ofstream out(path, std::ios::binary);
    out << "TexasSolver profiling report\n";
    out << "profile_enabled=1\n";
    out << "thread_count=" << buckets().size() << "\n";
    out << "name,total_ms,calls,avg_us\n";
    for (const auto& [name, entry] : totals) {
        const auto total_ms = ns_to_ms(entry.first);
        const auto calls = entry.second;
        const auto avg_us = calls > 0 ? static_cast<double>(entry.first) / static_cast<double>(calls) / 1000.0 : 0.0;
        out << name << ',' << std::fixed << std::setprecision(3) << total_ms << ','
            << calls << ',' << std::setprecision(3) << avg_us << '\n';
    }

    print_profiler_report();
}

void print_profiler_report() {
    if (!enabled_flag()) {
        return;
    }

    std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint64_t>>> totals;
    {
        std::lock_guard<std::mutex> lock(mutex());
        std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> merged;
        for (const auto& [thread_id, bucket] : buckets()) {
            (void)thread_id;
            for (const auto& [name, entry] : bucket.timings_ns) {
                auto& total = merged[name];
                total.first += entry.first;
                total.second += entry.second;
            }
        }
        totals.reserve(merged.size());
        for (auto& [name, entry] : merged) {
            totals.emplace_back(std::move(name), entry);
        }
    }

    std::sort(totals.begin(), totals.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second.first > rhs.second.first;
    });

    print_group("Top Hotspots", totals, "", 24);
    print_group("Benchmark Wallclock", totals, "hunl.bench.");
    print_group("Flat Solve Wallclock", totals, "hunl_flat.solve.");
    print_group("Flat Stage Wallclock", totals, "hunl_flat.stage.");
    print_group("Expected Value Detail", totals, "hunl.eval.expected_value.");
    print_group("Flat Reach Detail", totals, "hunl_flat.reach.");
    print_group("Flat Backward Detail", totals, "hunl_flat.backward.");
    print_group("Flat Worker Detail", totals, "hunl_flat.worker.");
}

}  // namespace core::profiling
