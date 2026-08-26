if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(fixture_dir "${SOURCE_DIR}/tests/cmake/fixtures/evaluator_option_isolation")
foreach(case_value IN ITEMS ON OFF "")
    set(binary_dir "${CMAKE_CURRENT_BINARY_DIR}/evaluator_option_${case_value}")
    set(command
        "${CMAKE_COMMAND}" -S "${fixture_dir}" -B "${binary_dir}"
        "-DTEXASSOLVER_SOURCE_DIR=${SOURCE_DIR}")
    if (NOT case_value STREQUAL "")
        list(APPEND command "-DCASE_VALUE=${case_value}")
    endif()
    execute_process(COMMAND ${command} RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "evaluator option isolation failed for '${case_value}'")
    endif()
endforeach()
