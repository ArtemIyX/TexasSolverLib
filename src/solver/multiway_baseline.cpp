#include "solver/multiway_baseline.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

#if defined(_WIN32)
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace core {
namespace {

MultiwayResolverFallbackKind fallback_kind(const MultiwayResolverDiagnostics& diagnostics) noexcept {
    if (diagnostics.used_latest_stable_root) return MultiwayResolverFallbackKind::LatestStableRoot;
    if (diagnostics.used_blueprint_fallback) return MultiwayResolverFallbackKind::Blueprint;
    if (diagnostics.used_static_fallback) return MultiwayResolverFallbackKind::StaticLegal;
    return MultiwayResolverFallbackKind::None;
}

bool same_action(const MultiwayActionDescriptor& lhs, const MultiwayActionDescriptor& rhs) noexcept {
    return lhs == rhs;
}

bool same_policy(
    const std::vector<MultiwayResolverActionProbability>& lhs,
    const std::vector<MultiwayResolverActionProbability>& rhs,
    double tolerance) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!same_action(lhs[index].action, rhs[index].action) ||
            !std::isfinite(lhs[index].probability) || !std::isfinite(rhs[index].probability) ||
            std::abs(lhs[index].probability - rhs[index].probability) > tolerance) {
            return false;
        }
    }
    return true;
}

void write_action(std::ostream& output, const MultiwayActionDescriptor& action) {
    output << static_cast<unsigned int>(action.action) << ',' << action.action_index << ','
           << action.target_street_contribution << ',' << action.action_menu_id;
}

std::ostringstream stable_output() {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::hexfloat;
    return output;
}

std::uint64_t difference(std::uint64_t after, std::uint64_t before) noexcept {
    return after >= before ? after - before : 0U;
}

}  // namespace

std::uint64_t observed_multiway_process_memory_bytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) == 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
    std::ifstream input("/proc/self/statm");
    std::uint64_t ignored_pages = 0;
    std::uint64_t resident_pages = 0;
    if (!(input >> ignored_pages >> resident_pages)) return 0U;
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || resident_pages >
            std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(page_size)) {
        return 0U;
    }
    return resident_pages * static_cast<std::uint64_t>(page_size);
#else
    return 0U;
#endif
}

MultiwayResolverBaselineFixtureHarness::MultiwayResolverBaselineFixtureHarness(
    MultiwayResolverConfig config)
    : config_(config) {
    config_.validate();
}

MultiwayResolverBaselineReport MultiwayResolverBaselineFixtureHarness::run(
    const MultiwayResolverBaselineFixture& fixture) const {
    MultiwayResolver resolver(config_);
    return record_multiway_resolver_baseline(
        fixture.kind, resolver, fixture.request, fixture.measurements);
}

MultiwayResolverBaselineReport record_multiway_resolver_baseline(
    MultiwayBaselineFixtureKind fixture,
    const MultiwayResolver& resolver,
    const MultiwayResolverRequest& request,
    MultiwayBaselineMeasurements measurements) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = resolver.resolve(request);
    measurements.elapsed_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    if (measurements.observed_memory_bytes == 0U) {
        measurements.observed_memory_bytes = observed_multiway_process_memory_bytes();
    }
    const auto& diagnostics = result.diagnostics;
    return {
        fixture,
        diagnostics.status,
        fallback_kind(diagnostics),
        result.has_sampled_action,
        result.sampled_action,
        result.policy,
        diagnostics.completed_batches,
        diagnostics.completed_trajectories,
        diagnostics.deadline_expired,
        diagnostics.policy_normalized,
        diagnostics.root_bucket,
        diagnostics.root_menu_size,
        diagnostics.resolved_public_state_id,
        measurements,
    };
}

MultiwayTraversalBaselineReport record_multiway_traversal_baseline(
    MultiwayBaselineFixtureKind fixture,
    MultiwayRootBatchRunner& runner,
    MultiwaySolverCoordinator& coordinator,
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t seed,
    double iteration_weight,
    MultiwayBaselineMeasurements measurements) {
    const auto before = coordinator.diagnostics();
    const auto start = std::chrono::steady_clock::now();
    const auto batch = runner.run(first_trajectory_id, trajectory_count, seed, iteration_weight);
    measurements.elapsed_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    if (measurements.observed_memory_bytes == 0U) {
        measurements.observed_memory_bytes = observed_multiway_process_memory_bytes();
    }
    const auto& diagnostics = coordinator.diagnostics();
    return {
        fixture,
        batch,
        difference(diagnostics.public_states_admitted, before.public_states_admitted),
        difference(diagnostics.sparse_rows_admitted, before.sparse_rows_admitted),
        difference(diagnostics.worker_delta_entries_merged, before.worker_delta_entries_merged),
        measurements,
    };
}

