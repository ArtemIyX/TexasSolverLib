if (TEXASSOLVER_BUILD_EXAMPLES)
    add_executable(texas_solver_example
        ${TEXASSOLVER_SOURCE_DIR}/examples/solve_kuhn.cpp
    )
    target_link_libraries(texas_solver_example PRIVATE texas)
    texassolver_configure_example_target(texas_solver_example)

    if (TEXASSOLVER_BUILD_RESEARCH_EXAMPLES)
        foreach(_multiway_workflow train buckets inspect evaluate finalize)
            add_executable(texas_multiway_${_multiway_workflow}
                ${TEXASSOLVER_SOURCE_DIR}/examples/multiway_workflow_main.cpp
            )
            target_link_libraries(texas_multiway_${_multiway_workflow} PRIVATE texas)
            target_link_libraries(texas_multiway_${_multiway_workflow} PRIVATE TexasSolver::logging)
            texassolver_configure_example_target(texas_multiway_${_multiway_workflow})
        endforeach()

        add_executable(texas_solver_hunl_mccfr_postflop
            ${TEXASSOLVER_SOURCE_DIR}/examples/hunl_mccfr_postflop_main.cpp
        )
        target_link_libraries(texas_solver_hunl_mccfr_postflop PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_hunl_mccfr_scaling
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/hunl_mccfr_scaling_main.cpp
        )
        target_link_libraries(texas_solver_hunl_mccfr_scaling PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_hunl_random_flat
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/hunl_random_flat_main.cpp
        )
        target_link_libraries(texas_solver_hunl_random_flat PRIVATE texas_hunl_flat_mccfr_research)

        add_executable(texas_solver_benchmark
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/main.cpp
        )
        target_link_libraries(texas_solver_benchmark PRIVATE texas)
        texassolver_configure_example_target(texas_solver_benchmark)

        add_executable(texas_solver_flat_scheduler_benchmark
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/flat_scheduler_main.cpp
        )
        target_link_libraries(texas_solver_flat_scheduler_benchmark PRIVATE texas)
        texassolver_configure_example_target(texas_solver_flat_scheduler_benchmark)

        add_executable(texas_solver_hunl_backend_compare
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/hunl_backend_compare_main.cpp
        )
        target_link_libraries(texas_solver_hunl_backend_compare PRIVATE texas)
        texassolver_configure_example_target(texas_solver_hunl_backend_compare)

        add_executable(texas_solver_multiway_training_scaling
            ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/multiway_training_scaling_main.cpp
        )
        target_link_libraries(texas_solver_multiway_training_scaling PRIVATE texas)
        texassolver_configure_example_target(texas_solver_multiway_training_scaling)

        if (TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS)
            add_executable(texas_multiway_train_tracy
                ${TEXASSOLVER_SOURCE_DIR}/examples/multiway_workflow_main.cpp
            )
            target_link_libraries(texas_multiway_train_tracy PRIVATE texas_tracy Tracy::TracyClient)
            target_link_libraries(texas_multiway_train_tracy PRIVATE TexasSolver::logging)
            target_compile_definitions(texas_multiway_train_tracy PRIVATE TEXASSOLVER_TRACY_ENABLED=1)
            if (TEXASSOLVER_TRACY_MEMORY)
                target_compile_definitions(texas_multiway_train_tracy PRIVATE TEXASSOLVER_TRACY_MEMORY=1)
            endif()
            target_include_directories(texas_multiway_train_tracy PRIVATE
                ${TEXASSOLVER_SOURCE_DIR}/include
                ${TEXASSOLVER_SOURCE_DIR}/external/tracy/public
            )

            add_executable(texas_solver_multiway_training_scaling_tracy
                ${TEXASSOLVER_SOURCE_DIR}/examples/benchmarks/multiway_training_scaling_main.cpp
            )
            target_link_libraries(texas_solver_multiway_training_scaling_tracy PRIVATE texas_tracy Tracy::TracyClient)
            target_compile_definitions(texas_solver_multiway_training_scaling_tracy PRIVATE TEXASSOLVER_TRACY_ENABLED=1)
            if (TEXASSOLVER_TRACY_MEMORY)
                target_compile_definitions(texas_solver_multiway_training_scaling_tracy PRIVATE TEXASSOLVER_TRACY_MEMORY=1)
            endif()
            target_include_directories(texas_solver_multiway_training_scaling_tracy PRIVATE
                ${TEXASSOLVER_SOURCE_DIR}/include
                ${TEXASSOLVER_SOURCE_DIR}/external/tracy/public
            )
        endif()
    endif()

endif()
