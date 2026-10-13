if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

foreach(header IN ITEMS
    core/types.hpp
    core/arena.hpp
    core/canonical_combo.hpp
    core/game.hpp
    core/fingerprint.hpp
    core/poker.hpp)
    file(READ "${SOURCE_DIR}/include/${header}" header_text)
    string(FIND "${header_text}" "core/namespaces.hpp" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "core header imports namespace flattening: ${header}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/include/core/types.hpp" types_text)
if (types_text MATCHES "std::hash<texas::InfosetId>")
    message(FATAL_ERROR "core type hash specialization still depends on a flattened root alias")
endif()

file(READ "${SOURCE_DIR}/include/core/namespaces.hpp" namespace_text)
if (namespace_text MATCHES "using namespace")
    message(FATAL_ERROR "namespace declaration header still imports flattened namespaces")
endif()
file(GLOB_RECURSE all_headers "${SOURCE_DIR}/include/*.hpp")
foreach(header_path IN LISTS all_headers)
    if (NOT header_path MATCHES "legacy_namespace_compat\\.hpp$")
        file(READ "${header_path}" header_text)
        if (header_text MATCHES "core/namespaces\\.hpp")
            message(FATAL_ERROR "internal header still imports the namespace declaration shim: ${header_path}")
        endif()
    endif()
endforeach()
