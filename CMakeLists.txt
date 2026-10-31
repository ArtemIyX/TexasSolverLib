cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Threads REQUIRED)

find_package(Git QUIET)
set(TEXASSOLVER_BINARY_SOURCE_COMMIT unknown)
set(TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE unknown)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE TEXASSOLVER_BINARY_SOURCE_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain --untracked-files=all
        OUTPUT_VARIABLE _texassolver_git_status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if (_texassolver_git_status STREQUAL "")
        set(TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE clean)
    else()
        set(TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE dirty)
    endif()
endif()
if (TEXASSOLVER_BINARY_SOURCE_COMMIT STREQUAL "")
    set(TEXASSOLVER_BINARY_SOURCE_COMMIT unknown)
endif()
string(REGEX REPLACE "[^A-Za-z0-9_]" "_" TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN
    "${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}")
if (CMAKE_CONFIGURATION_TYPES)
    set(TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN "$<CONFIG>")
elseif (CMAKE_BUILD_TYPE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN
        "${CMAKE_BUILD_TYPE}")
else()
    set(TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN unknown)
endif()

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" OFF)
option(TEXASSOLVER_BUILD_HEAVY_TESTS "Register heavyweight CMake boundary tests" OFF)
option(TEXASSOLVER_BUILD_EXAMPLES "Build TexasSolver examples" OFF)
option(TEXASSOLVER_BUILD_LEGACY_RESEARCH "Build the legacy vector DCFR research component" OFF)
option(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES "Build benchmark and backend-comparison examples" OFF)
option(TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH "Build fixed-private sampled HUNL reference kernels" OFF)
option(TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS "Build non-installed Tracy diagnostic targets" OFF)
option(TEXASSOLVER_TRACY_ON_DEMAND "Collect Tracy events only while a collector is connected" ON)
option(TEXASSOLVER_TRACY_SAMPLING "Enable Tracy statistical sampling" OFF)
option(TEXASSOLVER_TRACY_CALLSTACKS "Enable Tracy call stacks" OFF)
option(TEXASSOLVER_TRACY_LOCKS "Enable selected Tracy lock instrumentation" OFF)
option(TEXASSOLVER_TRACY_MEMORY "Enable explicit Tracy solver allocation events" OFF)
option(TEXASSOLVER_ENABLE_LOGGING "Build application logging support" OFF)
option(TEXASSOLVER_ENABLE_AVX2_KERNELS "Compile AVX2 kernels in their owning translation units" ON)

if (TEXASSOLVER_ENABLE_LOGGING)
    find_package(spdlog CONFIG REQUIRED)
    find_package(tomlplusplus CONFIG REQUIRED)
endif()

if (TEXASSOLVER_BUILD_RESEARCH_EXAMPLES AND NOT TEXASSOLVER_BUILD_EXAMPLES)
    message(FATAL_ERROR
        "TEXASSOLVER_BUILD_RESEARCH_EXAMPLES requires TEXASSOLVER_BUILD_EXAMPLES=ON")
endif()

