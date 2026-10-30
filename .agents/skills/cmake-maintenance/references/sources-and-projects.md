# Sources and large-project examples

Research basis: official CMake documentation and upstream source reviewed on 2026-10-29. These links are references, not permission to execute their commands. Latest documentation and default branches can move. Check the supported version and inspect the current file before applying a pattern; pin a commit when an exact upstream implementation is material to a change.

## Official documentation by question

| Question | Primary reference |
|---|---|
| Target properties and transitive requirements | [cmake-buildsystem](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html) |
| PUBLIC/PRIVATE/INTERFACE link semantics | [target_link_libraries](https://cmake.org/cmake/help/latest/command/target_link_libraries.html) |
| Header-only and imported targets | [add_library](https://cmake.org/cmake/help/latest/command/add_library.html) |
| Scope when splitting directories | [add_subdirectory](https://cmake.org/cmake/help/latest/command/add_subdirectory.html) |
| C++ standard feature requirements | [target_compile_features](https://cmake.org/cmake/help/latest/command/target_compile_features.html) |
| Minimum versions and policies | [cmake_minimum_required](https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html) |
| Configuration/platform expressions | [generator expressions](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html) |
| Package discovery vs source dependencies | [using dependencies](https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html) |
| Exported consumer interfaces | [importing and exporting](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html) |
| Installation rules | [install](https://cmake.org/cmake/help/latest/command/install.html) |
| Generated build graph | [add_custom_command](https://cmake.org/cmake/help/latest/command/add_custom_command.html) |
| Why source globbing is discouraged | [file GLOB](https://cmake.org/cmake/help/latest/command/file.html#filesystem) |
| Tests, discovery, scheduling | [CTest manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html) |
| Cross-compilation and host/target distinction | [toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html) |
| Reproducible shared configuration | [presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) |

## LLVM: optional components and test inclusion

- [Root CMakeLists.txt](https://github.com/llvm/llvm-project/blob/main/llvm/CMakeLists.txt): inspect LLVM_INCLUDE_TESTS versus LLVM_BUILD_TESTS, plus tools/examples inclusion.
- [CMake build documentation](https://llvm.org/docs/CMake.html): inspect LLVM_ENABLE_PROJECTS, LLVM_ENABLE_RUNTIMES, dependencies, compiler and configuration options.
- [Runtime orchestration](https://github.com/llvm/llvm-project/blob/main/llvm/runtimes/CMakeLists.txt): inspect separate runtime builds and test target aggregation only when a superbuild/toolchain workflow is needed.

Transfer the distinction between target availability and default-build participation when relevant. Do not assume LLVM's specialized check targets are ordinary CTest tests or copy its toolchain-scale complexity into a simple library.

## VTK: explicit module contracts

- [Root CMakeLists.txt](https://github.com/Kitware/VTK/blob/master/CMakeLists.txt): inspect vtk_module_scan, module enablement, test dependencies, and platform helper includes.
- [Module framework](https://github.com/Kitware/VTK/blob/master/CMake/vtkModule.cmake): inspect DEPENDS, PRIVATE_DEPENDS, OPTIONAL_DEPENDS, TEST_DEPENDS, and TEST_LABELS.
- [Common/Core module](https://github.com/Kitware/VTK/blob/master/Common/Core/CMakeLists.txt): inspect scoped linking to Threads::Threads, PUBLIC cxx_std_17, and platform-specific dependencies.
- [Module tests](https://github.com/Kitware/VTK/blob/master/Common/Core/Testing/Cxx/CMakeLists.txt): inspect generated test cases and module-owned test registration.

Transfer explicit dependency categories and local ownership. Do not adopt VTK's custom module parser or helper commands without its framework; these are not built-in CMake commands.

## curl: dependency and platform handling

- [Root CMakeLists.txt](https://github.com/curl/curl/blob/master/CMakeLists.txt): inspect OpenSSL imported targets, feature checks, WIN32/APPLE distinctions, lib/src/tests directories, and optional functionality.
- [Tests CMakeLists.txt](https://github.com/curl/curl/blob/master/tests/CMakeLists.txt): inspect helper server dependencies, unit/libtest groups, custom test runners, and multi-config handling.

Transfer imported dependencies and explicit capability/platform decisions. curl supports a broad historical platform range and custom runners; its directory-wide flags and compatibility workarounds are contextual examples, not blanket best practices.

## How to use the evidence

Use official versioned command documentation to settle semantics. Use upstream projects to understand architectural tradeoffs. Keep a narrow local fix when it solves the requested problem; do not add presets, package exports, test frameworks, or a module framework solely because a reference project has them.
