cmake_minimum_required(VERSION 3.20)

project(TexasSolver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

add_library(texas_core
    src/core/arena.cpp
    src/core/core.cpp
    src/core/dcfr.cpp
    src/core/exploit.cpp
    src/core/hunl.cpp
    src/core/hunl_eval.cpp
    src/core/hunl_solver.cpp
    src/core/hunl_tree.cpp
    src/core/kuhn.cpp
    src/core/leduc.cpp
    src/core/solver.cpp
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

add_executable(texas_core_tests
    tests/test_main.cpp
    tests/test_arena.cpp
    tests/test_dcfr.cpp
    tests/test_hunl_eval.cpp
    tests/test_hunl_state.cpp
    tests/test_hunl_tree.cpp
    tests/test_kuhn.cpp
    tests/test_leduc.cpp
)

target_link_libraries(texas_core_tests
    PRIVATE
    texas_core
)

target_include_directories(texas_core_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_test(NAME texas_core_tests COMMAND texas_core_tests)
