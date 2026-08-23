cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Threads REQUIRED)

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" OFF)
option(TEXASSOLVER_BUILD_EXAMPLES "Build TexasSolver examples" OFF)

set(TEXASSOLVER_PUBLIC_HEADERS
    include/core/arena.hpp
    include/core/atomic_publish.hpp
    include/core/canonical_combo.hpp
    include/core/game.hpp
    include/core/lib.hpp
    include/core/namespaces.hpp
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
    include/preflop/preflop_rvr.hpp
    include/solver/dcfr.hpp
    include/solver/dcfr_vector.hpp
    include/solver/exploit.hpp
    include/solver/hunl_bucket_map.hpp
    include/solver/hunl_bucket_terminal.hpp
    include/solver/hunl_flat_dcfr.hpp
    include/solver/hunl_flat_expected_value.hpp
    include/solver/hunl_flat_mccfr.hpp
    include/solver/hunl_flat_pipeline.hpp
    include/solver/hunl_flat_state.hpp
    include/solver/hunl_leaf_evaluator.hpp
    include/solver/hunl_sampled_builder.hpp
    include/solver/hunl_sampled_config.hpp
    include/solver/hunl_sampled_export.hpp
    include/solver/hunl_sampled_profile.hpp
    include/solver/hunl_sampled_range.hpp
    include/solver/hunl_sampled_scheduler.hpp
    include/solver/hunl_sampled_simd.hpp
    include/solver/hunl_sampled_solver.hpp
    include/solver/hunl_sampled_storage.hpp
    include/solver/hunl_sampled_terminal.hpp
    include/solver/hunl_sampled_traversal.hpp
    include/solver/multiway_action_abstraction.hpp
    include/solver/multiway_artifact.hpp
    include/solver/multiway_baseline.hpp
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
    include/solver/multiway_runtime_session.hpp
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

set(TEXASSOLVER_SOURCES
    src/core/arena.cpp
    src/core/atomic_publish.cpp
    src/core/canonical_combo.cpp
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
    src/preflop/preflop_rvr.cpp
    src/solver/dcfr_vector.cpp
    src/solver/exploit.cpp
    src/solver/hunl_bucket_map.cpp
    src/solver/hunl_bucket_terminal.cpp
    src/solver/hunl_flat_dcfr.cpp
    src/solver/hunl_flat_expected_value.cpp
    src/solver/hunl_flat_mccfr.cpp
    src/solver/hunl_flat_pipeline.cpp
    src/solver/hunl_flat_state.cpp
    src/solver/hunl_sampled_builder.cpp
    src/solver/hunl_sampled_config.cpp
    src/solver/hunl_sampled_export.cpp
    src/solver/hunl_sampled_profile.cpp
    src/solver/hunl_sampled_range.cpp
    src/solver/hunl_sampled_scheduler.cpp
    src/solver/hunl_sampled_simd.cpp
    src/solver/hunl_sampled_solver.cpp
    src/solver/hunl_sampled_storage.cpp
    src/solver/hunl_sampled_terminal.cpp
    src/solver/hunl_sampled_traversal.cpp
    src/solver/multiway_action_abstraction.cpp
    src/solver/multiway_artifact.cpp
    src/solver/multiway_baseline.cpp
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
    src/solver/multiway_rollout_leaf.cpp
    src/solver/multiway_runtime_session.cpp
    src/solver/multiway_scheduler.cpp
    src/solver/multiway_search_profile.cpp
    src/solver/multiway_search_session.cpp
    src/solver/multiway_solver.cpp
    src/solver/multiway_terminal_adapter.cpp
    src/solver/multiway_traversal.cpp
    src/solver/parallel_dcfr.cpp
    src/solver/solver.cpp
)

if (WIN32)
    list(APPEND TEXASSOLVER_SOURCES src/util/abstraction.cpp)
endif()

list(APPEND TEXASSOLVER_SOURCES
    src/util/infoset_registry.cpp
    src/util/pcs.cpp
    src/util/profiling.cpp
    src/util/simd.cpp
    src/util/suit_iso.cpp
)

set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_PLO4 OFF)
set(BUILD_PLO5 OFF)
set(BUILD_PLO6 OFF)
set(BUILD_CARD5 ON)
set(BUILD_CARD6 ON)
set(BUILD_CARD7 ON)
add_subdirectory(external/pokerHandEvaluator/cpp EXCLUDE_FROM_ALL)
unset(BUILD_TESTS)
unset(BUILD_EXAMPLES)
unset(BUILD_PLO4)
unset(BUILD_PLO5)
unset(BUILD_PLO6)
unset(BUILD_CARD5)
unset(BUILD_CARD6)
unset(BUILD_CARD7)

add_library(texas STATIC)
add_library(TexasSolver::texas ALIAS texas)

target_sources(texas PRIVATE ${TEXASSOLVER_SOURCES})
target_sources(texas PRIVATE ${TEXASSOLVER_PUBLIC_HEADERS})
target_link_libraries(texas PRIVATE pheval)
target_link_libraries(texas PRIVATE Threads::Threads)
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

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

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
    target_include_directories(texas_solver_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_benchmark
        examples/benchmarks/main.cpp
    )
    target_link_libraries(texas_solver_benchmark PRIVATE texas)
    target_include_directories(texas_solver_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_flat_scheduler_benchmark
        examples/benchmarks/flat_scheduler_main.cpp
    )
    target_link_libraries(texas_solver_flat_scheduler_benchmark PRIVATE texas)
    target_include_directories(texas_solver_flat_scheduler_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_hunl_backend_compare
        examples/benchmarks/hunl_backend_compare_main.cpp
    )
    target_link_libraries(texas_solver_hunl_backend_compare PRIVATE texas)
    target_include_directories(texas_solver_hunl_backend_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_hunl_random_flat
        examples/benchmarks/hunl_random_flat_main.cpp
    )
    target_link_libraries(texas_solver_hunl_random_flat PRIVATE texas)
    target_include_directories(texas_solver_hunl_random_flat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_hunl_mccfr_scaling
        examples/benchmarks/hunl_mccfr_scaling_main.cpp
    )
    target_link_libraries(texas_solver_hunl_mccfr_scaling PRIVATE texas)
    target_include_directories(texas_solver_hunl_mccfr_scaling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_hunl_mccfr_postflop
        examples/hunl_mccfr_postflop_main.cpp
    )
    target_link_libraries(texas_solver_hunl_mccfr_postflop PRIVATE texas)
    target_include_directories(texas_solver_hunl_mccfr_postflop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

endif()

if (TEXASSOLVER_BUILD_TESTS AND BUILD_TESTING)
    file(GLOB TEXASSOLVER_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp
    )

    foreach(test_source IN LISTS TEXASSOLVER_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        if (test_name STREQUAL "test_main" OR test_name STREQUAL "main_test")
            continue()
        endif()

        add_executable(${test_name}
            tests/test_main.cpp
            ${test_source}
        )

        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_link_libraries(${test_name} PRIVATE texas)

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    add_test(
        NAME test_package_config_contract
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_package_config_contract.cmake
    )
endif()