set(TEXASSOLVER_PUBLIC_HEADERS
    include/core/action_id.hpp
    include/core/arena.hpp
    include/core/atomic_publish.hpp
    include/core/canonical_combo.hpp
    include/core/checked_arithmetic.hpp
    include/core/game.hpp
    include/core/fingerprint.hpp
    include/core/lib.hpp
    include/core/namespaces.hpp
    include/core/poker.hpp
    include/core/portable_binary.hpp
    include/core/street_types.hpp
    include/core/types.hpp
    include/core/value_types.hpp
    include/games/hunl.hpp
    include/games/hunl_action_types.hpp
    include/games/hunl_eval.hpp
    include/games/hunl_flat_builder.hpp
    include/games/hunl_flat_graph.hpp
    include/games/hunl_flat_graph_types.hpp
    include/games/hunl_solver.hpp
    include/games/hunl_terminal_types.hpp
    include/games/hunl_tree.hpp
    include/games/kuhn.hpp
    include/games/leduc.hpp
    include/games/multiway_fixed.hpp
    include/games/multiway_private.hpp
    include/games/multiway_rake.hpp
    include/games/multiway_rake_types.hpp
    include/games/multiway_replay.hpp
    include/games/multiway_rules.hpp
    include/games/multiway_state.hpp
    include/games/multiway_state_types.hpp
    include/games/multiway_terminal.hpp
    include/preflop/preflop.hpp
    include/preflop/preflop_equity.hpp
    include/solver/generic/dcfr.hpp
    include/solver/generic/exploit.hpp
    include/solver/hunl/bucket/hunl_bucket_map.hpp
    include/solver/hunl/bucket/hunl_bucket_terminal.hpp
    include/solver/hunl/flat/hunl_flat_dcfr.hpp
    include/solver/hunl/flat/hunl_flat_expected_value.hpp
    include/solver/hunl/flat/hunl_flat_pipeline.hpp
    include/solver/hunl/flat/hunl_flat_state.hpp
    include/solver/hunl/flat/hunl_flat_types.hpp
    include/solver/hunl/hunl_leaf_evaluator.hpp
    include/solver/hunl/sampled/hunl_sampled_config.hpp
    include/solver/hunl/sampled/hunl_sampled_config_types.hpp
    include/solver/hunl/sampled/hunl_sampled_export.hpp
    include/solver/hunl/sampled/hunl_sampled_export_types.hpp
    include/solver/hunl/sampled/hunl_sampled_profile.hpp
    include/solver/hunl/sampled/hunl_sampled_profile_types.hpp
    include/solver/hunl/sampled/hunl_sampled_range.hpp
    include/solver/hunl/sampled/hunl_sampled_scheduler.hpp
    include/solver/hunl/sampled/hunl_sampled_simd.hpp
    include/solver/hunl/sampled/hunl_sampled_solver.hpp
    include/solver/hunl/sampled/hunl_sampled_storage.hpp
    include/solver/multiway/abstraction/multiway_action_abstraction.hpp
    include/solver/multiway/blueprint/multiway_artifact.hpp
    include/solver/multiway/blueprint/multiway_blueprint_config.hpp
    include/solver/multiway/blueprint/multiway_blueprint_config_types.hpp
    include/solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp
    include/solver/multiway/blueprint/multiway_blueprint_store.hpp
    include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp
    include/solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp
    include/solver/multiway/abstraction/multiway_bucket_artifact.hpp
    include/solver/multiway/abstraction/multiway_bucket_catalog.hpp
    include/solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp
    include/solver/multiway/abstraction/multiway_bucket_generation.hpp
    include/solver/multiway/workflow/multiway_workflow_config.hpp
    include/solver/multiway/workflow/multiway_workflow_path_policy.hpp
    include/solver/multiway/workflow/multiway_diagnostic_journal.hpp
    include/solver/multiway/workflow/multiway_evidence.hpp
    include/solver/multiway/workflow/multiway_binary_provenance.hpp
    include/solver/multiway/workflow/multiway_artifact_preflight.hpp
    include/solver/multiway/workflow/multiway_checkpoint_equivalence.hpp
    include/solver/multiway/workflow/multiway_f1_acceptance.hpp
    include/solver/multiway/workflow/multiway_sizing_report.hpp
    include/solver/multiway/workflow/multiway_training_report.hpp
    include/solver/multiway/workflow/multiway_lookup_qualification.hpp
    include/solver/multiway/workflow/multiway_pb12_coverage.hpp
    include/solver/multiway/workflow/multiway_staged_qualification.hpp
    include/solver/multiway/abstraction/multiway_bucket_model.hpp
    include/solver/multiway/engine/multiway_cfr.hpp
    include/solver/multiway/blueprint/multiway_checkpoint.hpp
    include/solver/multiway/continuation/multiway_continuation_policy.hpp
    include/solver/multiway/continuation/multiway_continuation_policy_kind.hpp
    include/solver/multiway/continuation/multiway_continuation_selector.hpp
    include/solver/multiway/evaluation/multiway_evaluation.hpp
    include/solver/multiway/blueprint/multiway_export.hpp
    include/solver/multiway/abstraction/multiway_future_bucket.hpp
    include/solver/multiway/abstraction/multiway_future_bucket_types.hpp
    include/solver/multiway/continuation/multiway_leaf_evaluator.hpp
    include/solver/multiway/engine/multiway_memory.hpp
    include/solver/multiway/engine/multiway_allocation_probe.hpp
    include/solver/multiway/engine/multiway_worker_topology.hpp
    include/solver/multiway/abstraction/multiway_model_identity.hpp
    include/solver/multiway/abstraction/multiway_model_identity_types.hpp
    include/solver/multiway/abstraction/multiway_public_builder.hpp
    include/solver/multiway/session/multiway_range_belief.hpp
    include/solver/multiway/session/multiway_range_update.hpp
    include/solver/multiway/resolver/multiway_resolver.hpp
    include/solver/multiway/resolver/multiway_resolver_budget.hpp
    include/solver/multiway/resolver/multiway_resolver_evaluation.hpp
    include/solver/multiway/continuation/multiway_rollout_leaf.hpp
    include/solver/multiway/session/multiway_decision_session.hpp
    include/solver/multiway/abstraction/multiway_action_calibration.hpp
    include/solver/multiway/abstraction/multiway_future_bucket_calibration.hpp
    include/solver/multiway/continuation/multiway_continuation_calibration.hpp
    include/solver/multiway/session/multiway_full_hand_session.hpp
    include/solver/multiway/engine/multiway_scheduler.hpp
    include/solver/multiway/session/multiway_search_profile.hpp
    include/solver/multiway/session/multiway_search_session.hpp
    include/solver/multiway/engine/multiway_solver.hpp
    include/solver/multiway/engine/multiway_compact_storage.hpp
    include/solver/multiway/engine/multiway_external_storage.hpp
    include/solver/multiway/engine/multiway_external_observer.hpp
    include/solver/multiway/abstraction/multiway_terminal_adapter.hpp
    include/solver/multiway/engine/multiway_traversal.hpp
    include/solver/generic/parallel_dcfr.hpp
    include/solver/generic/solver.hpp
    include/util/abstraction.hpp
    include/util/aligned_allocator.hpp
    include/util/checked_numeric.hpp
    include/util/infoset_lookup.hpp
    include/util/infoset_registry.hpp
    include/util/iteration_range.hpp
    include/util/pcs.hpp
    include/util/profiling.hpp
    include/util/simd.hpp
    include/util/suit_iso.hpp
    include/util/thread_join_guard.hpp
)

