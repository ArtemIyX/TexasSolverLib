if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${SOURCE_DIR}/examples/multiway_workflow_main.cpp" source_text)

string(FIND "${cmake_text}" "foreach(_multiway_workflow train buckets inspect evaluate)" target_position)
if(target_position EQUAL -1)
    message(FATAL_ERROR "missing complete multiway research target generator")
endif()

foreach(option "--help" "--config" "--seed")
    string(FIND "${source_text}" "${option}" option_position)
    if(option_position EQUAL -1)
        message(FATAL_ERROR "workflow front end does not support ${option}")
    endif()
endforeach()

if(source_text MATCHES "install\\(")
    message(FATAL_ERROR "multiway workflow front end must remain non-installed")
endif()
