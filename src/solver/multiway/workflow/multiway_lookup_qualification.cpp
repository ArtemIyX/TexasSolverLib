#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"

#include "core/atomic_publish.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"

#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {

MultiwayLookupQualificationReport qualify_multiway_required_lookups(
    const MultiwayBlueprintTrainingConfig& config,
    const MultiwayRootSnapshot& root,
    const MultiwayBucketRegistry& buckets,
    const MultiwayFullBlueprintArtifact& blueprint,
    std::uint64_t trajectory_count) {
    config.validate();
    root.validate();
    blueprint.validate();
    if (trajectory_count == 0U || blueprint.identity != config.identity() ||
        buckets.identity() != config.identity()) {
        throw std::invalid_argument("multiway lookup qualification inputs are incompatible");
    }
    MultiwayBlueprintStore store(blueprint.identity, blueprint.rows);
    MultiwayBlueprintPolicyProvider provider(store);
    MultiwaySolveRequest request(root, config.cfr, config.limits);
    MultiwaySolverCoordinator coordinator(request);
    MultiwayActionAbstraction action_abstraction(config.action_abstraction);
    MultiwayRootExternalSamplingTraversal traversal(
        coordinator, root, action_abstraction, buckets, nullptr,
        config.max_decision_depth, config.max_public_chance_depth, &provider);
    MultiwayWorkerDeltaStream stream(0U, config.limits.max_worker_delta_entries);
    MultiwayBlueprintLookupAudit audit;
    for (std::uint64_t trajectory = 0U; trajectory < trajectory_count; ++trajectory) {
        stream.rewind(0U);
        if (!traversal.run(
                traversal.traverser_for_trajectory(trajectory), trajectory,
                multiway_deterministic_trajectory_seed(config.deterministic_seed, trajectory),
                stream, 1.0, nullptr, nullptr, 0U, &audit)) {
            throw std::runtime_error("multiway lookup qualification discarded a trajectory");
        }
    }
    return {audit.lookup_hits, audit.missing_infosets, audit.missing_buckets,
            audit.action_menu_mismatches, audit.fingerprint()};
}

void save_multiway_lookup_qualification_report_atomic(
    const std::filesystem::path& path, const MultiwayLookupQualificationReport& report) {
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open lookup qualification report");
    output << "{\n  \"lookup_hits\": " << report.lookup_hits
           << ",\n  \"missing_infosets\": " << report.missing_infosets
           << ",\n  \"missing_buckets\": " << report.missing_buckets
           << ",\n  \"action_menu_mismatches\": " << report.action_menu_mismatches
           << ",\n  \"replay_fingerprint\": " << report.replay_fingerprint
           << ",\n  \"second_replay_fingerprint\": " << report.second_replay_fingerprint
           << ",\n  \"passed\": " << (report.passed() ? "true" : "false") << "\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish lookup qualification report");
}
}  // namespace texas::solver::multiway
