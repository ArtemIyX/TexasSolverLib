function(texassolver_configure_core_target target)
    target_sources(${target} PRIVATE
        ${TEXASSOLVER_SOURCES}
        ${TEXASSOLVER_PUBLIC_HEADERS}
    )
    target_link_libraries(${target} PRIVATE pheval Threads::Threads)
    if (WIN32)
        target_link_libraries(${target} PRIVATE Cabinet)
    endif()
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${TEXASSOLVER_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_definitions(${target} PUBLIC
        TEXASSOLVER_BINARY_SOURCE_COMMIT_TOKEN=${TEXASSOLVER_BINARY_SOURCE_COMMIT}
        TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE_TOKEN=${TEXASSOLVER_BINARY_SOURCE_DIRTY_STATE}
        TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN=${TEXASSOLVER_BINARY_COMPILER_IDENTITY_TOKEN}
        TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN=${TEXASSOLVER_BINARY_BUILD_CONFIGURATION_TOKEN})
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /FS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(texassolver_configure_internal_target target)
    target_include_directories(${target}
        PUBLIC $<BUILD_INTERFACE:${TEXASSOLVER_SOURCE_DIR}/include>)
    target_compile_features(${target} PUBLIC cxx_std_17)
endfunction()

function(texassolver_configure_example_target target)
    target_include_directories(${target}
        PRIVATE ${TEXASSOLVER_SOURCE_DIR}/include)
endfunction()

if (WIN32)
    list(APPEND TEXASSOLVER_SOURCES src/util/abstraction.cpp)
else()
    list(APPEND TEXASSOLVER_SOURCES src/util/abstraction_stub.cpp)
endif()

set(_texassolver_evaluator_options
    BUILD_TESTS BUILD_EXAMPLES BUILD_PLO4 BUILD_PLO5 BUILD_PLO6 BUILD_CARD5 BUILD_CARD6 BUILD_CARD7)
foreach(_texassolver_option IN LISTS _texassolver_evaluator_options)
    if (DEFINED ${_texassolver_option})
        set(_texassolver_saved_defined_${_texassolver_option} TRUE)
        set(_texassolver_saved_value_${_texassolver_option} "${${_texassolver_option}}")
    endif()
endforeach()
set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_PLO4 OFF)
set(BUILD_PLO5 OFF)
set(BUILD_PLO6 OFF)
set(BUILD_CARD5 ON)
set(BUILD_CARD6 ON)
set(BUILD_CARD7 ON)
add_subdirectory(external/pokerHandEvaluator/cpp EXCLUDE_FROM_ALL)
foreach(_texassolver_option IN LISTS _texassolver_evaluator_options)
    if (_texassolver_saved_defined_${_texassolver_option})
        set(${_texassolver_option} "${_texassolver_saved_value_${_texassolver_option}}")
    else()
        unset(${_texassolver_option})
    endif()
endforeach()
unset(_texassolver_evaluator_options)

add_library(texas STATIC)
add_library(TexasSolver::texas ALIAS texas)
texassolver_configure_core_target(texas)

