# TexasSolver

TexasSolver is a free open-source C++17 library for solving poker subgames and computing exploitability/value metrics with DCFR-style algorithms.<br>
This repository is a C++ port of the Rust implementation from **[amaster97/poker_solver](https://github.com/amaster97/poker_solver)** <br>
The code and layout here are organized so the project can be consumed as a normal CMake dependency from another C++ project.

## What is included

- Kuhn poker solver
- Leduc poker solver
- Hold'em game-state logic
- HUNL postflop and preflop solver code
- Exploitability and game-value evaluation
- Preflop equity helpers
- Vendored `pokerHandEvaluator` submodule for fast 5-card, 6-card, and 7-card hand ranking
- Abstraction / layout / SIMD / suit-isomorphism utilities


## CMake target

The main library target is:

- `TexasSolver::texas_core`

When installed, the package exports a CMake config file so another project can use `find_package(TexasSolver CONFIG REQUIRED)`.

## Build requirements

- CMake 3.20 or newer
- A C++17 compiler
- A working build toolchain for your platform

On Windows the project builds with Visual Studio/MSBuild. On other platforms a normal CMake generator is fine.

The repository includes `external/pokerHandEvaluator` as a Git submodule. The top-level CMake build wires that vendored library into `texas_core` so fixed-size hand evaluation can use the faster `evaluate_5cards`, `evaluate_6cards`, and `evaluate_7cards` paths.

## Build the project

### Configure

```bash
cmake -S . -B build
```

### Build

```bash
cmake --build build --config Release
```

### Run tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

## Install the library

You can install the library and export its CMake package files:

```bash
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix <install-prefix>
```

This installs:

- headers under `<prefix>/include`
- the library archive / import library under `<prefix>/lib` or the platform equivalent
- CMake package files under `<prefix>/lib/cmake/TexasSolver`

When building from the repository, the vendored hand evaluator is also configured through CMake. If you need to turn it off for debugging or portability work, set `TEXASSOLVER_USE_POKER_HAND_EVALUATOR=OFF` during configuration.

## Use as a submodule

Add this repository as a Git submodule inside your own project:

```bash
git submodule add https://github.com/ArtemIyX/TexasSolverLib
git submodule update --init --recursive
```

Then in your top-level `CMakeLists.txt`:

```cmake
add_subdirectory(external/TexasSolver)
target_link_libraries(your_target PRIVATE TexasSolver::texas_core)
```

## Use as an installed package

If the library has been installed to a prefix, consume it from another project like this:

```cmake
find_package(TexasSolver CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE TexasSolver::texas_core)
```

If CMake cannot find the package automatically, set one of:

```bash
TexasSolver_DIR=<prefix>/lib/cmake/TexasSolver
```

or:

```bash
CMAKE_PREFIX_PATH=<prefix>
```

## C++ examples

### Solve Kuhn poker

```cpp
#include "solver/solver.hpp"
#include <iostream>

int main() {
    const auto out = core::solve_kuhn(200, 1.5, 0.0, 2.0);
    std::cout << "iterations: " << out.iterations << '\n';
    std::cout << "game value: " << out.game_value << '\n';
    std::cout << "exploitability: " << out.exploitability << '\n';
}
```

### Solve Leduc poker

```cpp
#include "solver/solver.hpp"

int main() {
    const auto out = core::solve_leduc(50, 1.5, 0.0, 2.0);
    return out.average_strategy.empty() ? 1 : 0;
}
```

### Use the library facade

```cpp
#include "core/lib.hpp"

int main() {
    const auto out = core::lib::solve_kuhn(200, 1.5, 0.0, 2.0);
    return out.iterations == 200 ? 0 : 1;
}
```

## Notes

- The namespace used by the C++ code is still `core::`.
- The repository structure has been grouped by module so the include paths stay readable.

## License

This project is distributed under the MIT License. See [`LICENSE`](LICENSE).
