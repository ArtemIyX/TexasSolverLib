if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)

function(require_present needle label)
    string(FIND "${cmake_text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}'")
    endif()
endfunction()

require_present(
    "option(TEXASSOLVER_BUILD_RESEARCH_EXAMPLES"
    "research example option")
require_present(
    "if (TEXASSOLVER_BUILD_RESEARCH_EXAMPLES)"
    "research example guard")
require_present(
    "examples/solve_kuhn.cpp"
    "stable example")

