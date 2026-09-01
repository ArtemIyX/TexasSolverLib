#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}
std::uint64_t number(const std::string& value, const char* key) {
    if (value.empty() || value[0] == '-') throw std::invalid_argument(std::string("invalid value for ") + key);
    std::size_t consumed = 0U;
    const auto result = std::stoull(value, &consumed, 10);
    if (consumed != value.size()) throw std::invalid_argument(std::string("invalid value for ") + key);
    return result;
}
void expect(std::uint64_t actual, std::uint64_t expected, const char* key) {
    if (actual != expected) throw std::invalid_argument(std::string("invalid value for ") + key);
}
}  // namespace

void MultiwayWorkflowConfig::validate() const {
    if ((schema_version != 1U && schema_version != 2U) || profile_id.empty() || preflop_classes != 169U || storage_backend != "CompactInt32" ||
        (profile_kind != MultiwayWorkflowProfileKind::Sizing &&
         profile_kind != MultiwayWorkflowProfileKind::Acceptance) ||
        max_decision_depth != 64U || max_public_chance_depth != 3U || deterministic_seed != 1U ||
        training_worker_count == 0U || training_worker_count > 16U ||
        (schema_version == 1U && training_worker_count != 1U) || target_trajectories == 0U ||
        (profile_kind == MultiwayWorkflowProfileKind::Acceptance && target_trajectories != 50'000'000U) ||
        (profile_kind == MultiwayWorkflowProfileKind::Sizing && target_trajectories >= 50'000'000U)) {
        throw std::invalid_argument("invalid F1 workflow configuration");
    }
    model.validate();
    if (model.player_count != 6U || model.initial_stack_chips != 10000 || model.small_blind_chips != 50 ||
        model.big_blind_chips != 100 || model.ante_chips != 0 || model.flop_bucket_count != 12U ||
        model.turn_bucket_count != 12U || model.river_bucket_count != 12U) throw std::invalid_argument("F1 model profile mismatch");
    const auto any_capacity = maximum_public_states != 0U || maximum_sparse_rows != 0U ||
        maximum_sparse_values != 0U || worker_delta_capacity != 0U || trajectories_per_batch != 0U ||
        checkpoint_interval != 0U || disk_space_requirement_bytes != 0U || process_memory_limit_bytes != 0U;
    if (any_capacity && !capacities_resolved()) {
        throw std::invalid_argument("F1 workflow capacities must be fully resolved together");
    }
    if (capacities_resolved() &&
        (trajectories_per_batch > std::numeric_limits<std::uint32_t>::max() ||
         maximum_public_states > std::numeric_limits<std::size_t>::max() ||
         maximum_sparse_rows > std::numeric_limits<std::size_t>::max() ||
         maximum_sparse_values > std::numeric_limits<std::size_t>::max() ||
         worker_delta_capacity > std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("F1 workflow capacity exceeds the runtime type");
    }
}

bool MultiwayWorkflowConfig::capacities_resolved() const noexcept {
    return maximum_public_states != 0U && maximum_sparse_rows != 0U &&
        maximum_sparse_values != 0U && worker_delta_capacity != 0U &&
        trajectories_per_batch != 0U && checkpoint_interval != 0U &&
        disk_space_requirement_bytes != 0U && process_memory_limit_bytes != 0U;
}

MultiwayTrainingWorkerResolution resolve_multiway_training_workers(
    std::uint32_t configured_workers,
    std::uint32_t override_workers,
    std::uint64_t trajectories_per_batch) {
    const auto requested = override_workers == 0U ? configured_workers : override_workers;
    if (requested == 0U || requested > 16U || trajectories_per_batch == 0U) {
        throw std::invalid_argument("training worker count must be in [1, 16] with a positive batch size");
    }
    return {requested, static_cast<std::uint32_t>(
        std::min<std::uint64_t>(requested, trajectories_per_batch))};
}

std::uint64_t MultiwayWorkflowConfig::fingerprint() const noexcept {
    auto hash = core::fingerprint::FNV1A_OFFSET;
    const auto append = [&hash](std::uint64_t value) noexcept { core::fingerprint::append_u64(hash, value); };
    append(schema_version);
    append(static_cast<std::uint64_t>(profile_kind));
    for (const auto character : profile_id) core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
    append(model.player_count);
    append(static_cast<std::uint64_t>(model.initial_stack_chips));
    append(static_cast<std::uint64_t>(model.small_blind_chips));
    append(static_cast<std::uint64_t>(model.big_blind_chips));
    append(static_cast<std::uint64_t>(model.ante_chips));
    append(model.rake_policy.identity());
    append(preflop_classes);
    for (const auto character : storage_backend) core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
    append(model.flop_bucket_count); append(model.turn_bucket_count); append(model.river_bucket_count);
    append(max_decision_depth); append(max_public_chance_depth); append(deterministic_seed);
    append(training_worker_count); append(target_trajectories);
    append(maximum_public_states); append(maximum_sparse_rows); append(maximum_sparse_values);
    append(worker_delta_capacity); append(trajectories_per_batch); append(checkpoint_interval);
    append(disk_space_requirement_bytes); append(process_memory_limit_bytes);
    return core::fingerprint::finish(hash);
}

MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text) {
    MultiwayWorkflowConfig result;
    std::set<std::string> seen;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) throw std::invalid_argument("configuration line requires '='");
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1U));
        if (!seen.insert(key).second) throw std::invalid_argument("duplicate configuration key: " + key);
        const auto n = [&] { return number(value, key.c_str()); };
        if (key == "schema_version") result.schema_version = static_cast<std::uint32_t>(n());
        else if (key == "profile_kind") {
            if (value == "sizing") result.profile_kind = MultiwayWorkflowProfileKind::Sizing;
            else if (value == "acceptance") result.profile_kind = MultiwayWorkflowProfileKind::Acceptance;
            else throw std::invalid_argument("invalid profile_kind");
        }
        else if (key == "profile_id") result.profile_id = value;
        else if (key == "players") result.model.player_count = static_cast<std::uint8_t>(n());
        else if (key == "initial_stack_chips") result.model.initial_stack_chips = static_cast<int>(n());
        else if (key == "small_blind_chips") result.model.small_blind_chips = static_cast<int>(n());
        else if (key == "big_blind_chips") result.model.big_blind_chips = static_cast<int>(n());
        else if (key == "ante_chips") result.model.ante_chips = static_cast<int>(n());
        else if (key == "rake") expect(n(), 0U, "rake");
        else if (key == "preflop_classes") result.preflop_classes = static_cast<std::uint32_t>(n());
        else if (key == "flop_buckets") result.model.flop_bucket_count = static_cast<std::uint32_t>(n());
        else if (key == "turn_buckets") result.model.turn_bucket_count = static_cast<std::uint32_t>(n());
        else if (key == "river_buckets") result.model.river_bucket_count = static_cast<std::uint32_t>(n());
        else if (key == "storage_backend") result.storage_backend = value;
        else if (key == "max_decision_depth") result.max_decision_depth = static_cast<std::uint32_t>(n());
        else if (key == "max_public_chance_depth") result.max_public_chance_depth = static_cast<std::uint32_t>(n());
        else if (key == "deterministic_seed") result.deterministic_seed = n();
        else if (key == "reference_worker_count" || key == "training_worker_count") {
            const auto workers = n();
            if (workers > 16U) throw std::invalid_argument("training worker count exceeds 16");
            result.training_worker_count = static_cast<std::uint32_t>(workers);
        }
        else if (key == "target_trajectories") result.target_trajectories = n();
        else if (key == "maximum_public_states" || key == "maximum_sparse_rows" ||
                 key == "maximum_sparse_values" || key == "worker_delta_capacity" ||
                 key == "trajectories_per_batch" || key == "checkpoint_interval" ||
                 key == "disk_space_requirement_bytes" || key == "process_memory_limit_bytes") {
            const auto capacity = value == "UNRESOLVED" ? 0U : number(value, key.c_str());
            if (key == "maximum_public_states") result.maximum_public_states = capacity;
            else if (key == "maximum_sparse_rows") result.maximum_sparse_rows = capacity;
            else if (key == "maximum_sparse_values") result.maximum_sparse_values = capacity;
            else if (key == "worker_delta_capacity") result.worker_delta_capacity = capacity;
            else if (key == "trajectories_per_batch") result.trajectories_per_batch = capacity;
            else if (key == "checkpoint_interval") result.checkpoint_interval = capacity;
            else if (key == "disk_space_requirement_bytes") result.disk_space_requirement_bytes = capacity;
            else result.process_memory_limit_bytes = capacity;
        } else throw std::invalid_argument("unknown configuration key: " + key);
    }
    if (result.schema_version == 1U) {
        if (seen.count("reference_worker_count") == 0U || seen.count("training_worker_count") != 0U) {
            throw std::invalid_argument("schema 1 requires reference_worker_count");
        }
    } else if (result.schema_version == 2U) {
        if (seen.count("training_worker_count") == 0U || seen.count("reference_worker_count") != 0U) {
            throw std::invalid_argument("schema 2 requires training_worker_count");
        }
    }
    result.validate();
    return result;
}

MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open workflow configuration");
    return parse_multiway_workflow_config({std::istreambuf_iterator<char>(input), {}});
}
}  // namespace texas::solver::multiway
