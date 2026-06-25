cmake_minimum_required(VERSION 3.20)

project(TexasSolver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(texas_core
    src/core/core.cpp
)

target_include_directories(texas_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(TexasSolver
    src/main.cpp
)

target_link_libraries(TexasSolver
    PRIVATE
    texas_core
)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(TexasSolverTests
        tests/main_test.cpp
    )

    target_link_libraries(TexasSolverTests
        PRIVATE
        texas_core
        GTest::gtest_main
    )

    add_test(NAME TexasSolverTests COMMAND TexasSolverTests)
endif()