set(TEXASSOLVER_COMPATIBILITY_HEADERS
    include/core/legacy_namespace_compat.hpp
)
set(TEXASSOLVER_STABLE_INTERNAL_HEADERS
    include/logging/config.hpp
    include/logging/log.hpp
    include/logging/progress.hpp
    include/util/tracy.hpp
    include/util/restrict.hpp
    include/solver/hunl/sampled/hunl_sampled_trajectory.hpp
    include/solver/multiway/engine/detail/multiway_compact_pruning.hpp
    include/solver/multiway/resolver/multiway_resolver_policy.hpp
)
set(TEXASSOLVER_RESEARCH_HEADERS
    include/preflop/preflop_rvr.hpp
    include/solver/generic/dcfr_vector.hpp
    include/solver/hunl/flat/hunl_flat_mccfr.hpp
    include/solver/hunl/sampled/hunl_sampled_builder.hpp
    include/solver/hunl/sampled/hunl_sampled_terminal.hpp
    include/solver/hunl/sampled/hunl_sampled_traversal.hpp
    include/solver/multiway/evaluation/multiway_baseline.hpp
)

set(TEXASSOLVER_SOURCES
    src/core/arena.cpp
    src/core/atomic_publish.cpp
    src/core/canonical_combo.cpp
    src/core/poker.cpp
    src/games/hunl.cpp
    src/games/hunl_eval.cpp
    src/games/hunl_flat_builder.cpp
    src/games/hunl_flat_graph.cpp
    src/games/hunl_solver.cpp
    src/games/hunl_tree.cpp
    src/games/kuhn.cpp
    src/games/leduc.cpp
    src/games/multiway_fixed.cpp
    src/games/multiway_private.cpp
    src/games/multiway_replay.cpp
    src/games/multiway_rules.cpp
    src/games/multiway_state.cpp
    src/games/multiway_terminal.cpp
    src/preflop/preflop.cpp
    src/preflop/preflop_equity.cpp
    src/solver/generic/exploit.cpp
    src/solver/hunl/bucket/hunl_bucket_map.cpp
    src/solver/hunl/bucket/hunl_bucket_terminal.cpp
    src/solver/hunl/flat/hunl_flat_dcfr.cpp
    src/solver/hunl/flat/hunl_flat_expected_value.cpp
    src/solver/hunl/flat/hunl_flat_pipeline.cpp
    src/solver/hunl/flat/hunl_flat_state.cpp
    src/solver/hunl/sampled/hunl_sampled_config.cpp
    src/solver/hunl/sampled/hunl_sampled_export.cpp
    src/solver/hunl/sampled/hunl_sampled_profile.cpp
    src/solver/hunl/sampled/hunl_sampled_range.cpp
    src/solver/hunl/sampled/hunl_sampled_scheduler.cpp
    src/solver/hunl/sampled/hunl_sampled_simd.cpp
    src/solver/multiway/engine/multiway_compact_pruning.cpp
    src/solver/multiway/engine/multiway_compact_pruning_avx2.cpp
    src/solver/hunl/sampled/hunl_sampled_solver.cpp
    src/solver/hunl/sampled/hunl_sampled_storage.cpp
    src/solver/hunl/sampled/hunl_sampled_trajectory.cpp
    src/solver/multiway/abstraction/multiway_action_abstraction.cpp
    src/solver/multiway/blueprint/multiway_artifact.cpp
    src/solver/multiway/blueprint/multiway_root_policy_artifact.cpp
    src/solver/multiway/blueprint/multiway_replay_artifact.cpp
    src/solver/multiway/blueprint/multiway_training_checkpoint_artifact.cpp
    src/solver/multiway/blueprint/multiway_blueprint_config.cpp
    src/solver/multiway/blueprint/multiway_blueprint_policy_provider.cpp
    src/solver/multiway/blueprint/multiway_mapped_sharded_blueprint_provider.cpp
    src/solver/multiway/blueprint/multiway_blueprint_store.cpp
    src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp
    src/solver/multiway/blueprint/multiway_blueprint_export.cpp
    src/solver/multiway/blueprint/multiway_blueprint_checkpoint.cpp
    src/solver/multiway/blueprint/multiway_blueprint_training_loop.cpp
    src/solver/multiway/abstraction/multiway_bucket_artifact.cpp
    src/solver/multiway/abstraction/multiway_bucket_artifact_codec.cpp
    src/solver/multiway/abstraction/multiway_bucket_catalog.cpp
    src/solver/multiway/abstraction/multiway_bucket_artifact_writer.cpp
    src/solver/multiway/abstraction/multiway_bucket_generation.cpp
    src/solver/multiway/abstraction/multiway_bucket_inspector.cpp
    src/solver/multiway/workflow/multiway_workflow_config.cpp
    src/solver/multiway/workflow/multiway_workflow_config_identity.cpp
    src/solver/multiway/workflow/multiway_workflow_config_io.cpp
    src/solver/multiway/workflow/multiway_workflow_path_policy.cpp
    src/solver/multiway/workflow/multiway_diagnostic_journal.cpp
    src/solver/multiway/workflow/multiway_evidence.cpp
    src/solver/multiway/workflow/multiway_artifact_preflight.cpp
    src/solver/multiway/workflow/multiway_checkpoint_equivalence.cpp
    src/solver/multiway/workflow/multiway_f1_acceptance.cpp
    src/solver/multiway/workflow/multiway_sizing_report.cpp
    src/solver/multiway/workflow/multiway_training_report.cpp
    src/solver/multiway/workflow/multiway_training_report_json.cpp
    src/solver/multiway/workflow/multiway_lookup_qualification.cpp
    src/solver/multiway/workflow/multiway_pb12_coverage.cpp
    src/solver/multiway/workflow/multiway_staged_qualification.cpp
    src/solver/multiway/abstraction/multiway_bucket_model.cpp
    src/solver/multiway/engine/multiway_cfr.cpp
    src/solver/multiway/blueprint/multiway_checkpoint.cpp
    src/solver/multiway/continuation/multiway_continuation_policy.cpp
    src/solver/multiway/continuation/multiway_continuation_selector.cpp
    src/solver/multiway/evaluation/multiway_aivat.cpp
    src/solver/multiway/evaluation/multiway_evaluation.cpp
    src/solver/multiway/blueprint/multiway_export.cpp
    src/solver/multiway/abstraction/multiway_future_bucket.cpp
    src/solver/multiway/engine/multiway_memory.cpp
    src/solver/multiway/engine/multiway_allocation_probe.cpp
    src/solver/multiway/engine/multiway_worker_topology.cpp
    src/solver/multiway/abstraction/multiway_model_identity.cpp
    src/solver/multiway/abstraction/multiway_public_builder.cpp
    src/solver/multiway/session/multiway_range_belief.cpp
    src/solver/multiway/session/multiway_range_update.cpp
    src/solver/multiway/resolver/multiway_resolver.cpp
    src/solver/multiway/resolver/multiway_resolver_budget.cpp
    src/solver/multiway/resolver/multiway_resolver_evaluation.cpp
    src/solver/multiway/resolver/multiway_resolver_policy.cpp
    src/solver/multiway/continuation/multiway_rollout_leaf.cpp
    src/solver/multiway/session/multiway_decision_session.cpp
    src/solver/multiway/abstraction/multiway_action_calibration.cpp
    src/solver/multiway/abstraction/multiway_future_bucket_calibration.cpp
    src/solver/multiway/continuation/multiway_continuation_calibration.cpp
    src/solver/multiway/session/multiway_full_hand_session.cpp
    src/solver/multiway/engine/multiway_scheduler.cpp
    src/solver/multiway/session/multiway_search_session.cpp
    src/solver/multiway/engine/multiway_solver.cpp
    src/solver/multiway/engine/multiway_coordinator_admission.cpp
    src/solver/multiway/engine/multiway_coordinator_merge.cpp
    src/solver/multiway/engine/multiway_coordinator_checkpoint.cpp
    src/solver/multiway/engine/multiway_compact_storage.cpp
    src/solver/multiway/engine/multiway_external_checkpoint.cpp
    src/solver/multiway/engine/multiway_external_transaction.cpp
    src/solver/multiway/engine/multiway_external_storage_config.cpp
    src/solver/multiway/engine/multiway_external_record_store.cpp
    src/solver/multiway/engine/multiway_external_compaction.cpp
    src/solver/multiway/engine/multiway_external_public_catalog.cpp
    src/solver/multiway/engine/multiway_external_row_index.cpp
    src/solver/multiway/engine/multiway_external_compact_pages.cpp
    src/solver/multiway/engine/multiway_external_compact_storage.cpp
    src/solver/multiway/engine/multiway_external_training_storage.cpp
    src/solver/multiway/engine/multiway_external_page_cache.cpp
    src/solver/multiway/engine/multiway_public_catalog.cpp
    src/solver/multiway/abstraction/multiway_terminal_adapter.cpp
    src/solver/multiway/engine/multiway_discovery.cpp
    src/solver/multiway/engine/multiway_root_traversal.cpp
    src/solver/multiway/engine/multiway_batch_runner.cpp
    src/solver/generic/parallel_dcfr.cpp
    src/solver/generic/solver.cpp
)

