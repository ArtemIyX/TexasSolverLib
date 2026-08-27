cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Threads REQUIRED)

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" OFF)
option(TEXASSOLVER_BUILD_HEAVY_TESTS "Register heavyweight CMake boundary tests" OFF)
option(TEXASSOLVER_BUILD_EXAMPLES "Build TexasSolver examples" OFF)
option(TEXASSOLVER_BUILD_LEGACY_RESEARCH "Build the legacy vector DCFR research component" OFF)
option(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES "Build benchmark and backend-comparison examples" OFF)
option(TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH "Build fixed-private sampled HUNL reference kernels" OFF)

if (TEXASSOLVER_BUILD_RESEARCH_EXAMPLES AND NOT TEXASSOLVER_BUILD_EXAMPLES)
    message(FATAL_ERROR
        "TEXASSOLVER_BUILD_RESEARCH_EXAMPLES requires TEXASSOLVER_BUILD_EXAMPLES=ON")
endif()

set(TEXASSOLVER_PUBLIC_HEADERS
    include/core/arena.hpp
    include/core/atomic_publish.hpp
    include/core/canonical_combo.hpp
    include/core/game.hpp
    include/core/fingerprint.hpp
    include/core/lib.hpp
    include/core/namespaces.hpp
    include/core/poker.hpp
    include/core/portable_binary.hpp
    include/core/types.hpp
    include/games/hunl.hpp
    include/games/hunl_eval.hpp
    include/games/hunl_flat_builder.hpp
    include/games/hunl_flat_graph.hpp
    include/games/hunl_solver.hpp
    include/games/hunl_tree.hpp
    include/games/kuhn.hpp
    include/games/leduc.hpp
    include/games/multiway_fixed.hpp
    include/games/multiway_private.hpp
    include/games/multiway_rake.hpp
    include/games/multiway_replay.hpp
    include/games/multiway_rules.hpp
    include/games/multiway_state.hpp
    include/games/multiway_terminal.hpp
    include/preflop/preflop.hpp
    include/preflop/preflop_equity.hpp
    include/solver/dcfr.hpp
    include/solver/exploit.hpp
    include/solver/hunl_bucket_map.hpp
    include/solver/hunl_bucket_terminal.hpp
    include/solver/hunl_flat_dcfr.hpp
    include/solver/hunl_flat_expected_value.hpp
    include/solver/hunl_flat_pipeline.hpp
    include/solver/hunl_flat_state.hpp
    include/solver/hunl_leaf_evaluator.hpp
    include/solver/hunl_sampled_config.hpp
    include/solver/hunl_sampled_export.hpp
    include/solver/hunl_sampled_profile.hpp
    include/solver/hunl_sampled_range.hpp
    include/solver/hunl_sampled_scheduler.hpp
    include/solver/hunl_sampled_simd.hpp
    include/solver/hunl_sampled_solver.hpp
    include/solver/hunl_sampled_storage.hpp
    include/solver/multiway_action_abstraction.hpp
    include/solver/multiway_artifact.hpp
    include/solver/multiway_blueprint_config.hpp
    include/solver/multiway_blueprint_policy_provider.hpp
    include/solver/multiway_blueprint_store.hpp
    include/solver/multiway_blueprint_trainer.hpp
    include/solver/multiway_bucket_artifact.hpp
    include/solver/multiway_bucket_model.hpp
    include/solver/multiway_cfr.hpp
    include/solver/multiway_checkpoint.hpp
    include/solver/multiway_continuation_policy.hpp
    include/solver/multiway_continuation_policy_kind.hpp
    include/solver/multiway_continuation_selector.hpp
    include/solver/multiway_evaluation.hpp
    include/solver/multiway_export.hpp
    include/solver/multiway_future_bucket.hpp
    include/solver/multiway_leaf_evaluator.hpp
    include/solver/multiway_memory.hpp
    include/solver/multiway_model_identity.hpp
    include/solver/multiway_public_builder.hpp
    include/solver/multiway_range_belief.hpp
    include/solver/multiway_range_update.hpp
    include/solver/multiway_resolver.hpp
    include/solver/multiway_resolver_budget.hpp
    include/solver/multiway_resolver_evaluation.hpp
    include/solver/multiway_rollout_leaf.hpp
    include/solver/multiway_decision_session.hpp
    include/solver/multiway_full_hand_session.hpp
    include/solver/multiway_scheduler.hpp
    include/solver/multiway_search_profile.hpp
    include/solver/multiway_search_session.hpp
    include/solver/multiway_solver.hpp
    include/solver/multiway_terminal_adapter.hpp
    include/solver/multiway_traversal.hpp
    include/solver/parallel_dcfr.hpp
    include/solver/solver.hpp
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
    include/solver/hunl_sampled_trajectory.hpp
    include/solver/multiway_resolver_policy.hpp
)
set(TEXASSOLVER_RESEARCH_HEADERS
    include/preflop/preflop_rvr.hpp
    include/solver/dcfr_vector.hpp
    include/solver/hunl_flat_mccfr.hpp
    include/solver/hunl_sampled_builder.hpp
    include/solver/hunl_sampled_terminal.hpp
    include/solver/hunl_sampled_traversal.hpp
    include/solver/multiway_baseline.hpp
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
    src/solver/exploit.cpp
    src/solver/hunl_bucket_map.cpp
    src/solver/hunl_bucket_terminal.cpp
    src/solver/hunl_flat_dcfr.cpp
    src/solver/hunl_flat_expected_value.cpp
    src/solver/hunl_flat_pipeline.cpp
    src/solver/hunl_flat_state.cpp
    src/solver/hunl_sampled_config.cpp
    src/solver/hunl_sampled_export.cpp
    src/solver/hunl_sampled_profile.cpp
    src/solver/hunl_sampled_range.cpp
    src/solver/hunl_sampled_scheduler.cpp
    src/solver/hunl_sampled_simd.cpp
    src/solver/hunl_sampled_solver.cpp
    src/solver/hunl_sampled_storage.cpp
    src/solver/hunl_sampled_trajectory.cpp
    src/solver/multiway_action_abstraction.cpp
    src/solver/multiway_artifact.cpp
    src/solver/multiway_blueprint_config.cpp
    src/solver/multiway_blueprint_policy_provider.cpp
    src/solver/multiway_blueprint_store.cpp
    src/solver/multiway_blueprint_trainer.cpp
    src/solver/multiway_bucket_artifact.cpp
    src/solver/multiway_bucket_model.cpp
    src/solver/multiway_cfr.cpp
    src/solver/multiway_checkpoint.cpp
    src/solver/multiway_continuation_policy.cpp
    src/solver/multiway_continuation_selector.cpp
    src/solver/multiway_evaluation.cpp
    src/solver/multiway_export.cpp
    src/solver/multiway_future_bucket.cpp
    src/solver/multiway_memory.cpp
    src/solver/multiway_model_identity.cpp
    src/solver/multiway_public_builder.cpp
    src/solver/multiway_range_belief.cpp
    src/solver/multiway_range_update.cpp
    src/solver/multiway_resolver.cpp
    src/solver/multiway_resolver_budget.cpp
    src/solver/multiway_resolver_evaluation.cpp
    src/solver/multiway_resolver_policy.cpp
    src/solver/multiway_rollout_leaf.cpp
    src/solver/multiway_decision_session.cpp
    src/solver/multiway_full_hand_session.cpp
    src/solver/multiway_scheduler.cpp
    src/solver/multiway_search_session.cpp
    src/solver/multiway_solver.cpp
    src/solver/multiway_terminal_adapter.cpp
    src/solver/multiway_traversal.cpp
    src/solver/parallel_dcfr.cpp
    src/solver/solver.cpp
)

