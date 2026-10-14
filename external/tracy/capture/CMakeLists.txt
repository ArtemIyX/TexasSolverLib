cmake_minimum_required(VERSION 3.16)

set(NO_STATISTICS ON)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/version.cmake)

set(CMAKE_CXX_STANDARD 20)

project(
    tracy-capture
    LANGUAGES C CXX
    VERSION ${TRACY_VERSION_STRING}
)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/config.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/vendor.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/server.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/GitRef.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/util.cmake)

set(PROGRAM_FILES
    src/capture.cpp
)

add_executable(${PROJECT_NAME} ${PROGRAM_FILES})
add_git_ref(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PRIVATE TracyServer TracyUtil TracyGetOpt)
set_property(DIRECTORY ${CMAKE_CURRENT_LIST_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(tracy-capture-daemon src/capturedaemon.cpp)
add_git_ref(tracy-capture-daemon)
target_link_libraries(tracy-capture-daemon PRIVATE TracyServer TracyUtil TracyGetOpt)

install(TARGETS tracy-capture-daemon DESTINATION ${CMAKE_INSTALL_BINDIR})