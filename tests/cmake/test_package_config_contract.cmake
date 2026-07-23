if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(config_path "${SOURCE_DIR}/cmake/TexasSolverConfig.cmake.in")
file(READ "${config_path}" config_text)
set(assertions_passed 0)

function(require_contains needle label)
    string(FIND "${config_text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}'")
    endif()
    math(EXPR next_count "${assertions_passed} + 1")
    set(assertions_passed "${next_count}" PARENT_SCOPE)
endfunction()

function(require_absent needle label)
    string(FIND "${config_text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${label}: forbidden '${needle}'")
    endif()
    math(EXPR next_count "${assertions_passed} + 1")
    set(assertions_passed "${next_count}" PARENT_SCOPE)
endfunction()

function(require_once needle label)
    string(FIND "${config_text}" "${needle}" first_position)
    if(first_position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}'")
    endif()
    string(LENGTH "${needle}" needle_length)
    math(EXPR tail_begin "${first_position} + ${needle_length}")
    string(SUBSTRING "${config_text}" "${tail_begin}" -1 tail)
    string(FIND "${tail}" "${needle}" second_position)
    if(NOT second_position EQUAL -1)
        message(FATAL_ERROR "${label}: duplicate '${needle}'")
    endif()
    math(EXPR next_count "${assertions_passed} + 1")
    set(assertions_passed "${next_count}" PARENT_SCOPE)
endfunction()

function(require_before earlier later label)
    string(FIND "${config_text}" "${earlier}" earlier_position)
    string(FIND "${config_text}" "${later}" later_position)
    if(earlier_position EQUAL -1 OR
       later_position EQUAL -1 OR
       earlier_position GREATER_EQUAL later_position)
        message(FATAL_ERROR "${label}: required ordering is missing")
    endif()
    math(EXPR next_count "${assertions_passed} + 1")
    set(assertions_passed "${next_count}" PARENT_SCOPE)
endfunction()

require_contains("@PACKAGE_INIT@" "package initialization")
require_contains("include(CMakeFindDependencyMacro)" "dependency macro include")
require_contains("find_dependency(Threads)" "thread dependency")
require_contains(
    [=[include("${CMAKE_CURRENT_LIST_DIR}/TexasSolverTargets.cmake")]=]
    "relocatable targets import")
require_contains(
    "check_required_components(TexasSolver)"
    "required component check")

require_once("@PACKAGE_INIT@" "single package initialization")
require_once(
    "include(CMakeFindDependencyMacro)"
    "single dependency macro include")
require_once("find_dependency(Threads)" "single thread dependency")
require_once("TexasSolverTargets.cmake" "single targets import")
require_once(
    "check_required_components(TexasSolver)"
    "single component check")

require_before(
    "@PACKAGE_INIT@"
    "include(CMakeFindDependencyMacro)"
    "package init before dependency macro")
require_before(
    "include(CMakeFindDependencyMacro)"
    "find_dependency(Threads)"
    "dependency macro before lookup")
require_before(
    "find_dependency(Threads)"
    "TexasSolverTargets.cmake"
    "dependency before imported targets")
require_before(
    "TexasSolverTargets.cmake"
    "check_required_components(TexasSolver)"
    "targets before component validation")

require_absent("find_package(Threads" "must preserve REQUIRED propagation")
require_absent("Threads::Threads" "config must not synthesize dependency targets")
require_absent("CMAKE_SOURCE_DIR" "no consumer source-root coupling")
require_absent("PROJECT_SOURCE_DIR" "no producer source-root coupling")
require_absent("CMAKE_BINARY_DIR" "no consumer binary-root coupling")
require_absent("PROJECT_BINARY_DIR" "no producer binary-root coupling")
require_absent("TexasSolverTargets.cmake.in" "must import generated targets")
require_absent("CMAKE_CURRENT_SOURCE_DIR" "must remain install relocatable")

if(assertions_passed LESS 20)
    message(FATAL_ERROR "package contract must execute at least 20 assertions")
endif()
