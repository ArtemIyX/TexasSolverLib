if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${SOURCE_DIR}/examples/multiway_workflow_main.cpp" source_text)
file(READ "${SOURCE_DIR}/src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp" trainer_text)
file(READ "${SOURCE_DIR}/src/solver/multiway/workflow/multiway_training_report.cpp" report_text)
file(READ "${SOURCE_DIR}/src/solver/multiway/workflow/multiway_f1_acceptance.cpp" report_reader_text)
file(READ "${SOURCE_DIR}/tests/test_multiway_blueprint_training.cpp" export_test_text)
file(READ "${SOURCE_DIR}/scripts/run/run_multiway_ram_1000.ps1" runner_text)
file(READ "${SOURCE_DIR}/scripts/run/run_multiway_worker_matrix.ps1" matrix_runner_text)

string(FIND "${cmake_text}" "foreach(_multiway_workflow train buckets inspect evaluate finalize)" target_position)
if(target_position EQUAL -1)
    message(FATAL_ERROR "missing complete multiway research target generator")
endif()

foreach(logging_field "TexasSolver::logging" "TEXAS_LOG_INFO" "Progress::start" "configure_training_audit")
    string(FIND "${cmake_text}${source_text}" "${logging_field}" logging_position)
    if(logging_position EQUAL -1)
        message(FATAL_ERROR "workflow logging integration is missing ${logging_field}")
    endif()
endforeach()

foreach(option "--help" "--config" "--seed")
    string(FIND "${source_text}" "${option}" option_position)
    if(option_position EQUAL -1)
        message(FATAL_ERROR "workflow front end does not support ${option}")
    endif()
endforeach()

string(FIND "${source_text}" "--threads <integer>" threads_option_position)
if(threads_option_position EQUAL -1)
    message(FATAL_ERROR "bucket workflow front end does not expose --threads")
endif()

