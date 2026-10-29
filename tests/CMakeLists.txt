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
        ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
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
        if ((test_name STREQUAL "test_logging_config" OR test_name STREQUAL "test_logging" OR
             test_name STREQUAL "test_logging_progress") AND NOT TEXASSOLVER_ENABLE_LOGGING)
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

        set(test_support_source_paths)
        foreach(test_support_source IN LISTS test_support_sources)
            list(APPEND test_support_source_paths
                "${TEXASSOLVER_SOURCE_DIR}/${test_support_source}")
        endforeach()

        add_executable(${test_name}
            ${TEXASSOLVER_SOURCE_DIR}/tests/test_main.cpp
            ${test_source}
            ${test_support_source_paths}
        )

        target_include_directories(${test_name} PRIVATE
            ${TEXASSOLVER_SOURCE_DIR}/include
            ${TEXASSOLVER_SOURCE_DIR}/tests
        )
        target_link_libraries(${test_name} PRIVATE texas)
        if (test_name STREQUAL "test_logging_config" OR test_name STREQUAL "test_logging" OR
            test_name STREQUAL "test_logging_progress")
            target_link_libraries(${test_name} PRIVATE TexasSolver::logging)
        endif()
        if (test_name STREQUAL "test_logging_disabled")
            target_compile_definitions(${test_name} PRIVATE TEXASSOLVER_ENABLE_LOGGING=0)
        endif()
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
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_package_config_contract.cmake
    )
    add_test(
        NAME test_cmake_structure
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_cmake_structure.cmake
    )
    add_test(
        NAME test_multiway_workflow_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_multiway_workflow_boundary.cmake
    )
    add_test(
        NAME test_legacy_vector_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_legacy_vector_boundary.cmake
    )
    add_test(
        NAME test_example_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_example_boundary.cmake
    )
    add_test(
        NAME test_core_namespace_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_core_namespace_boundary.cmake
    )
    add_test(
        NAME test_decision_session_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_decision_session_boundary.cmake
    )
    add_test(
        NAME test_hunl_sampled_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_hunl_sampled_boundary.cmake
    )
    add_test(
        NAME test_multiway_traversal_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_multiway_traversal_boundary.cmake
    )
    if (TEXASSOLVER_BUILD_HEAVY_TESTS)
        add_test(
            NAME test_evaluator_options_boundary
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
                -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_evaluator_options_boundary.cmake
        )
    endif()
    add_test(
        NAME test_source_classification
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_source_classification.cmake
    )
    add_test(
        NAME test_tracy_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_tracy_boundary.cmake
    )
    add_test(
        NAME test_legacy_namespace_boundary
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
            -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_legacy_namespace_boundary.cmake
    )
    if (TEXASSOLVER_BUILD_HEAVY_TESTS)
        add_test(
            NAME test_public_header_boundary
            COMMAND ${CMAKE_COMMAND}
                "-DSOURCE_DIR=${TEXASSOLVER_SOURCE_DIR}"
                -P ${TEXASSOLVER_SOURCE_DIR}/tests/cmake/test_public_header_boundary.cmake
        )
    endif()
endif()
