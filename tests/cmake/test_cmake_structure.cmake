if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${SOURCE_DIR}/cmake/TexasSolverTargets.cmake" target_cmake)
file(READ "${SOURCE_DIR}/examples/CMakeLists.txt" example_cmake)
file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" test_cmake)

foreach(required_text
    "external/pokerHandEvaluator/cpp/include/phevaluator/phevaluator.h"
    "external/pokerHandEvaluator/cpp/include/phevaluator/card.h"
    "external/pokerHandEvaluator/cpp/include/phevaluator/rank.h")
    string(FIND "${target_cmake}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "installed evaluator header source is missing '${required_text}'")
    endif()
endforeach()

foreach(required_text
    "cmake_minimum_required(VERSION 3.20)"
    "option(TEXASSOLVER_BUILD_TESTS"
    "option(TEXASSOLVER_BUILD_HEAVY_TESTS"
    "option(TEXASSOLVER_BUILD_EXAMPLES"
    "option(TEXASSOLVER_BUILD_LEGACY_RESEARCH"
    "option(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES"
    "option(TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH"
    "option(TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS"
    "option(TEXASSOLVER_ENABLE_LOGGING"
    "include(cmake/TexasSolverTargets.cmake)"
    "add_subdirectory(examples)"
    "add_subdirectory(tests)")
    string(FIND "${root_cmake}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "root CMake contract is missing '${required_text}'")
    endif()
endforeach()

if(root_cmake MATCHES "add_(library|executable|test)\\(")
    message(FATAL_ERROR "root CMakeLists.txt must delegate target and test declarations")
endif()
if(NOT target_cmake MATCHES "target_compile_features\\([^)]*PUBLIC cxx_std_17")
    message(FATAL_ERROR "production C++17 requirement must remain target scoped")
endif()
if(NOT target_cmake MATCHES "target_link_libraries\\(\\$\\{target\\} PRIVATE pheval Threads::Threads\\)")
    message(FATAL_ERROR "core implementation dependencies must remain private")
endif()
if(NOT example_cmake MATCHES "if \\(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES\\)")
    message(FATAL_ERROR "research examples must remain independently selected")
endif()
if(NOT test_cmake MATCHES "if \\(NOT TEXASSOLVER_BUILD_HEAVY_TESTS")
    message(FATAL_ERROR "heavy tests must remain independently selected")
endif()
