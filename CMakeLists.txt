cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" ON)
option(TEXASSOLVER_BUILD_EXAMPLES "Build TexasSolver examples" ON)
option(TEXASSOLVER_USE_POKER_HAND_EVALUATOR "Build vendored PokerHandEvaluator" ON)

file(GLOB_RECURSE TEXASSOLVER_PUBLIC_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB_RECURSE TEXASSOLVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

list(FILTER TEXASSOLVER_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

if (TEXASSOLVER_USE_POKER_HAND_EVALUATOR)
    set(BUILD_TESTS OFF CACHE BOOL "Disable PokerHandEvaluator tests" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable PokerHandEvaluator examples" FORCE)
    set(BUILD_PLO4 OFF CACHE BOOL "Disable PokerHandEvaluator PLO4" FORCE)
    set(BUILD_PLO5 OFF CACHE BOOL "Disable PokerHandEvaluator PLO5" FORCE)
    set(BUILD_PLO6 OFF CACHE BOOL "Disable PokerHandEvaluator PLO6" FORCE)
    set(BUILD_CARD5 ON CACHE BOOL "Enable PokerHandEvaluator 5-card" FORCE)
    set(BUILD_CARD6 ON CACHE BOOL "Enable PokerHandEvaluator 6-card" FORCE)
    set(BUILD_CARD7 ON CACHE BOOL "Enable PokerHandEvaluator 7-card" FORCE)
    add_subdirectory(external/pokerHandEvaluator/cpp EXCLUDE_FROM_ALL)
endif()

add_library(texas_core STATIC)
add_library(TexasSolver::texas_core ALIAS texas_core)

target_sources(texas_core PRIVATE ${TEXASSOLVER_SOURCES})
if (TEXASSOLVER_USE_POKER_HAND_EVALUATOR)
    target_link_libraries(texas_core PRIVATE pheval)
endif()

target_include_directories(texas_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(texas_core PUBLIC cxx_std_17)

set_target_properties(texas_core PROPERTIES
    EXPORT_NAME texas_core
    OUTPUT_NAME texas_core
)

if (MSVC)
    target_compile_options(texas_core PRIVATE /W4)
else()
    target_compile_options(texas_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS texas_core
    EXPORT TexasSolverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (TEXASSOLVER_USE_POKER_HAND_EVALUATOR)
    install(TARGETS pheval
        EXPORT TexasSolverTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phevaluator
    )
endif()

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
    target_link_libraries(texas_solver_example PRIVATE texas_core)
    target_include_directories(texas_solver_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(texas_solver_benchmark
        examples/benchmarks/main.cpp
    )
    target_link_libraries(texas_solver_benchmark PRIVATE texas_core)
    target_include_directories(texas_solver_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
        target_link_libraries(${test_name} PRIVATE texas_core)

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
