#include "solver/multiway_resolver_evaluation.hpp"

#include <stdexcept>
#include <utility>

namespace core {
namespace {

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

MultiwayResolverConfig config_for(
    MultiwayResolverConfig config,
    MultiwayResolverEvaluationCandidateKind kind) {
    switch (kind) {
        case MultiwayResolverEvaluationCandidateKind::StaticLegal:
            config.buckets = nullptr;
            config.verified_blueprint = nullptr;
            config.blueprint = nullptr;
            config.full_blueprint = nullptr;
            config.search_mode = MultiwayResolverSearchMode::ForcedFallback;
            break;
        case MultiwayResolverEvaluationCandidateKind::BlueprintOnly:
            config.search_mode = MultiwayResolverSearchMode::ForcedFallback;
            break;
        case MultiwayResolverEvaluationCandidateKind::SearchDisabled:
            config.search_mode = MultiwayResolverSearchMode::LegacyStatic;
            break;
        case MultiwayResolverEvaluationCandidateKind::SearchEnabled:
            config.search_mode = MultiwayResolverSearchMode::SearchActive;
            break;
    }
    return config;
}

}  // namespace

void MultiwayResolverEvaluationAdapterConfig::validate() const {
    if (candidates.empty()) {
        throw std::invalid_argument("multiway resolver evaluation requires candidates");
    }
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        if (candidates[index].kind == MultiwayResolverEvaluationCandidateKind::BlueprintOnly &&
            resolver.verified_blueprint == nullptr && resolver.blueprint == nullptr) {
            throw std::invalid_argument("multiway blueprint-only candidate requires a blueprint artifact");
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (candidates[index].id == candidates[prior].id) {
                throw std::invalid_argument("multiway resolver evaluation candidate ids must be unique");
            }
        }
    }
}

MultiwayResolverEvaluationAdapter::MultiwayResolverEvaluationAdapter(
    MultiwayResolverEvaluationAdapterConfig config)
    : config_(std::move(config)) {
    config_.validate();
}

const MultiwayResolverEvaluationCandidate& MultiwayResolverEvaluationAdapter::candidate(
    std::uint64_t id) const {
    for (const auto& entry : config_.candidates) {
        if (entry.id == id) return entry;
    }
    throw std::invalid_argument("multiway resolver evaluation candidate is unknown");
}

MultiwayResolverEvaluationDecision MultiwayResolverEvaluationAdapter::resolve(
    std::uint64_t candidate_id,
    const MultiwayResolverRequest& request,
    std::uint64_t decision_index) const {
    const auto& selected = candidate(candidate_id);
    MultiwayResolverRequest request_copy = request;
    request_copy.sampling_seed = mix_seed(request.sampling_seed ^ decision_index);
    MultiwayResolver resolver(config_for(config_.resolver, selected.kind));

    MultiwayResolverEvaluationDecision decision;
    decision.candidate_id = candidate_id;
    decision.sampling_seed = request_copy.sampling_seed;
    decision.result = resolver.resolve(request_copy);
    return decision;
}

}  // namespace core
