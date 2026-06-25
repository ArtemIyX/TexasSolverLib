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
    src/core/dcfr_vector.cpp
    src/core/exploit.cpp
    src/core/hunl.cpp
    src/core/hunl_eval.cpp
    src/core/hunl_solver.cpp
    src/core/hunl_tree.cpp
    src/core/kuhn.cpp
    src/core/layout.cpp
    src/core/leduc.cpp
    src/core/pcs.cpp
    src/core/preflop.cpp
    src/core/preflop_equity.cpp
    src/core/preflop_rvr.cpp
    src/core/simd.cpp
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

function(add_texas_core_test test_name test_source)
    add_executable(${test_name}
        tests/test_main.cpp
        ${test_source}
    )

    target_link_libraries(${test_name}
        PRIVATE
        texas_core
    )

    target_include_directories(${test_name}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_texas_core_test(texas_core_test_arena tests/test_arena.cpp)
add_texas_core_test(texas_core_test_dcfr tests/test_dcfr.cpp)
add_texas_core_test(texas_core_test_dcfr_vector tests/test_dcfr_vector.cpp)
add_texas_core_test(texas_core_test_hunl_eval tests/test_hunl_eval.cpp)
add_texas_core_test(texas_core_test_hunl_state tests/test_hunl_state.cpp)
add_texas_core_test(texas_core_test_hunl_tree tests/test_hunl_tree.cpp)
add_texas_core_test(texas_core_test_kuhn tests/test_kuhn.cpp)
add_texas_core_test(texas_core_test_layout tests/test_layout.cpp)
add_texas_core_test(texas_core_test_leduc tests/test_leduc.cpp)
add_texas_core_test(texas_core_test_pcs tests/test_pcs.cpp)
add_texas_core_test(texas_core_test_simd tests/test_simd.cpp)
