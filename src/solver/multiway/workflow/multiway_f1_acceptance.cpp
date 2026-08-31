#include "solver/multiway/workflow/multiway_f1_acceptance.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <charconv>
#include <cctype>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace texas::solver::multiway {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

void require_identity(const MultiwayEvidenceHeader& header, std::uint32_t schema) {
    header.validate(schema);
    require(header.profile_kind == MultiwayWorkflowProfileKind::Acceptance,
        "F1 acceptance evidence cannot use a sizing profile");
}

std::string read_report(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("F1 evidence report path is empty");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open F1 evidence report");
    std::string text((std::istreambuf_iterator<char>(input)), {});
    if (text.empty()) throw std::invalid_argument("F1 evidence report is empty");
    return text;
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::string json_value(const std::string& text, std::string_view key) {
    const std::string marker = std::string("\"") + std::string(key) + "\"";
    const auto key_position = text.find(marker);
    if (key_position == std::string::npos) {
        throw std::invalid_argument("F1 evidence report is missing a required field");
    }
    auto position = text.find(':', key_position + marker.size());
    if (position == std::string::npos) throw std::invalid_argument("malformed F1 evidence report");
    ++position;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    if (position == text.size()) throw std::invalid_argument("malformed F1 evidence report");
    if (text[position] == '"') {
        const auto end = text.find('"', position + 1U);
        if (end == std::string::npos) throw std::invalid_argument("malformed F1 evidence string");
        return text.substr(position + 1U, end - position - 1U);
    }
    auto end = position;
    while (end < text.size() && text[end] != ',' && text[end] != '}' &&
        !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
    return text.substr(position, end - position);
}

std::uint64_t json_u64(const std::string& text, std::string_view key) {
    const auto value = json_value(text, key);
    std::uint64_t result = 0U;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("F1 evidence numeric field is invalid");
    }
    return result;
}

bool json_bool(const std::string& text, std::string_view key) {
    const auto value = json_value(text, key);
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::invalid_argument("F1 evidence boolean field is invalid");
}

MultiwayEvidenceHeader parse_header(const std::string& text, std::uint32_t schema) {
    MultiwayEvidenceHeader header;
    header.schema_version = static_cast<std::uint32_t>(json_u64(text, "schema_version"));
    header.profile_kind = static_cast<MultiwayWorkflowProfileKind>(
        json_u64(text, "profile_kind"));
    header.producer.schema_version = static_cast<std::uint32_t>(
        json_u64(text, "producer_schema_version"));
    header.producer.git_commit = json_value(text, "git_commit");
    header.producer.build_configuration = json_value(text, "build_configuration");
    header.producer.compiler_identity = json_value(text, "compiler_identity");
    header.producer.workflow_config_fingerprint = json_u64(text, "workflow_config_fingerprint");
    auto& identity = header.producer.model_identity;
    identity.combined_hash = json_u64(text, "model_identity");
    identity.rules_hash = json_u64(text, "rules_hash");
    identity.rules_schema_hash = json_u64(text, "rules_schema_hash");
    identity.action_abstraction_hash = json_u64(text, "action_abstraction_hash");
    identity.bucket_model_hash = json_u64(text, "bucket_model_hash");
    identity.terminal_model_hash = json_u64(text, "terminal_model_hash");
    identity.resolver_schema_hash = json_u64(text, "resolver_schema_hash");
    identity.code_schema_hash = json_u64(text, "code_schema_hash");
    identity.range_semantics_hash = json_u64(text, "range_semantics_hash");
    identity.future_bucket_model_hash = json_u64(text, "future_bucket_model_hash");
    identity.off_tree_policy_hash = json_u64(text, "off_tree_policy_hash");
    identity.continuation_policy_hash = json_u64(text, "continuation_policy_hash");
    identity.runtime_search_schema_hash = json_u64(text, "runtime_search_schema_hash");
    header.producer.artifact_schemas.bucket_artifact = static_cast<std::uint32_t>(
        json_u64(text, "bucket_artifact_schema"));
    header.producer.artifact_schemas.blueprint_artifact = static_cast<std::uint32_t>(
        json_u64(text, "blueprint_artifact_schema"));
    header.producer.artifact_schemas.checkpoint_artifact = static_cast<std::uint32_t>(
        json_u64(text, "checkpoint_artifact_schema"));
    header.validate(schema);
    return header;
}

MultiwayF1BucketEvidence load_bucket(const std::filesystem::path& path) {
    const auto text = read_report(path);
    MultiwayF1BucketEvidence result;
    result.header = parse_header(text, MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION);
    result.identity_matches = json_bool(text, "identity_matches");
    result.payload_hash_matches = json_bool(text, "payload_hash_matches");
    result.flop_tables = json_u64(text, "flop_tables");
    result.turn_tables = json_u64(text, "turn_tables");
    result.river_tables = json_u64(text, "river_tables");
    return result;
}

MultiwayF1TrainingEvidence load_training(const std::filesystem::path& path) {
    const auto text = read_report(path);
    MultiwayF1TrainingEvidence result;
    result.header = parse_header(text, MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
    result.started_at_preflop = json_bool(text, "started_at_preflop");
    result.preflop_rows = json_u64(text, "preflop_rows");
    result.flop_rows = json_u64(text, "flop_rows");
    result.turn_rows = json_u64(text, "turn_rows");
    result.river_rows = json_u64(text, "river_rows");
    result.terminal_visits = json_u64(text, "terminal_visits");
    result.accepted_trajectories = json_u64(text, "accepted_trajectories");
    result.discarded_trajectories = json_u64(text, "discarded_trajectories");
    result.peak_rss_bytes = json_u64(text, "peak_process_rss_bytes");
    result.process_memory_limit_bytes = json_u64(text, "process_memory_limit_bytes");
    result.peak_rss_available = json_bool(text, "process_rss_available");
    return result;
}

MultiwayF1ResumeEvidence load_resume(const std::filesystem::path& path) {
    const auto text = read_report(path);
    MultiwayF1ResumeEvidence result;
    result.header = parse_header(text, MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION);
    result.blueprint_payload_equal = json_bool(text, "blueprint_payload_hash_equal");
    result.coverage_equal = json_bool(text, "coverage_equal");
    result.merge_fingerprint_equal = json_bool(text, "merge_fingerprint_equal");
    return result;
}

MultiwayF1LookupEvidence load_lookup(const std::filesystem::path& path) {
    const auto text = read_report(path);
    MultiwayF1LookupEvidence result;
    result.header = parse_header(text, MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION);
    result.missing_infosets = json_u64(text, "missing_infosets");
    result.missing_buckets = json_u64(text, "missing_buckets");
    result.action_menu_mismatches = json_u64(text, "action_menu_mismatches");
    result.first_replay_fingerprint = json_u64(text, "replay_fingerprint");
    result.second_replay_fingerprint = json_u64(text, "second_replay_fingerprint");
    return result;
}

}  // namespace

void MultiwayF1BucketEvidence::validate() const {
    require_identity(header, MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION);
    require(identity_matches && payload_hash_matches, "bucket evidence identity gate failed");
    require(flop_tables == 22100U && turn_tables == 270725U && river_tables == 2598960U,
        "bucket evidence table counts are incomplete");
}

void MultiwayF1TrainingEvidence::validate() const {
    require_identity(header, MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
    require(started_at_preflop && preflop_rows > 0U && flop_rows > 0U && turn_rows > 0U &&
        river_rows > 0U && terminal_visits > 0U && accepted_trajectories == 50'000'000U &&
        discarded_trajectories == 0U, "training evidence coverage gate failed");
    require(process_memory_limit_bytes != 0U &&
        (!peak_rss_available || peak_rss_bytes <= process_memory_limit_bytes),
        "training evidence memory gate failed");
}

void MultiwayF1ResumeEvidence::validate() const {
    require_identity(header, MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION);
    require(blueprint_payload_equal && coverage_equal && merge_fingerprint_equal,
        "checkpoint equivalence gate failed");
}

void MultiwayF1LookupEvidence::validate() const {
    require_identity(header, MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION);
    require(missing_infosets == 0U && missing_buckets == 0U && action_menu_mismatches == 0U &&
        first_replay_fingerprint != 0U && first_replay_fingerprint == second_replay_fingerprint,
        "lookup qualification gate failed");
}

void MultiwayF1HumanRunRecord::validate() const {
    require(!operator_id.empty() && !start_utc.empty() && !end_utc.empty() &&
        !command.empty() && !machine_report.empty() && exit_code == 0,
        "human F1 run provenance is incomplete");
}

void MultiwayF1AcceptanceEvidence::validate() const {
    require_identity(header, MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION);
    bucket.validate();
    training.validate();
    resume.validate();
    lookup.validate();
    human_run.validate();
    validate_matching_producer_identity(header.producer, bucket.header.producer);
    validate_matching_producer_identity(header.producer, training.header.producer);
    validate_matching_producer_identity(header.producer, resume.header.producer);
    validate_matching_producer_identity(header.producer, lookup.header.producer);
}

void MultiwayF1AcceptanceReport::validate() const {
    if (schema_version != MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION ||
        failed_predicate_count != 0U || !passed) {
        throw std::invalid_argument("F1 acceptance report is not a passing report");
    }
    evidence.validate(MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION);
    human_run.validate();
    for (const auto& source_report : source_reports) {
        require(!source_report.empty(), "F1 acceptance source report path is incomplete");
    }
}

MultiwayF1AcceptanceReport evaluate_multiway_f1_acceptance(
    const MultiwayF1AcceptanceEvidence& evidence) {
    evidence.validate();
    MultiwayF1AcceptanceReport report;
    report.schema_version = MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION;
    report.passed = true;
    report.failed_predicate_count = 0U;
    report.evidence = evidence.header;
    report.human_run = evidence.human_run;
    return report;
}

MultiwayF1AcceptanceEvidence load_multiway_f1_acceptance_evidence(
    const MultiwayF1AcceptanceInputPaths& paths) {
    const auto bucket = load_bucket(paths.bucket_inspection_report);
    const auto training = load_training(paths.training_report);
    const auto resume = load_resume(paths.checkpoint_equivalence_report);
    const auto first_lookup = load_lookup(paths.first_lookup_report);
    const auto second_lookup = load_lookup(paths.second_lookup_report);
    MultiwayF1AcceptanceEvidence result;
    result.header = {
        MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        bucket.header.producer};
    result.bucket = bucket;
    result.training = training;
    result.resume = resume;
    result.lookup = first_lookup;
    validate_matching_producer_identity(first_lookup.header.producer,
        second_lookup.header.producer);
    require(second_lookup.missing_infosets == 0U && second_lookup.missing_buckets == 0U &&
        second_lookup.action_menu_mismatches == 0U &&
        second_lookup.first_replay_fingerprint != 0U,
        "second lookup qualification report failed");
    result.lookup.second_replay_fingerprint = second_lookup.first_replay_fingerprint;
    result.human_run = paths.human_run;
    return result;
}

MultiwayF1AcceptanceReport finalize_multiway_f1_acceptance(
    const MultiwayF1AcceptanceInputPaths& paths) {
    auto report = evaluate_multiway_f1_acceptance(load_multiway_f1_acceptance_evidence(paths));
    report.source_reports = {
        paths.bucket_inspection_report.string(),
        paths.training_report.string(),
        paths.checkpoint_equivalence_report.string(),
        paths.first_lookup_report.string(),
        paths.second_lookup_report.string()};
    return report;
}

void finalize_multiway_f1_acceptance_atomic(
    const std::filesystem::path& output_path,
    const MultiwayF1AcceptanceInputPaths& paths) {
    save_multiway_f1_acceptance_atomic(output_path, finalize_multiway_f1_acceptance(paths));
}

void save_multiway_f1_acceptance_atomic(
    const std::filesystem::path& path,
    const MultiwayF1AcceptanceReport& report) {
    report.validate();
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open F1 acceptance report");
    output << "{\n  \"schema_version\": " << report.schema_version
           << ",\n  \"passed\": true"
           << ",\n  \"failed_predicate_count\": 0"
           << ",\n  \"evidence\": " << serialize_multiway_evidence_header(report.evidence)
           << ",\n  \"human_run\": {\n    \"operator\": \""
           << json_escape(report.human_run.operator_id)
           << "\",\n    \"start_utc\": \"" << json_escape(report.human_run.start_utc)
           << "\",\n    \"end_utc\": \"" << json_escape(report.human_run.end_utc)
           << "\",\n    \"command\": \"" << json_escape(report.human_run.command)
           << "\",\n    \"machine_report\": \"" << json_escape(report.human_run.machine_report)
           << "\",\n    \"exit_code\": " << report.human_run.exit_code
           << "\n  },\n  \"source_reports\": [\n";
    for (std::size_t index = 0U; index < report.source_reports.size(); ++index) {
        output << "    \"" << json_escape(report.source_reports[index]) << "\"";
        if (index + 1U != report.source_reports.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish F1 acceptance report");
}

}  // namespace texas::solver::multiway
