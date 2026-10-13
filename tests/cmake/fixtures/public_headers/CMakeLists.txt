cmake_minimum_required(VERSION 3.20)
project(TexasSolverPublicHeaderContract LANGUAGES CXX)

add_subdirectory("${TEXASSOLVER_SOURCE_DIR}" texas_solver)
add_executable(public_header_contract main.cpp)
target_link_libraries(public_header_contract PRIVATE TexasSolver::texas)
