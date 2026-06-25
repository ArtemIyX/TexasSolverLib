cmake_minimum_required(VERSION 3.20)

project(TexasSolver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(TexasSolver
    src/main.cpp
)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(TexasSolverTests
        tests/main_test.cpp
    )

    target_link_libraries(TexasSolverTests
        PRIVATE
        GTest::gtest_main
    )

    add_test(NAME TexasSolverTests COMMAND TexasSolverTests)
endif()
