if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)

function(require_absent needle label)
    string(FIND "${cmake_text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${label}: forbidden '${needle}'")
    endif()
endfunction()

function(require_present needle label)
    string(FIND "${cmake_text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}'")
    endif()
endfunction()

require_present(
    "option(TEXASSOLVER_BUILD_LEGACY_RESEARCH"
    "legacy vector build option")
require_present(
    "add_library(texas_legacy_vector STATIC"
    "legacy vector research target")
require_present(
    "src/preflop/preflop_rvr.cpp"
    "legacy vector source ownership")
require_present(
    "src/solver/dcfr_vector.cpp"
    "vector DCFR source ownership")
require_absent(
    "include/preflop/preflop_rvr.hpp\n    include/solver/dcfr.hpp"
    "legacy RVR header from stable public headers")
require_absent(
    "src/preflop/preflop_equity.cpp\n    src/preflop/preflop_rvr.cpp"
    "legacy RVR source from stable sources")
