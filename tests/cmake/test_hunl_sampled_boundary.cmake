if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/include/solver/hunl_sampled_solver.hpp" SOLVER_HEADER)
file(READ "${SOURCE_DIR}/src/solver/hunl_sampled_solver.cpp" SOLVER_SOURCE)
file(READ "${SOURCE_DIR}/CMakeLists.txt" PROJECT_CMAKE)

if (SOLVER_HEADER MATCHES "root_state")
    message(FATAL_ERROR "production sampled request still exposes a fixed-private root")
endif()
if (SOLVER_SOURCE MATCHES "root_state")
    message(FATAL_ERROR "production sampled implementation still dispatches fixed-private roots")
endif()
if (PROJECT_CMAKE MATCHES "include/solver/hunl_sampled_builder.hpp" OR
    PROJECT_CMAKE MATCHES "include/solver/hunl_sampled_terminal.hpp" OR
    PROJECT_CMAKE MATCHES "include/solver/hunl_sampled_traversal.hpp")
    message(FATAL_ERROR "fixed-private sampled implementation remains an installed stable header")
endif()
if (NOT SOLVER_HEADER MATCHES "HUNLStructuredRootRequest structured_root")
    message(FATAL_ERROR "production sampled request lacks its explicit structured root")
endif()
