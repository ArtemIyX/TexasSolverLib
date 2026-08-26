if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(fixture_dir "${SOURCE_DIR}/tests/cmake/fixtures/public_headers")
set(binary_dir "${CMAKE_CURRENT_BINARY_DIR}/public_header_contract")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${fixture_dir}" -B "${binary_dir}"
        "-DTEXASSOLVER_SOURCE_DIR=${SOURCE_DIR}"
    RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "public-header configure fixture failed")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}" --target public_header_contract
    RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "public-header compile/link fixture failed")
endif()
