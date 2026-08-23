cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Threads REQUIRED)

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" OFF)
option(TEXASSOLVER_BUILD_EXAMPLES "Build TexasSolver examples" OFF)

file(GLOB_RECURSE TEXASSOLVER_PUBLIC_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB_RECURSE TEXASSOLVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

list(FILTER TEXASSOLVER_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
if (NOT WIN32)
    list(FILTER TEXASSOLVER_SOURCES EXCLUDE REGEX ".*/util/abstraction\\.cpp$")
endif()

set(_texas_saved_build_tests "${BUILD_TESTS}")
set(_texas_saved_build_examples "${BUILD_EXAMPLES}")
set(_texas_saved_build_plo4 "${BUILD_PLO4}")
set(_texas_saved_build_plo5 "${BUILD_PLO5}")
set(_texas_saved_build_plo6 "${BUILD_PLO6}")
set(_texas_saved_build_card5 "${BUILD_CARD5}")
set(_texas_saved_build_card6 "${BUILD_CARD6}")
set(_texas_saved_build_card7 "${BUILD_CARD7}")
set(BUILD_TESTS OFF CACHE BOOL "Disable PokerHandEvaluator tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable PokerHandEvaluator examples" FORCE)
set(BUILD_PLO4 OFF CACHE BOOL "Disable PokerHandEvaluator PLO4" FORCE)
set(BUILD_PLO5 OFF CACHE BOOL "Disable PokerHandEvaluator PLO5" FORCE)
set(BUILD_PLO6 OFF CACHE BOOL "Disable PokerHandEvaluator PLO6" FORCE)
set(BUILD_CARD5 ON CACHE BOOL "Enable PokerHandEvaluator 5-card" FORCE)
set(BUILD_CARD6 ON CACHE BOOL "Enable PokerHandEvaluator 6-card" FORCE)
set(BUILD_CARD7 ON CACHE BOOL "Enable PokerHandEvaluator 7-card" FORCE)
add_subdirectory(external/pokerHandEvaluator/cpp EXCLUDE_FROM_ALL)
set(BUILD_TESTS "${_texas_saved_build_tests}" CACHE BOOL "Parent BUILD_TESTS" FORCE)
set(BUILD_EXAMPLES "${_texas_saved_build_examples}" CACHE BOOL "Parent BUILD_EXAMPLES" FORCE)
set(BUILD_PLO4 "${_texas_saved_build_plo4}" CACHE BOOL "Parent BUILD_PLO4" FORCE)
set(BUILD_PLO5 "${_texas_saved_build_plo5}" CACHE BOOL "Parent BUILD_PLO5" FORCE)
set(BUILD_PLO6 "${_texas_saved_build_plo6}" CACHE BOOL "Parent BUILD_PLO6" FORCE)
set(BUILD_CARD5 "${_texas_saved_build_card5}" CACHE BOOL "Parent BUILD_CARD5" FORCE)
set(BUILD_CARD6 "${_texas_saved_build_card6}" CACHE BOOL "Parent BUILD_CARD6" FORCE)
set(BUILD_CARD7 "${_texas_saved_build_card7}" CACHE BOOL "Parent BUILD_CARD7" FORCE)
unset(_texas_saved_build_tests)
unset(_texas_saved_build_examples)
unset(_texas_saved_build_plo4)
unset(_texas_saved_build_plo5)
unset(_texas_saved_build_plo6)
unset(_texas_saved_build_card5)
unset(_texas_saved_build_card6)
unset(_texas_saved_build_card7)

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