set(TEXASSOLVER_PLATFORM_SOURCES
    src/util/abstraction.cpp
    src/util/abstraction_stub.cpp
)
set(TEXASSOLVER_APPLICATION_SOURCES
    src/logging/config.cpp
    src/logging/log.cpp
    src/logging/progress.cpp
    src/logging/terminal.cpp
)
set(TEXASSOLVER_APPLICATION_DISABLED_SOURCES
    src/logging/config_stub.cpp
    src/logging/log_stub.cpp
    src/logging/progress_stub.cpp
)
set(TEXASSOLVER_TEST_SUPPORT_SOURCES
    src/solver/multiway/evaluation/multiway_baseline.cpp
)
set(TEXASSOLVER_LEGACY_RESEARCH_SOURCES
    src/preflop/preflop_rvr.cpp
    src/solver/generic/dcfr_vector.cpp
)
set(TEXASSOLVER_FIXED_RESEARCH_SOURCES
    src/solver/hunl/sampled/hunl_sampled_builder.cpp
    src/solver/hunl/sampled/hunl_sampled_terminal.cpp
    src/solver/hunl/sampled/hunl_sampled_traversal.cpp
)
set(TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES
    src/solver/hunl/flat/hunl_flat_mccfr.cpp
)

list(APPEND TEXASSOLVER_SOURCES
    src/util/infoset_registry.cpp
    src/util/pcs.cpp
    src/util/profiling.cpp
    src/util/simd.cpp
    src/util/suit_iso.cpp
)

