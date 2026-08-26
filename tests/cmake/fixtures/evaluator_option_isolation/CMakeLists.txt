cmake_minimum_required(VERSION 3.20)

project(EvaluatorOptionIsolation LANGUAGES CXX)

if (DEFINED CASE_VALUE)
    set(BUILD_TESTS "${CASE_VALUE}")
    set(BUILD_EXAMPLES "${CASE_VALUE}")
    set(BUILD_PLO4 "${CASE_VALUE}")
    set(BUILD_PLO5 "${CASE_VALUE}")
    set(BUILD_PLO6 "${CASE_VALUE}")
    set(BUILD_CARD5 "${CASE_VALUE}")
    set(BUILD_CARD6 "${CASE_VALUE}")
    set(BUILD_CARD7 "${CASE_VALUE}")
endif()

add_subdirectory("${TEXASSOLVER_SOURCE_DIR}" texas_solver)

foreach(option IN ITEMS
    BUILD_TESTS BUILD_EXAMPLES BUILD_PLO4 BUILD_PLO5 BUILD_PLO6
    BUILD_CARD5 BUILD_CARD6 BUILD_CARD7)
    if (DEFINED CASE_VALUE)
        if (NOT "${${option}}" STREQUAL "${CASE_VALUE}")
            message(FATAL_ERROR "${option} changed from ${CASE_VALUE} to ${${option}}")
        endif()
    elseif (DEFINED ${option})
        message(FATAL_ERROR "${option} was introduced into an undefined parent scope")
    endif()
endforeach()
