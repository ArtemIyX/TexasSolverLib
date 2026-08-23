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
