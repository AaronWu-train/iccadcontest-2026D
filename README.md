# iccadcontest-2026D

C++ scaffold for ICCAD Contest 2026 Problem D. The executable reads a testcase
directory, prepares the required input paths, and writes the modified clock tree
to the requested output path.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler
- Make
- Git, for CMake FetchContent dependencies
- Ninja is recommended for faster builds, but not required

Catch2 v3 and CLI11 are fetched automatically by CMake.

The Makefile automatically uses Ninja when `ninja` is available. If Ninja is
not installed, it falls back to CMake's default generator.

To install Ninja:

```sh
# macOS
brew install ninja

# Ubuntu/Debian
sudo apt install ninja-build
```

## Build

```sh
make build
```

For an optimized build:

```sh
make release
```

## Run

```sh
./build/cadd0040 <testcase_dir> <output_file>
```

Example:

```sh
./build/cadd0040 ./testcases/testcase0 ./testcases/testcase0/modified_clk_tree.structure
```

Each testcase directory should contain:

- `clk_tree.structure`
- `buf.lib`
- `SS_delay.rpt`
- `FF_delay.rpt`

## Unit Test

```sh
make test
```

Tests are written with Catch2 v3 and registered with CTest.

## Development

The project keeps headers next to their `.cpp` files:

- `src/main.cpp`: command-line parsing and program startup only.
- `src/app.hpp`, `src/app.cpp`: top-level solver flow.
- `src/**`: future parser, model, and algorithm code.
- `tests/`: Catch2 unit tests.

Keep reusable logic out of `main.cpp` so it can be tested through the core
library. When adding new source files, put the `.hpp` and `.cpp` together under
`src/`, then add the `.cpp` to `cadd0040_core` in `CMakeLists.txt`:

```cmake
add_library(cadd0040_core
    src/app.cpp
    src/parser/clk_tree_parser.cpp
)
```

Headers usually do not need to be listed in CMake. For new tests, add the test
source to `cadd0040_tests` in `tests/CMakeLists.txt`:

```cmake
add_executable(cadd0040_tests
    test_app.cpp
    test_clk_tree_parser.cpp
)
```

Typical development loop:

```sh
make
make test
```

## Contribution Notes

- Follow Git Flow: develop features on `feature/*`, fixes on `fix/*`, and merge
  back through the normal review flow.
- Use clear commit messages. Prefer short imperative summaries such as
  `Add clock tree parser` or `Fix delay report parsing`.
