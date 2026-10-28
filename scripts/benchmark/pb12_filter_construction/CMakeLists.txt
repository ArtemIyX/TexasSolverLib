cmake_minimum_required(VERSION 3.20)
project(pb12_filter_construction LANGUAGES CXX)

get_filename_component(TEXASSOLVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(pb12_filter_construction main.cpp)
target_compile_features(pb12_filter_construction PRIVATE cxx_std_17)
target_include_directories(pb12_filter_construction PRIVATE "${TEXASSOLVER_ROOT}/include")
target_link_libraries(pb12_filter_construction PRIVATE
    "${TEXASSOLVER_ROOT}/build/$<CONFIG>/texas.lib"
    "${TEXASSOLVER_ROOT}/build/external/pokerHandEvaluator/cpp/$<CONFIG>/pheval.lib"
    Cabinet)