function(texassolver_verify_source_classification)
    file(GLOB_RECURSE sources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/*.cpp")
    set(source_categories
        TEXASSOLVER_SOURCES TEXASSOLVER_PLATFORM_SOURCES TEXASSOLVER_TEST_SUPPORT_SOURCES
        TEXASSOLVER_LEGACY_RESEARCH_SOURCES TEXASSOLVER_FIXED_RESEARCH_SOURCES
        TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES TEXASSOLVER_APPLICATION_SOURCES
        TEXASSOLVER_APPLICATION_DISABLED_SOURCES)
    foreach(source IN LISTS sources)
        set(matches 0)
        foreach(category IN LISTS source_categories)
            list(FIND ${category} "${source}" index)
            if (NOT index EQUAL -1)
                math(EXPR matches "${matches} + 1")
            endif()
        endforeach()
        if (NOT matches EQUAL 1)
            message(FATAL_ERROR "source classification requires exactly one category: ${source}")
        endif()
    endforeach()

    file(GLOB_RECURSE headers RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "include/*.hpp")
    set(header_categories
        TEXASSOLVER_PUBLIC_HEADERS TEXASSOLVER_STABLE_INTERNAL_HEADERS
        TEXASSOLVER_COMPATIBILITY_HEADERS TEXASSOLVER_RESEARCH_HEADERS)
    foreach(header IN LISTS headers)
        set(matches 0)
        foreach(category IN LISTS header_categories)
            list(FIND ${category} "${header}" index)
            if (NOT index EQUAL -1)
                math(EXPR matches "${matches} + 1")
            endif()
        endforeach()
        if (NOT matches EQUAL 1)
            message(FATAL_ERROR "header classification requires exactly one category: ${header}")
        endif()
    endforeach()
endfunction()

texassolver_verify_source_classification()

set(TEXASSOLVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/TexasSolverTargets.cmake)

if (TEXASSOLVER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if (TEXASSOLVER_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