if (TEXASSOLVER_ENABLE_LOGGING)
    add_library(texas_logging STATIC ${TEXASSOLVER_APPLICATION_SOURCES})
    add_library(TexasSolver::logging ALIAS texas_logging)
    texassolver_configure_internal_target(texas_logging)
    target_link_libraries(texas_logging PUBLIC spdlog::spdlog PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(texas_logging PUBLIC TEXASSOLVER_ENABLE_LOGGING=1)
    if (MSVC)
        target_compile_options(texas_logging PRIVATE /W4 /FS)
    else()
        target_compile_options(texas_logging PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    add_library(texas_logging STATIC ${TEXASSOLVER_APPLICATION_DISABLED_SOURCES})
    add_library(TexasSolver::logging ALIAS texas_logging)
    texassolver_configure_internal_target(texas_logging)
    target_compile_definitions(texas_logging PUBLIC TEXASSOLVER_ENABLE_LOGGING=0)
endif()

set_target_properties(texas PROPERTIES
    EXPORT_NAME texas
    OUTPUT_NAME texas
)

if (TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS)
    if (NOT EXISTS "${TEXASSOLVER_SOURCE_DIR}/external/tracy/CMakeLists.txt")
        message(FATAL_ERROR "TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS requires vendored external/tracy")
    endif()
    set(TRACY_STATIC ON CACHE BOOL "" FORCE)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ${TEXASSOLVER_TRACY_ON_DEMAND} CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_IPV4 ON CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_SAMPLING ON CACHE BOOL "" FORCE)
    if (TEXASSOLVER_TRACY_SAMPLING)
        set(TRACY_NO_SAMPLING OFF CACHE BOOL "" FORCE)
    endif()
    set(TRACY_NO_CALLSTACK ON CACHE BOOL "" FORCE)
    if (TEXASSOLVER_TRACY_CALLSTACKS)
        set(TRACY_NO_CALLSTACK OFF CACHE BOOL "" FORCE)
    endif()
    set(TRACY_LTO OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_EXIT OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_CONTEXT_SWITCH OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/tracy EXCLUDE_FROM_ALL)

    add_library(texas_tracy STATIC)
    texassolver_configure_core_target(texas_tracy)
    target_compile_definitions(texas_tracy PRIVATE TEXASSOLVER_TRACY_ENABLED=1)
    if (TEXASSOLVER_TRACY_MEMORY)
        target_compile_definitions(texas_tracy PRIVATE TEXASSOLVER_TRACY_MEMORY=1)
    endif()
    target_link_libraries(texas_tracy PRIVATE Tracy::TracyClient)
    set_target_properties(texas_tracy PROPERTIES OUTPUT_NAME texas_tracy)
endif()

if (TEXASSOLVER_BUILD_LEGACY_RESEARCH)
    add_library(texas_legacy_vector STATIC
        ${TEXASSOLVER_LEGACY_RESEARCH_SOURCES}
    )
    add_library(TexasSolver::legacy_vector ALIAS texas_legacy_vector)
    texassolver_configure_internal_target(texas_legacy_vector)
    target_link_libraries(texas_legacy_vector PUBLIC texas)
endif()

if (TEXASSOLVER_BUILD_TESTS OR TEXASSOLVER_BUILD_RESEARCH_EXAMPLES)
    add_library(texas_hunl_flat_mccfr_research STATIC
        ${TEXASSOLVER_FLAT_MCCFR_RESEARCH_SOURCES}
    )
    add_library(TexasSolver::hunl_flat_mccfr_research ALIAS texas_hunl_flat_mccfr_research)
    texassolver_configure_internal_target(texas_hunl_flat_mccfr_research)
    target_link_libraries(texas_hunl_flat_mccfr_research PUBLIC texas)
endif()

if (TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH)
    add_library(texas_hunl_fixed_research STATIC
        ${TEXASSOLVER_FIXED_RESEARCH_SOURCES}
    )
    texassolver_configure_internal_target(texas_hunl_fixed_research)
    target_link_libraries(texas_hunl_fixed_research PUBLIC texas)
    add_library(TexasSolver::hunl_fixed_research ALIAS texas_hunl_fixed_research)
endif()

install(TARGETS texas
    EXPORT TexasSolverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set_target_properties(pheval PROPERTIES
    PUBLIC_HEADER
        "${TEXASSOLVER_SOURCE_DIR}/external/pokerHandEvaluator/cpp/include/phevaluator/phevaluator.h;${TEXASSOLVER_SOURCE_DIR}/external/pokerHandEvaluator/cpp/include/phevaluator/card.h;${TEXASSOLVER_SOURCE_DIR}/external/pokerHandEvaluator/cpp/include/phevaluator/rank.h")

install(TARGETS pheval
    EXPORT TexasSolverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phevaluator
)

foreach(public_header IN LISTS TEXASSOLVER_PUBLIC_HEADERS)
    file(RELATIVE_PATH public_header_relative ${TEXASSOLVER_SOURCE_DIR}/include ${TEXASSOLVER_SOURCE_DIR}/${public_header})
    get_filename_component(public_header_directory ${public_header_relative} DIRECTORY)
    install(
        FILES ${public_header}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${public_header_directory})
endforeach()

install(EXPORT TexasSolverTargets
    NAMESPACE TexasSolver::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexasSolver
)

configure_package_config_file(
    ${TEXASSOLVER_SOURCE_DIR}/cmake/TexasSolverConfig.cmake.in
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