string(FIND "${source_text}" "Print training progress (default true)" training_progress_position)
if(training_progress_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not expose live progress")
endif()

string(FIND "${source_text}" "report_multiway_training_progress" training_progress_callback_position)
if(training_progress_callback_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not connect a batch progress callback")
endif()

string(FIND "${source_text}" "--progress-memory <true|false>" progress_memory_option_position)
if(progress_memory_option_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not expose progress memory telemetry")
endif()

string(FIND "${source_text}" "working_set_current_bytes" progress_memory_output_position)
if(progress_memory_output_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not report current working-set telemetry")
endif()

string(FIND "${source_text}" "--progress-speed <true|false>" progress_speed_option_position)
if(progress_speed_option_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not expose progress speed telemetry")
endif()

string(FIND "${source_text}" "overall_trajectories_per_second" progress_speed_output_position)
if(progress_speed_output_position EQUAL -1)
    message(FATAL_ERROR "training workflow does not report overall throughput telemetry")
endif()

foreach(startup_log "texas_multiway_buckets: starting bucket generation"
        "bucket generation goal:" "effective_threads=")
    string(FIND "${source_text}" "${startup_log}" startup_log_position)
    if(startup_log_position EQUAL -1)
        message(FATAL_ERROR "bucket workflow is missing startup log field: ${startup_log}")
    endif()
endforeach()

if(source_text MATCHES "install\\(")
    message(FATAL_ERROR "multiway workflow front end must remain non-installed")
endif()

string(FIND "${source_text}" "session.save_full_policy_atomic(output_path)" streaming_call_position)
string(FIND "${source_text}" "session.export_full_policy()" materialized_call_position)
string(FIND "${source_text}" "session.record_process_memory_snapshot(MultiwayMemoryPhase::BeforeExport)" workflow_before_export_position)
string(FIND "${source_text}" "session.record_process_memory_snapshot(MultiwayMemoryPhase::AfterExport)" workflow_after_export_position)
if(streaming_call_position EQUAL -1 OR materialized_call_position GREATER_EQUAL 0 OR
   workflow_before_export_position GREATER_EQUAL 0 OR workflow_after_export_position GREATER_EQUAL 0)
    message(FATAL_ERROR "production training workflow must use only the streaming export path")
endif()

foreach(guard_field "system_memory_guard" "MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES" "export_gate.system")
    string(FIND "${source_text}" "${guard_field}" guard_position)
    if(guard_position EQUAL -1)
        message(FATAL_ERROR "production workflow is missing system memory guard field: ${guard_field}")
    endif()
endforeach()

foreach(runner_guard_field "systemMemoryReserveBytes" "systemMemoryGuardTriggered" "Stop-Process")
    string(FIND "${runner_text}" "${runner_guard_field}" runner_guard_position)
    if(runner_guard_position EQUAL -1)
        message(FATAL_ERROR "RAM runner is missing hard system memory guard field: ${runner_guard_field}")
    endif()
endforeach()

foreach(provenance_field "TEXASSOLVER_RUN_CHECKOUT_COMMIT" "--provenance"
        "binary_source_commit" "binary_dirty_state" "binarySha256After"
        "Executable/source provenance mismatch" "Executable SHA-256 changed during run")
    string(FIND "${runner_text}" "${provenance_field}" provenance_position)
    if(provenance_position EQUAL -1)
        message(FATAL_ERROR "RAM runner is missing provenance gate field: ${provenance_field}")
    endif()
endforeach()

foreach(matrix_provenance_field "BuildConfiguration" "BinaryPath" "binary_sha256"
        "matrixBinarySha256" "Worker matrix provenance gate failed")
    string(FIND "${matrix_runner_text}" "${matrix_provenance_field}" matrix_provenance_position)
    if(matrix_provenance_position EQUAL -1)
        message(FATAL_ERROR "worker matrix is missing provenance gate field: ${matrix_provenance_field}")
    endif()
endforeach()

foreach(matrix_qualification_field "DynamicChunkSize" "PlacementPolicy" "CheckpointMode"
        "WarmupRuns" "WarmColdOrder" "RandomizeCaseOrder" "CaseOrderSeed"
        "worker_matrix_summary.json" "Get-StandardDeviation" "binary_identity"
        "worker_migration_detected" "total_page_faults")
    string(FIND "${matrix_runner_text}" "${matrix_qualification_field}" matrix_qualification_position)
    if(matrix_qualification_position EQUAL -1)
        message(FATAL_ERROR "worker matrix is missing qualification tooling field: ${matrix_qualification_field}")
    endif()
endforeach()

if(matrix_runner_text MATCHES "multiway_training_scaling")
    message(FATAL_ERROR "qualification worker matrix must not invoke benchmark binaries")
endif()

foreach(runner_configuration_field "DynamicChunkSize" "--dynamic-chunk-size" "PlacementPolicy"
        "CheckpointMode" "dynamic_chunk_size" "placement_policy" "checkpoint_mode"
        "export_result_payload_hash" "export_policy_content_hash")
    string(FIND "${runner_text}" "${runner_configuration_field}" runner_configuration_position)
    if(runner_configuration_position EQUAL -1)
        message(FATAL_ERROR "RAM runner is missing qualification configuration field: ${runner_configuration_field}")
    endif()
endforeach()

string(FIND "${trainer_text}" "acquire_export_lease()" lease_position)
string(FIND "${trainer_text}" "const auto plan = make_export_plan(" plan_position)
string(FIND "${trainer_text}" "const auto before_export = working_set_monitor_.export_process_observer" process_snapshot_position)
string(FIND "${trainer_text}" "evaluate_multiway_export_process_gate(" process_gate_position)
string(FIND "${trainer_text}" "evaluate_multiway_export_system_gate(" system_gate_position)
string(FIND "${trainer_text}" "make_unique<MultiwayFullBlueprintStreamWriter>(path, stream_plan" writer_position)
string(FIND "${trainer_text}" "make_unique<MultiwayFullBlueprintShardedStreamWriter>(" sharded_writer_position)
if(lease_position EQUAL -1 OR plan_position EQUAL -1 OR process_snapshot_position EQUAL -1 OR
   process_gate_position EQUAL -1 OR system_gate_position EQUAL -1 OR writer_position EQUAL -1 OR sharded_writer_position EQUAL -1 OR
   lease_position GREATER plan_position OR plan_position GREATER process_snapshot_position OR
   process_snapshot_position GREATER process_gate_position OR process_gate_position GREATER writer_position OR
   system_gate_position GREATER writer_position OR process_gate_position GREATER sharded_writer_position OR
   system_gate_position GREATER sharded_writer_position)
    message(FATAL_ERROR "streaming export must admit lease, plan, snapshots, and gates before temp creation")
endif()

foreach(report_field "export_serialized_bytes" "export_result_bytes" "export_result_payload_hash"
        "export_duration_nanoseconds" "export_publication_mode" "export_process_gate_reason"
        "export_system_gate_reason" "export_disk_gate_status"
        "export_disk_gate_required_free_bytes" "export_disk_gate_available_free_bytes")
    string(FIND "${report_text}" "${report_field}" report_field_position)
    if(report_field_position EQUAL -1)
        message(FATAL_ERROR "training report is missing export field: ${report_field}")
    endif()
endforeach()

string(FIND "${report_reader_text}" "load_training(" report_reader_position)
string(FIND "${report_reader_text}" "parse_header(text, MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION)" report_reader_schema_position)
if(report_reader_position EQUAL -1 OR report_reader_schema_position EQUAL -1)
    message(FATAL_ERROR "existing training report reader compatibility surface is missing")
endif()

string(FIND "${export_test_text}" "previous-blueprint" previous_destination_test_position)
string(FIND "${export_test_text}" "multiway_streaming_export_process_gate_rejects_before_temp_creation" rejected_gate_test_position)
if(previous_destination_test_position EQUAL -1 OR rejected_gate_test_position EQUAL -1)
    message(FATAL_ERROR "workflow export regression test must preserve the previous destination after gate rejection")
endif()

string(FIND "${source_text}" "if (!checkpoint_dir.empty())" checkpoint_directory_guard_position)
string(FIND "${source_text}" "std::filesystem::create_directories(checkpoint_dir);" checkpoint_directory_create_position)
string(FIND "${source_text}" "MultiwayBucketArtifactWriter> writer;" writer_position)
if(checkpoint_directory_guard_position EQUAL -1 OR checkpoint_directory_create_position EQUAL -1 OR
   writer_position EQUAL -1 OR checkpoint_directory_guard_position GREATER checkpoint_directory_create_position OR
   checkpoint_directory_create_position GREATER writer_position)
    message(FATAL_ERROR "bucket workflow must create checkpoint directory before writer setup")
endif()

string(FIND "${source_text}" "--external-storage-snapshot-dir <dir>" snapshot_option_position)
string(FIND "${source_text}" "batches != 0U" snapshot_batch_guard_position)
string(FIND "${source_text}" "workflow.identity_storage_directory = workflow.storage_directory" snapshot_identity_position)
string(FIND "${source_text}" "config.limits.external_storage.identity_directory = workflow.identity_storage_directory" snapshot_model_identity_position)
string(FIND "${source_text}" "restore_multiway_external_checkpoint(" snapshot_restore_position)
foreach(snapshot_journal "CHECKPOINT_TRAINING_ROLLBACK.tmp" "CATALOG_CURRENT.previous"
        "ROWS_CURRENT.previous" "PAGES_CURRENT.previous" "CURRENT.restore.tmp"
        "CHECKPOINT_ROLLBACK.tmp" ".PIN.tmp")
    string(FIND "${source_text}" "${snapshot_journal}" snapshot_journal_position)
    if(snapshot_journal_position EQUAL -1)
        message(FATAL_ERROR "checkpoint-only storage snapshots must reject pending transaction state: ${snapshot_journal}")
    endif()
endforeach()
if(snapshot_option_position EQUAL -1 OR snapshot_batch_guard_position EQUAL -1 OR
   snapshot_identity_position EQUAL -1 OR snapshot_model_identity_position EQUAL -1 OR
   snapshot_restore_position EQUAL -1 OR snapshot_identity_position GREATER snapshot_restore_position OR
   snapshot_model_identity_position GREATER snapshot_restore_position)
    message(FATAL_ERROR "checkpoint-only storage snapshots must preserve identity and redirect restore storage")
endif()
