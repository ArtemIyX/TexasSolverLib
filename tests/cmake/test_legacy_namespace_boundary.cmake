if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" project_cmake)
if (project_cmake MATCHES "set\\(TEXASSOLVER_PUBLIC_HEADERS[^)]*legacy_namespace_compat")
    message(FATAL_ERROR "legacy namespace compatibility is installed automatically")
endif()
if (NOT project_cmake MATCHES "TEXASSOLVER_COMPATIBILITY_HEADERS")
    message(FATAL_ERROR "compatibility headers do not have an explicit classification")
endif()

file(READ "${SOURCE_DIR}/include/core/legacy_namespace_compat.hpp" compatibility_header)
if (NOT compatibility_header MATCHES "Transitional source-compatibility import")
    message(FATAL_ERROR "compatibility header does not state its opt-in boundary")
endif()

set(allowed_headers
    core/legacy_namespace_compat.hpp
    games/hunl.hpp
    games/hunl_tree.hpp
    games/kuhn.hpp
    games/leduc.hpp
    games/multiway_fixed.hpp
    games/multiway_private.hpp
    games/multiway_terminal.hpp
    preflop/preflop.hpp
    preflop/preflop_equity.hpp
    preflop/preflop_rvr.hpp
    solver/dcfr_vector.hpp
    solver/hunl_flat_mccfr.hpp
    solver/hunl_sampled_builder.hpp
    solver/hunl_sampled_terminal.hpp
    solver/hunl_sampled_traversal.hpp
    solver/multiway_baseline.hpp
    util/abstraction.hpp
    util/infoset_lookup.hpp
    util/infoset_registry.hpp)
file(GLOB_RECURSE headers RELATIVE "${SOURCE_DIR}/include" "${SOURCE_DIR}/include/*.hpp")
foreach(header IN LISTS headers)
    file(READ "${SOURCE_DIR}/include/${header}" text)
    if (text MATCHES "core/legacy_namespace_compat.hpp")
        list(FIND allowed_headers "${header}" index)
        if (index EQUAL -1)
            message(FATAL_ERROR "new compatibility import is not allowlisted: ${header}")
        endif()
    endif()
endforeach()
