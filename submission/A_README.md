# ICCAD Contest 2026 Problem D Final Submission

This archive contains the final submission materials for ICCAD Contest 2026
Problem D, "Timing Fixing by Useful Skew".

## Directory Map

```text
A_README.md
B_PRESENTATION_SLIDES.pdf
C_PROJECT_REPORT.pdf
D_Source_Code_and_Testcases/
E_Supplemental_Materials/
```

- `A_README.md`: this file.
- `B_PRESENTATION_SLIDES.pdf`: final presentation slides.
- `C_PROJECT_REPORT.pdf`: final project report.
- `D_Source_Code_and_Testcases/`: source code, scripts, tests, testcase inputs,
  and build files.
- `E_Supplemental_Materials/`: optional supplemental materials maintained by
  the team.

## Requirements

- CMake 3.20 or newer
- C++17 compiler
- Make
- Git, for CMake FetchContent dependencies
- Optional: Ninja for faster builds
- Optional: Docker for Rocky Linux 8 release builds

Catch2 v3 and CLI11 are fetched by CMake during configuration.

## Build And Test

From `D_Source_Code_and_Testcases/`:

```sh
make build
make test
```

Build the optimized binary:

```sh
make release
```

Build a Rocky Linux 8 release binary:

```sh
make rocky8
```

## Run Examples

Run one testcase with the default optimizer:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure
```

Run all bundled testcase directories with one optimizer:

```sh
./scripts/run_all_testcases.sh
```

Run the optimizer matrix locally:

```sh
./scripts/slurm_run_all_optimizers.sh --local
```

Run the optimizer matrix on Slurm:

```sh
./scripts/slurm_run_all_optimizers.sh
```

## Notes

The solver executable is `cadd0040`. It reads a testcase directory containing
`clk_tree.structure`, `buf.lib`, `SS_delay.rpt`, and `FF_delay.rpt`, then writes
`modified_clk_tree.structure`.

The default optimizer is `tabu-random`.

## Detailed Experimental data

See `E_Supplemental_Material/`. There is a file `README_FOR_SUPPLEMENT.md` introduce the structure of our test result and guidance for detailed read.  
