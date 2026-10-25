if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" project_cmake)
file(READ "${SOURCE_DIR}/include/util/tracy.hpp" tracy_header)
file(READ "${SOURCE_DIR}/src/solver/multiway/engine/multiway_allocation_probe.cpp" allocation_probe)
file(READ "${SOURCE_DIR}/scripts/build_multiway_tracy.ps1" tracy_build_script)
file(READ "${SOURCE_DIR}/scripts/run/run_multiway_tracy_training_capture.ps1" tracy_capture_script)

foreach(required_text
    "TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS"
    "add_library(texas_tracy STATIC)"
    "target_link_libraries(texas_tracy PRIVATE Tracy::TracyClient)"
    "target_compile_definitions(texas_tracy PRIVATE TEXASSOLVER_TRACY_ENABLED=1"
    "target_compile_definitions(texas_tracy PRIVATE TEXASSOLVER_TRACY_MEMORY=1)"
    "add_executable(texas_multiway_train_tracy"
    "add_executable(texas_solver_multiway_training_scaling_tracy"
    "TEXASSOLVER_TRACY_MEMORY=ON"
    "tracy_memory_events_enabled"
    "tracy_memory_event_active"
    "record_tracy_allocation"
    "record_tracy_deallocation"
    "allocationEventCount"
    "freeEventCount"
    "Tracy capture lacks allocation memory events"
    "free_events_optional=true"
    "Tracy memory-event inspection produced no result"
)
    if(required_text STREQUAL "TEXASSOLVER_TRACY_MEMORY=ON")
        string(FIND "${tracy_build_script}" "${required_text}" position)
    elseif(required_text STREQUAL "tracy_memory_events_enabled")
        string(FIND "${tracy_header}" "${required_text}" position)
    elseif(required_text MATCHES "^tracy_memory_event_active$|^record_tracy_")
        string(FIND "${allocation_probe}" "${required_text}" position)
    elseif(required_text MATCHES "Count$|memory-event|memory events|free_events_optional")
        string(FIND "${tracy_capture_script}" "${required_text}" position)
    else()
        string(FIND "${project_cmake}" "${required_text}" position)
    endif()
    if(position EQUAL -1)
        message(FATAL_ERROR "missing Tracy memory boundary: ${required_text}")
    endif()
endforeach()

if(project_cmake MATCHES "target_link_libraries\\(texas[ \\t\\r\\n]+[^\\)]*Tracy")
    message(FATAL_ERROR "normal texas target must not link Tracy")
endif()
if(project_cmake MATCHES "install\\([^\\)]*texas_tracy")
    message(FATAL_ERROR "texas_tracy must not be installed")
endif()
if(project_cmake MATCHES "target_compile_definitions\\(texas[ \\t\\r\\n]+[^\\)]*TEXASSOLVER_TRACY_MEMORY")
    message(FATAL_ERROR "production texas target must not receive Tracy memory definitions")
endif()
if(tracy_header MATCHES "#include <tracy/Tracy.hpp>" AND
   NOT tracy_header MATCHES "TEXASSOLVER_TRACY_ENABLED")
    message(FATAL_ERROR "Tracy wrapper lacks compile-time isolation")
endif()
