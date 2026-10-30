# Platforms and presets

## Keep distinctions explicit

- WIN32, APPLE, UNIX, and CMAKE_SYSTEM_NAME describe the target platform. UNIX also matches macOS; use precise conditions and order APPLE before a general UNIX fallback where behavior differs.
- MSVC, compiler ID, and compiler frontend variant describe compiler behavior. Windows can use MSVC, clang-cl, Clang, or MinGW. Do not put all Windows compilers behind MSVC flags or assume all non-MSVC compilers accept GCC flags.
- CMAKE_HOST_SYSTEM_NAME and CMAKE_HOST_WIN32 describe the host. Use these for host tools, not target libraries.
- Check features when capability is the question. Use OS conditions when selecting an actual OS API/source/library. Compile/link probes are generally more suitable than execution probes during cross-compilation.
- Put compiler, sysroot, target system, and architecture selection in a toolchain file supplied before project() initializes compilers. Do not switch compilers inside an already configured build tree.
- Quote filesystem paths and use CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR for directory-owned files. In reusable includes use CMAKE_CURRENT_LIST_DIR appropriately; distinguish call-site from definition-site semantics inside functions.

## Configuration handling

CMAKE_BUILD_TYPE selects a configuration for single-config generators such as ordinary Ninja. Multi-config generators select at build time with --config and CTest -C. Do not force CMAKE_BUILD_TYPE to Release to fix Visual Studio. Use $<CONFIG:Debug> and other appropriate generator expressions for configuration-specific target settings.

Illustrative commands only; execute only when authorized:

```text
cmake -S . -B build/ninja-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ninja-debug --target texas
ctest --test-dir build/ninja-debug --output-on-failure

cmake -S . -B build/multi -G "Ninja Multi-Config"
cmake --build build/multi --config Release --target texas
ctest --test-dir build/multi -C Release --output-on-failure
```

These configure examples leave tests at project defaults. Pass both -DTEXASSOLVER_BUILD_TESTS=ON and -DBUILD_TESTING=ON when intending to register ordinary TexasSolver tests, and build the needed test targets before CTest. CTest does not generally compile missing executables.

## Presets compatible with the current 3.20 baseline

Preset schema version 2 supports configure/build/test presets with CMake 3.20. Conditions and toolchainFile fields require schema 3/CMake 3.21; includes require schema 4/CMake 3.23; workflow presets require schema 6/CMake 3.25. Verify newer features against versioned documentation before use.

Commit CMakePresets.json for shared settings. Keep CMakeUserPresets.json for local paths and out of version control. Use separate binary directories for incompatible toolchains and configurations. Avoid hardcoded user paths, credentials, and undocumented environment assumptions.

```json
{
  "version": 2,
  "cmakeMinimumRequired": { "major": 3, "minor": 20, "patch": 0 },
  "configurePresets": [{
    "name": "dev-debug",
    "generator": "Ninja",
    "binaryDir": "${sourceDir}/build/dev-debug",
    "cacheVariables": {
      "CMAKE_BUILD_TYPE": "Debug",
      "BUILD_TESTING": "ON",
      "TEXASSOLVER_BUILD_TESTS": "ON"
    }
  }],
  "buildPresets": [{ "name": "dev-debug", "configurePreset": "dev-debug" }],
  "testPresets": [{
    "name": "dev-debug",
    "configurePreset": "dev-debug",
    "output": { "outputOnFailure": true }
  }]
}
```

Ninja and an initialized compiler environment are prerequisites. This example does not install tools or choose a compiler. Keep existing presets and CI workflows unless the task asks to change them. Recommend relevant Windows/MSVC, Linux/GCC or Clang, and macOS/AppleClang checks; do not claim all platforms were verified from one host.

References: [toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html), [generator expressions](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html), [presets and schema versions](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html), [CMake 3.20 presets](https://cmake.org/cmake/help/v3.20/manual/cmake-presets.7.html), [CheckCXXSourceCompiles](https://cmake.org/cmake/help/latest/module/CheckCXXSourceCompiles.html).
