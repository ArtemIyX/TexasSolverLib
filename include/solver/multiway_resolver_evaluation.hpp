#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

// Candidate modes deliberately select existing resolver fallback/search paths.
// Match scheduling remains owned by the evaluation host.
enum class MultiwayResolverEvaluationCandidateKind : std::uint8_t {
    StaticLegal,
    BlueprintOnly,
    SearchDisabled,
    SearchEnabled,
};

struct MultiwayResolverEvaluationCandidate {
    std::uint64_t id = 0U;
    MultiwayResolverEvaluationCandidateKind kind =
        MultiwayResolverEvaluationCandidateKind::StaticLegal;
};

struct MultiwayResolverEvaluationAdapterConfig {
    MultiwayResolverConfig resolver{};
    std::vector<MultiwayResolverEvaluationCandidate> candidates;

    void validate() const;
};

// One decision record returned to a callback-owned match host. It retains
// public resolver diagnostics but no request cards or ranges.
struct MultiwayResolverEvaluationDecision {
    std::uint64_t candidate_id = 0U;
    std::uint64_t sampling_seed = 0U;
    MultiwayResolverResult result{};
};

// Cold adapter for evaluation hosts. Every resolve call creates a fresh
// resolver, so stable-root state and runtime search sessions are request-local.
class MultiwayResolverEvaluationAdapter {
public:
    explicit MultiwayResolverEvaluationAdapter(MultiwayResolverEvaluationAdapterConfig config);

    [[nodiscard]] MultiwayResolverEvaluationDecision resolve(
        std::uint64_t candidate_id,
        const MultiwayResolverRequest& request,
        std::uint64_t decision_index) const;

private:
    [[nodiscard]] const MultiwayResolverEvaluationCandidate& candidate(std::uint64_t id) const;

    MultiwayResolverEvaluationAdapterConfig config_{};
};

}  // namespace texas::solver::multiway