set(TEXASSOLVER_PLATFORM_SOURCES
    src/util/abstraction.cpp
    src/util/abstraction_stub.cpp
)
set(TEXASSOLVER_TEST_SUPPORT_SOURCES
    src/solver/multiway_baseline.cpp
)
set(TEXASSOLVER_LEGACY_RESEARCH_SOURCES
    src/preflop/preflop_rvr.cpp
    src/solver/dcfr_vector.cpp
)
set(TEXASSOLVER_FIXED_RESEARCH_SOURCES
    src/solver/hunl_sampled_builder.cpp
    src/solver/hunl_sampled_terminal.cpp
    src/solver/hunl_sampled_traversal.cpp
)
set(TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES
    src/solver/hunl_flat_mccfr.cpp
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
        TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES)
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

function(texassolver_configure_internal_target target)
    target_include_directories(${target}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_compile_features(${target} PUBLIC cxx_std_17)
endfunction()

function(texassolver_configure_example_target target)
    target_include_directories(${target}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endfunction()

if (WIN32)
    list(APPEND TEXASSOLVER_SOURCES src/util/abstraction.cpp)
else()
    list(APPEND TEXASSOLVER_SOURCES src/util/abstraction_stub.cpp)
endif()

set(_texassolver_evaluator_options
    BUILD_TESTS BUILD_EXAMPLES BUILD_PLO4 BUILD_PLO5 BUILD_PLO6 BUILD_CARD5 BUILD_CARD6 BUILD_CARD7)
foreach(_texassolver_option IN LISTS _texassolver_evaluator_options)
    if (DEFINED ${_texassolver_option})
        set(_texassolver_saved_defined_${_texassolver_option} TRUE)
        set(_texassolver_saved_value_${_texassolver_option} "${${_texassolver_option}}")
    endif()
endforeach()
set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_PLO4 OFF)
set(BUILD_PLO5 OFF)
set(BUILD_PLO6 OFF)
set(BUILD_CARD5 ON)
set(BUILD_CARD6 ON)
set(BUILD_CARD7 ON)
add_subdirectory(external/pokerHandEvaluator/cpp EXCLUDE_FROM_ALL)
foreach(_texassolver_option IN LISTS _texassolver_evaluator_options)
    if (_texassolver_saved_defined_${_texassolver_option})
        set(${_texassolver_option} "${_texassolver_saved_value_${_texassolver_option}}")
    else()
        unset(${_texassolver_option})
    endif()
endforeach()
unset(_texassolver_evaluator_options)

add_library(texas STATIC)
add_library(TexasSolver::texas ALIAS texas)

target_sources(texas PRIVATE
    ${TEXASSOLVER_SOURCES}
    ${TEXASSOLVER_PUBLIC_HEADERS}
)
target_link_libraries(texas PRIVATE pheval Threads::Threads)
if (WIN32)
    target_link_libraries(texas PRIVATE Cabinet)
endif()

target_include_directories(texas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(texas PUBLIC cxx_std_17)

set_target_properties(texas PROPERTIES
    EXPORT_NAME texas
    OUTPUT_NAME texas
)

if (TEXASSOLVER_BUILD_LEGACY_RESEARCH)
    add_library(texas_legacy_vector STATIC
        ${TEXASSOLVER_LEGACY_RESEARCH_SOURCES}
    )
    add_library(TexasSolver::legacy_vector ALIAS texas_legacy_vector)
    texassolver_configure_internal_target(texas_legacy_vector)
    target_link_libraries(texas_legacy_vector PUBLIC texas)
endif()

if (TEXASSOLVER_BUILD_TESTS OR TEXASSOLVER_BUILD_RESEARCH_EXAMPLES)
    add_library(texas_hunl_flat_mccfr_research STATIC
        ${TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES}
    )
    add_library(TexasSolver::hunl_flat_mccfr_research ALIAS texas_hunl_flat_mccfr_research)
    texassolver_configure_internal_target(texas_hunl_flat_mccfr_research)
    target_link_libraries(texas_hunl_flat_mccfr_research PUBLIC texas)
endif()

if (TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH)
    add_library(texas_hunl_fixed_research STATIC
        ${TEXASSOLVER_FIXED_RESEARCH_SOURCES}
    )
    texassolver_configure_internal_target(texas_hunl_fixed_research)
    target_link_libraries(texas_hunl_fixed_research PUBLIC texas)
    add_library(TexasSolver::hunl_fixed_research ALIAS texas_hunl_fixed_research)
endif()

if (MSVC)
    target_compile_options(texas PRIVATE /W4)
else()
    target_compile_options(texas PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS texas
    EXPORT TexasSolverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS pheval
    EXPORT TexasSolverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phevaluator
)

foreach(public_header IN LISTS TEXASSOLVER_PUBLIC_HEADERS)
    file(RELATIVE_PATH public_header_relative ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/${public_header})
    get_filename_component(public_header_directory ${public_header_relative} DIRECTORY)
    install(
        FILES ${public_header}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${public_header_directory})
endforeach()

install(EXPORT TexasSolverTargets
    NAMESPACE TexasSolver::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexasSolver
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TexasSolverConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TexasSolverConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexasSolver
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TexasSolverConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TexasSolverConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TexasSolverConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexasSolver
)

if (TEXASSOLVER_BUILD_EXAMPLES)
    add_executable(texas_solver_example
        examples/solve_kuhn.cpp
    )
    target_link_libraries(texas_solver_example PRIVATE texas)
    texassolver_configure_example_target(texas_solver_example)

    if (TEXASSOLVER_BUILD_RESEARCH_EXAMPLES)
        foreach(_multiway_workflow train buckets inspect evaluate)
            add_executable(texas_multiway_${_multiway_workflow}
                examples/multiway_workflow_main.cpp
            )
            target_link_libraries(texas_multiway_${_multiway_workflow} PRIVATE texas)
            texassolver_configure_example_target(texas_multiway_${_multiway_workflow})
        endforeach()

        add_executable(texas_solver_hunl_mccfr_postflop
            examples/hunl_mccfr_postflop_main.cpp
        )
        target_link_libraries(texas_solver_hunl_mccfr_postflop PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_hunl_mccfr_scaling
            examples/benchmarks/hunl_mccfr_scaling_main.cpp
        )
        target_link_libraries(texas_solver_hunl_mccfr_scaling PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_hunl_random_flat
            examples/benchmarks/hunl_random_flat_main.cpp
        )
        target_link_libraries(texas_solver_hunl_random_flat PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_benchmark
            examples/benchmarks/main.cpp
        )
        target_link_libraries(texas_solver_benchmark PRIVATE texas)
        texassolver_configure_example_target(texas_solver_benchmark)

        add_executable(texas_solver_flat_scheduler_benchmark
            examples/benchmarks/flat_scheduler_main.cpp
        )
        target_link_libraries(texas_solver_flat_scheduler_benchmark PRIVATE texas)
        texassolver_configure_example_target(texas_solver_flat_scheduler_benchmark)

        add_executable(texas_solver_hunl_backend_compare
            examples/benchmarks/hunl_backend_compare_main.cpp
        )
        target_link_libraries(texas_solver_hunl_backend_compare PRIVATE texas)
        texassolver_configure_example_target(texas_solver_hunl_backend_compare)
    endif()

endif()

if (TEXASSOLVER_BUILD_TESTS AND BUILD_TESTING)
    set(TEXASSOLVER_HEAVY_TESTS
        test_hunl_flat_dcfr
        test_ranges_threading
        test_solver_exploitability_contract
        test_parallel_dcfr
        test_hunl_bucket_model
        test_hunl_flat_graph
    )

    file(GLOB TEXASSOLVER_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp
    )

    foreach(test_source IN LISTS TEXASSOLVER_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        if (test_name STREQUAL "test_main" OR test_name STREQUAL "main_test")
            continue()
        endif()
        if (NOT WIN32 AND (
            test_name STREQUAL "test_abstraction_loader" OR
            test_name STREQUAL "test_hunl_solver_storage_loader"))
            continue()
        endif()
        if (test_name STREQUAL "test_hunl_sampled_solver" AND
            NOT TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH)
            continue()
        endif()
        if (NOT TEXASSOLVER_BUILD_HEAVY_TESTS AND
            test_name IN_LIST TEXASSOLVER_HEAVY_TESTS)
            continue()
        endif()

        set(test_support_sources)
        if (test_name STREQUAL "test_multiway_baseline")
            list(APPEND test_support_sources ${TEXASSOLVER_TEST_SUPPORT_SOURCES})
        endif()
        if (test_name STREQUAL "test_class169_solver_contract" OR
            test_name STREQUAL "test_dcfr_discount_contract" OR
            test_name STREQUAL "test_preflop_tree_action_contract")
            list(APPEND test_support_sources
                ${TEXASSOLVER_LEGACY_RESEARCH_SOURCES})
        endif()

        add_executable(${test_name}
            tests/test_main.cpp
            ${test_source}
            ${test_support_sources}
        )

        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_link_libraries(${test_name} PRIVATE texas)
        if (test_name STREQUAL "test_hunl_flat_mccfr" OR
            test_name STREQUAL "test_dcfr_discount_contract")
            target_link_libraries(${test_name} PRIVATE texas_hunl_flat_mccfr_research)
        endif()
        if (test_name STREQUAL "test_hunl_sampled_solver")
            target_link_libraries(${test_name} PRIVATE texas_hunl_fixed_research)
        endif()

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    add_test(
        NAME test_package_config_contract
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_package_config_contract.cmake
    )
    add_test(
        NAME test_multiway_workflow_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_multiway_workflow_boundary.cmake
    )
    add_test(
        NAME test_legacy_vector_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_legacy_vector_boundary.cmake
    )
    add_test(
        NAME test_example_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_example_boundary.cmake
    )
    add_test(
        NAME test_core_namespace_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_core_namespace_boundary.cmake
    )
    add_test(
        NAME test_decision_session_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_decision_session_boundary.cmake
    )
    add_test(
        NAME test_hunl_sampled_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_hunl_sampled_boundary.cmake
    )
    add_test(
        NAME test_multiway_traversal_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_multiway_traversal_boundary.cmake
    )
    if (TEXASSOLVER_BUILD_HEAVY_TESTS)
        add_test(
            NAME test_evaluator_options_boundary
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_evaluator_options_boundary.cmake
        )
    endif()
    add_test(
        NAME test_source_classification
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_source_classification.cmake
    )
    add_test(
        NAME test_legacy_namespace_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_legacy_namespace_boundary.cmake
    )
    if (TEXASSOLVER_BUILD_HEAVY_TESTS)
        add_test(
            NAME test_public_header_boundary
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_public_header_boundary.cmake
        )
    endif()
endif()
