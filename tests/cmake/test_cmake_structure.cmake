if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${SOURCE_DIR}/cmake/TexasSolverTargets.cmake" target_cmake)
file(READ "${SOURCE_DIR}/examples/CMakeLists.txt" example_cmake)
file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" test_cmake)

foreach(required_type_header
    "include/core/action_id.hpp"
    "include/core/value_types.hpp"
    "include/core/street_types.hpp"
    "include/games/hunl_action_types.hpp"
    "include/games/hunl_flat_graph_types.hpp"
    "include/games/hunl_terminal_types.hpp"
    "include/games/multiway_rake_types.hpp"
    "include/games/multiway_state_types.hpp"
    "include/solver/hunl/flat/hunl_flat_types.hpp"
    "include/solver/hunl/sampled/hunl_sampled_config_types.hpp"
    "include/solver/hunl/sampled/hunl_sampled_export_types.hpp"
    "include/solver/hunl/sampled/hunl_sampled_profile_types.hpp"
    "include/solver/multiway/abstraction/multiway_future_bucket_types.hpp"
    "include/solver/multiway/abstraction/multiway_model_identity_types.hpp"
    "include/solver/multiway/blueprint/multiway_blueprint_config_types.hpp")
    string(FIND "${root_cmake}" "${required_type_header}" type_header_position)
    if(type_header_position EQUAL -1)
        message(FATAL_ERROR "public types header is not installed: ${required_type_header}")
    endif()
endforeach()

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
if(NOT target_cmake MATCHES "add_library\\(texas_types INTERFACE\\)")
    message(FATAL_ERROR "standalone public types interface target is missing")
endif()
if(NOT target_cmake MATCHES "add_library\\(TexasSolver::types ALIAS texas_types\\)")
    message(FATAL_ERROR "build-tree public types alias is missing")
endif()
if(NOT target_cmake MATCHES "set_target_properties\\(texas_types PROPERTIES EXPORT_NAME types\\)")
    message(FATAL_ERROR "installed public types target must export as TexasSolver::types")
endif()
string(FIND "${target_cmake}" "target_include_directories(texas_types" types_include_position)
if(types_include_position EQUAL -1)
    message(FATAL_ERROR "public types target must expose include usage requirements")
endif()
if(NOT target_cmake MATCHES "target_compile_features\\(texas_types INTERFACE cxx_std_17\\)")
    message(FATAL_ERROR "public types target must require C++17")
endif()
if(target_cmake MATCHES "target_link_libraries\\(texas_types")
    message(FATAL_ERROR "public types target must not propagate solver dependencies")
endif()
if(NOT target_cmake MATCHES "target_link_libraries\\(texas PUBLIC texas_types\\)")
    message(FATAL_ERROR "solver target must inherit public types usage requirements")
endif()
string(FIND "${target_cmake}" "install(TARGETS texas_types" types_install_position)
if(types_install_position EQUAL -1)
    message(FATAL_ERROR "public types target must use the existing install export")
endif()
string(SUBSTRING "${target_cmake}" ${types_install_position} -1 types_install_tail)
string(FIND "${types_install_tail}" "EXPORT TexasSolverTargets" types_export_position)
if(types_export_position EQUAL -1)
    message(FATAL_ERROR "public types target must use the existing install export")
endif()
if(NOT example_cmake MATCHES "if \\(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES\\)")
    message(FATAL_ERROR "research examples must remain independently selected")
endif()
if(NOT test_cmake MATCHES "if \\(NOT TEXASSOLVER_BUILD_HEAVY_TESTS")
    message(FATAL_ERROR "heavy tests must remain independently selected")
endif()