bool equivalent_multiway_resolver_baseline(
    const MultiwayResolverBaselineReport& lhs,
    const MultiwayResolverBaselineReport& rhs,
    double policy_absolute_tolerance) noexcept {
    if (!std::isfinite(policy_absolute_tolerance) || policy_absolute_tolerance < 0.0) return false;
    return lhs.fixture == rhs.fixture &&
        lhs.status == rhs.status &&
        lhs.fallback == rhs.fallback &&
        lhs.has_sampled_action == rhs.has_sampled_action &&
        (!lhs.has_sampled_action || same_action(lhs.sampled_action, rhs.sampled_action)) &&
        lhs.completed_batches == rhs.completed_batches &&
        lhs.completed_trajectories == rhs.completed_trajectories &&
        lhs.deadline_expired == rhs.deadline_expired &&
        lhs.policy_normalized == rhs.policy_normalized &&
        lhs.root_bucket == rhs.root_bucket &&
        lhs.root_menu_size == rhs.root_menu_size &&
        lhs.resolved_public_state_id == rhs.resolved_public_state_id &&
        same_policy(lhs.policy, rhs.policy, policy_absolute_tolerance);
}

bool equivalent_multiway_traversal_baseline(
    const MultiwayTraversalBaselineReport& lhs,
    const MultiwayTraversalBaselineReport& rhs) noexcept {
    return lhs.fixture == rhs.fixture &&
        lhs.batch.trajectories_attempted == rhs.batch.trajectories_attempted &&
        lhs.batch.trajectories_accepted == rhs.batch.trajectories_accepted &&
        lhs.batch.trajectories_discarded == rhs.batch.trajectories_discarded &&
        lhs.batch.delta_entries_merged == rhs.batch.delta_entries_merged &&
        lhs.public_states_admitted == rhs.public_states_admitted &&
        lhs.sparse_rows_admitted == rhs.sparse_rows_admitted &&
        lhs.worker_delta_entries_merged == rhs.worker_delta_entries_merged;
}

std::string serialize_multiway_resolver_baseline(const MultiwayResolverBaselineReport& report) {
    auto output = stable_output();
    output << "fixture=" << static_cast<unsigned int>(report.fixture) << '\n'
           << "status=" << static_cast<unsigned int>(report.status) << '\n'
           << "fallback=" << static_cast<unsigned int>(report.fallback) << '\n'
           << "has_sampled_action=" << report.has_sampled_action << '\n';
    if (report.has_sampled_action) {
        output << "sampled_action=";
        write_action(output, report.sampled_action);
        output << '\n';
    }
    output << "completed_batches=" << report.completed_batches << '\n'
           << "completed_trajectories=" << report.completed_trajectories << '\n'
           << "deadline_expired=" << report.deadline_expired << '\n'
           << "policy_normalized=" << report.policy_normalized << '\n'
           << "root_bucket=" << report.root_bucket << '\n'
           << "root_menu_size=" << report.root_menu_size << '\n'
           << "resolved_public_state_id=" << report.resolved_public_state_id << '\n'
           << "policy_count=" << report.policy.size() << '\n';
    for (const auto& entry : report.policy) {
        output << "policy=";
        write_action(output, entry.action);
        output << ',' << entry.probability << '\n';
    }
    return output.str();
}

std::string serialize_multiway_traversal_baseline(const MultiwayTraversalBaselineReport& report) {
    auto output = stable_output();
    output << "fixture=" << static_cast<unsigned int>(report.fixture) << '\n'
           << "trajectories_attempted=" << report.batch.trajectories_attempted << '\n'
           << "trajectories_accepted=" << report.batch.trajectories_accepted << '\n'
           << "trajectories_discarded=" << report.batch.trajectories_discarded << '\n'
           << "delta_entries_merged=" << report.batch.delta_entries_merged << '\n'
           << "public_states_admitted=" << report.public_states_admitted << '\n'
           << "sparse_rows_admitted=" << report.sparse_rows_admitted << '\n'
           << "worker_delta_entries_merged=" << report.worker_delta_entries_merged << '\n';
    return output.str();
}

}  // namespace core
