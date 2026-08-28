#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "core/fingerprint.hpp"

#include <fstream>
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
    if (schema_version != 1U || profile_id.empty() || preflop_classes != 169U || storage_backend != "CompactInt32" ||
        max_decision_depth != 64U || max_public_chance_depth != 3U || deterministic_seed != 1U ||
        reference_worker_count != 1U || target_trajectories == 0U) throw std::invalid_argument("invalid F1 workflow configuration");
    model.validate();
    if (model.player_count != 6U || model.initial_stack_chips != 10000 || model.small_blind_chips != 50 ||
        model.big_blind_chips != 100 || model.ante_chips != 0 || model.flop_bucket_count != 12U ||
        model.turn_bucket_count != 12U || model.river_bucket_count != 12U) throw std::invalid_argument("F1 model profile mismatch");
}

std::uint64_t MultiwayWorkflowConfig::fingerprint() const noexcept {
    auto hash = core::fingerprint::FNV1A_OFFSET;
    const auto append = [&hash](std::uint64_t value) noexcept { core::fingerprint::append_u64(hash, value); };
    append(schema_version);
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
    append(reference_worker_count); append(target_trajectories);
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
        else if (key == "reference_worker_count") result.reference_worker_count = static_cast<std::uint32_t>(n());
        else if (key == "target_trajectories") result.target_trajectories = n();
        else if (key == "maximum_public_states" || key == "maximum_sparse_rows" ||
                 key == "maximum_sparse_values" || key == "worker_delta_capacity" ||
                 key == "trajectories_per_batch" || key == "checkpoint_interval" ||
                 key == "disk_space_requirement_bytes" || key == "process_memory_limit_bytes") {
            if (value != "UNRESOLVED") throw std::invalid_argument("pilot capacity must be UNRESOLVED: " + key);
        } else throw std::invalid_argument("unknown configuration key: " + key);
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
