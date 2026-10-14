if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" project_cmake)
file(READ "${SOURCE_DIR}/include/util/tracy.hpp" tracy_header)

foreach(required_text
    "TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS"
    "add_library(texas_tracy STATIC)"
    "target_link_libraries(texas_tracy PRIVATE Tracy::TracyClient)"
    "target_compile_definitions(texas_tracy PRIVATE TEXASSOLVER_TRACY_ENABLED=1"
    "add_executable(texas_multiway_train_tracy"
    "add_executable(texas_solver_multiway_training_scaling_tracy")
    string(FIND "${project_cmake}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing Tracy build boundary: ${required_text}")
    endif()
endforeach()

if(project_cmake MATCHES "target_link_libraries\\(texas[^\\n]*Tracy")
    message(FATAL_ERROR "normal texas target must not link Tracy")
endif()
if(project_cmake MATCHES "install\\([^\\)]*texas_tracy")
    message(FATAL_ERROR "texas_tracy must not be installed")
endif()
if(tracy_header MATCHES "#include <tracy/Tracy.hpp>" AND
   NOT tracy_header MATCHES "TEXASSOLVER_TRACY_ENABLED")
    message(FATAL_ERROR "Tracy wrapper lacks compile-time isolation")
endif()
