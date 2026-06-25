# ICCAD Contest 2026 Problem D

[![Contest](https://img.shields.io/badge/ICCAD%202026-Problem%20D-orange.svg)](problem-statement.pdf)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064f8c.svg)](https://cmake.org/)
[![Catch2](https://img.shields.io/badge/tests-Catch2%20v3-2ea44f.svg)](https://github.com/catchorg/Catch2)
[![License](https://img.shields.io/badge/license-Apache%20License%202.0-red)](LICENSE)

C++17 solver for ICCAD Contest 2026 Problem D clock-tree useful-skew
optimization.

`cadd0040` reads one testcase directory, runs an optimizer, and writes a
`modified_clk_tree.structure` output file. The default optimizer is
`tabu-random`.

## Requirements

- CMake 3.20 or newer
- C++17 compiler
- Make
- Git, for CMake FetchContent dependencies
- Optional: Ninja for faster builds
- Optional: Docker for Rocky Linux 8 release builds

Catch2 v3 and CLI11 are fetched by CMake.

## Quick Start

```sh
make build
make test
```

Run one testcase:

```sh
./build/cadd0040 testcases/testcase0 testcases/testcase0/modified_clk_tree.structure
```

Run all bundled testcase directories with the default optimizer:

```sh
make run
```

## Usage

```sh
./build/cadd0040 <testcase_dir> <output_file> [options]
```

Common options:

```text
--optimizer <name>
--seed <n>
--seconds <n>
--config <file>
--debug
--progress-dir <dir>
--progress-steps <n>
```

Each testcase directory must contain:

```text
clk_tree.structure
buf.lib
SS_delay.rpt
FF_delay.rpt
```

## Experiments

Run one optimizer across all `testcases/testcase*/` directories:

```sh
./scripts/run_all_testcases.sh --optimizer tabu-random
```

Run the A1-A13 optimizer matrix on Slurm:

```sh
./scripts/slurm_run_all_optimizers.sh --seed-runs 10
```

Run the same matrix locally:

```sh
./scripts/slurm_run_all_optimizers.sh --local
```

Aggregate an existing Slurm run:

```sh
OUTPUT_DIR=slurm_runs/<run-id> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```

Generate report figures and derived tables from a run directory:

```sh
python3 scripts/plot_optimizer_report.py \
  --run-dir slurm_runs/<run-id> \
  --out-dir slurm_runs/<run-id>/report_plots
```

## Final Submission Package

Edit the manual submission inputs:

```text
submission/A_README.md
submission/E_Supplemental_Materials/
```

Build the final course submission ZIP:

```sh
make submission
```

The archive is written to:

```text
dist/submission/B13901011_B13901078_B13901088_B13901104.zip
```

## Documentation

- [`CONTRIBUTING.md`](CONTRIBUTING.md): local development, formatting, adding
  code, and submission packaging workflow.
- [`docs/architecture.md`](docs/architecture.md): solver flow, data structures,
  telemetry, and output behavior.
- [`docs/optimizers.md`](docs/optimizers.md): optimizer aliases, policies,
  defaults, complexity, and extension rules.
- [`docs/experiments.md`](docs/experiments.md): scripts, run directories, TSVs,
  reports, and config files.
- [`docs/report-plots.md`](docs/report-plots.md): report figure/table generator
  details.
- [`docs/analyze.md`](docs/analyze.md): analysis notes kept as-is.
