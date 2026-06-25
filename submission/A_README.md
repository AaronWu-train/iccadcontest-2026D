# ICCAD Contest 2026 Problem D Final Submission

This archive contains the final submission materials for ICCAD Contest 2026
Problem D, "Timing Fixing by Useful Skew". The project's original repository is available at [GitHub](https://github.com/AaronWu-train/iccadcontest-2026D)

## Directory Map

```text
A_README.md
B_PRESENTATION_SLIDES.pdf
C_PROJECT_REPORT.pdf
D_Source_Code_and_Testcases/
E_Supplemental_Materials/
```

* `A_README.md`: this file. It describes the submission contents and gives
  instructions for building, testing, and running the solver.
* `B_PRESENTATION_SLIDES.pdf`: final presentation slides.
* `C_PROJECT_REPORT.pdf`: final project report.
* `D_Source_Code_and_Testcases/`: source code, build files, scripts, tests, and
  the expected location for testcase inputs.
* `E_Supplemental_Materials/`: supplemental material description and links to
  raw experimental results.

## Requirements

The solver is implemented in C++17 and is built with CMake.

Required:

* CMake 3.20 or newer
* C++17-compatible compiler
* Make
* Git

Optional:

* Ninja, for faster local builds
* Docker, for building the Rocky Linux 8 release binary
* Slurm, for running the full optimizer matrix on a cluster

During the first CMake configuration, CMake uses `FetchContent` to download the
following dependencies:

* Catch2 v3.8.1
* CLI11 v2.4.2

Therefore, internet access is required during the first CMake configuration
unless these dependencies are already cached by CMake.

## Build and Test

All commands in this section should be run from the source-code directory:

```sh
cd D_Source_Code_and_Testcases
```

Build the debug binary:

```sh
make build
```

Run the unit tests:

```sh
make test
```

Build the optimized release binary:

```sh
make release
```

The debug binary is generated at:

```text
build/cadd0040
```

The release binary is generated at:

```text
build-release/cadd0040
```

To build a Rocky Linux 8 release binary with Docker, run:

```sh
make rocky8
```

The Rocky Linux 8 binary is generated at:

```text
dist/rocky8/cadd0040
```

## Download Testcases

The official contest testcases are used in this work. However, they are too
large to be included in this zip file. Please download the test data from the
following Drive link:

[Drive Link](https://drive.google.com/drive/folders/1Jj3eE2K4qAnVpTw3MQPYatJaYjE2bakj?usp=sharing)

After downloading the test data, place the testcase directories under:

```text
D_Source_Code_and_Testcases/testcases/
```

The expected directory structure is:

```text
D_Source_Code_and_Testcases/
└── testcases/
    ├── testcase0/
    │   ├── buf.lib
    │   ├── clk_tree.structure
    │   ├── FF_delay.rpt
    │   └── SS_delay.rpt
    ├── testcase1/
    │   ├── buf.lib
    │   ├── clk_tree.structure
    │   ├── FF_delay.rpt
    │   └── SS_delay.rpt
    ├── testcase2/
    │   ├── buf.lib
    │   ├── clk_tree.structure
    │   ├── FF_delay.rpt
    │   └── SS_delay.rpt
    ├── testcase3/
    │   ├── buf.lib
    │   ├── clk_tree.structure
    │   ├── FF_delay.rpt
    │   └── SS_delay.rpt
    └── testcase4/
        ├── buf.lib
        ├── clk_tree.structure
        ├── FF_delay.rpt
        └── SS_delay.rpt
```

## Run the Solver

The solver executable is named `cadd0040`. It takes two positional arguments:

```text
cadd0040 <testcase_dir> <output_file>
```

The testcase directory must contain:

```text
clk_tree.structure
buf.lib
SS_delay.rpt
FF_delay.rpt
```

The solver writes the modified clock tree to the given output path.

### Run One Testcase

After building the debug binary with `make build`, run:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure
```

After building the release binary with `make release`, run:

```sh
./build-release/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure
```

The default optimizer is:

```text
tabu-random
```

### Run One Testcase with a Specific Optimizer

Example:

```sh
./build-release/cadd0040 \
  --optimizer tabu-random \
  --seconds 570 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure
```

Common optimizer names include:

```text
greedy-random
greedy-violation-path
greedy-upstream-window
greedy-critical-endpoint
greedy-union-pool
two-step-union-pool
sa-sampled-union-pool
isa-sampled-union-pool
tabu-union-pool
two-step-random
sa-random
isa-random
tabu-random
```

### Run All Downloaded Testcases

The batch script runs every `testcase*` directory under `testcases/`.

Using the default debug build directory:

```sh
./scripts/run_all_testcases.sh
```

Using the release build directory:

```sh
./scripts/run_all_testcases.sh --build-dir build-release
```

Using a specific optimizer:

```sh
./scripts/run_all_testcases.sh \
  --build-dir build-release \
  --optimizer tabu-random \
  --seconds 570
```

The script writes each output file to the corresponding testcase directory as:

```text
modified_clk_tree.structure
```

## Reproduce the Optimizer Matrix

The full local optimizer matrix can be run with:

```sh
./scripts/slurm_run_all_optimizers.sh --local
```

By default, this script evaluates 13 optimizer variants on all available
`testcase*` directories under `testcases/`.

To use the release build directory:

```sh
BUILD_DIR=build-release ./scripts/slurm_run_all_optimizers.sh --local
```

To run the optimizer matrix on a Slurm cluster, run:

```sh
./scripts/slurm_run_all_optimizers.sh
```

The script creates a timestamped directory under:

```text
D_Source_Code_and_Testcases/slurm_runs/
```

The generated result files include:

```text
results.tsv
by_optimizer.tsv
best_by_testcase.tsv
summary.txt
logs/
outputs/
progress/
```

## Supplemental Experimental Data

The detailed raw experimental results are too large to be included directly in
this zip file. Please see:

```text
E_Supplemental_Materials/README_FOR_SUPPLEMENT.md
```

The raw experimental results can be downloaded from the following Drive link:

[Drive Link](https://drive.google.com/file/d/1aEEa5809AMByTVic1d0iDEWjBdPaHaad/view?usp=sharing)

After downloading the archive, extract it before reading the detailed
experimental data. The supplemental data includes raw logs, per-run outputs,
numeric progress traces, and summary TSV files for the experiments reported in
the project report.

## Notes

* The submitted zip file does not include the official testcase data because the
  data exceeds the submission size limit.
* The submitted zip file does not include the full raw experimental results
  because they exceed the submission size limit.
* The source code is under `D_Source_Code_and_Testcases/`.
* The final project report is `C_PROJECT_REPORT.pdf`.
* The final presentation slides are `B_PRESENTATION_SLIDES.pdf`.
