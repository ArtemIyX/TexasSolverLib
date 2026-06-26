cmake_minimum_required(VERSION 3.20)

project(TexasSolver VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TEXASSOLVER_BUILD_TESTS "Build TexasSolver tests" ON)

add_library(texas_core)
add_library(TexasSolver::texas_core ALIAS texas_core)

target_sources(texas_core
    PRIVATE
        src/core/abstraction.cpp
        src/core/arena.cpp
        src/core/core.cpp
        src/core/dcfr.cpp
        src/core/dcfr_vector.cpp
        src/core/dcfr_vector_parallel.cpp
        src/core/exploit.cpp
        src/core/hunl.cpp
        src/core/hunl_eval.cpp
        src/core/hunl_solver.cpp
        src/core/hunl_tree.cpp
        src/core/kuhn.cpp
        src/core/layout.cpp
        src/core/leduc.cpp
        src/core/lib.cpp
        src/core/pcs.cpp
        src/core/preflop.cpp
        src/core/preflop_equity.cpp
        src/core/preflop_rvr.cpp
        src/core/simd.cpp
        src/core/solver.cpp
        src/core/suit_iso.cpp
)

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

if (TEXASSOLVER_BUILD_TESTS)
    include(CTest)
    if (BUILD_TESTING)
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
endif()
