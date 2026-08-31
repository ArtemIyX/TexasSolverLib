#pragma once

#include "solver/multiway/workflow/multiway_checkpoint_equivalence.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace texas::solver::multiway {

struct MultiwayF1BucketEvidence {
    MultiwayEvidenceHeader header{
        MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    bool identity_matches = false;
    bool payload_hash_matches = false;
    std::uint64_t flop_tables = 0U;
    std::uint64_t turn_tables = 0U;
    std::uint64_t river_tables = 0U;

    void validate() const;
};

struct MultiwayF1TrainingEvidence {
    MultiwayEvidenceHeader header{
        MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    bool started_at_preflop = false;
    std::uint64_t preflop_rows = 0U;
    std::uint64_t flop_rows = 0U;
    std::uint64_t turn_rows = 0U;
    std::uint64_t river_rows = 0U;
    std::uint64_t terminal_visits = 0U;
    std::uint64_t accepted_trajectories = 0U;
    std::uint64_t discarded_trajectories = 0U;
    std::uint64_t peak_rss_bytes = 0U;
    std::uint64_t process_memory_limit_bytes = 0U;
    bool peak_rss_available = false;

    void validate() const;
};

struct MultiwayF1ResumeEvidence {
    MultiwayEvidenceHeader header{
        MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    bool blueprint_payload_equal = false;
    bool coverage_equal = false;
    bool merge_fingerprint_equal = false;

    void validate() const;
};

struct MultiwayF1LookupEvidence {
    MultiwayEvidenceHeader header{
        MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    std::uint64_t missing_infosets = 0U;
    std::uint64_t missing_buckets = 0U;
    std::uint64_t action_menu_mismatches = 0U;
    std::uint64_t first_replay_fingerprint = 0U;
    std::uint64_t second_replay_fingerprint = 0U;

    void validate() const;
};

struct MultiwayF1HumanRunRecord {
    std::string operator_id;
    std::string start_utc;
    std::string end_utc;
    std::string command;
    std::string machine_report;
    std::int32_t exit_code = -1;

    void validate() const;
};

struct MultiwayF1AcceptanceEvidence {
    MultiwayEvidenceHeader header{
        MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    MultiwayF1BucketEvidence bucket{};
    MultiwayF1TrainingEvidence training{};
    MultiwayF1ResumeEvidence resume{};
    MultiwayF1LookupEvidence lookup{};
    MultiwayF1HumanRunRecord human_run{};

    void validate() const;
};

struct MultiwayF1AcceptanceReport {
    std::uint32_t schema_version = MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION;
    bool passed = false;
    std::uint32_t failed_predicate_count = 0U;
    MultiwayEvidenceHeader evidence{};
    MultiwayF1HumanRunRecord human_run{};
    std::array<std::string, 5> source_reports{};

    void validate() const;
};

struct MultiwayF1AcceptanceInputPaths {
    std::filesystem::path bucket_inspection_report;
    std::filesystem::path training_report;
    std::filesystem::path checkpoint_equivalence_report;
    std::filesystem::path first_lookup_report;
    std::filesystem::path second_lookup_report;
    MultiwayF1HumanRunRecord human_run{};
};

[[nodiscard]] MultiwayF1AcceptanceReport evaluate_multiway_f1_acceptance(
    const MultiwayF1AcceptanceEvidence& evidence);

[[nodiscard]] MultiwayF1AcceptanceEvidence load_multiway_f1_acceptance_evidence(
    const MultiwayF1AcceptanceInputPaths& paths);

[[nodiscard]] MultiwayF1AcceptanceReport finalize_multiway_f1_acceptance(
    const MultiwayF1AcceptanceInputPaths& paths);

void finalize_multiway_f1_acceptance_atomic(
    const std::filesystem::path& output_path,
    const MultiwayF1AcceptanceInputPaths& paths);

void save_multiway_f1_acceptance_atomic(
    const std::filesystem::path& path,
    const MultiwayF1AcceptanceReport& report);

}  // namespace texas::solver::multiway
