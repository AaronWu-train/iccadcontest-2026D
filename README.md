# ICCAD Contest 2026 Problem D

C++17 solver for ICCAD Contest 2026 Problem D clock-tree optimization.

`cadd0040` reads one testcase directory, runs an optimizer, and writes a
`modified_clk_tree.structure` output file.

## Requirements

- CMake 3.20 or newer
- C++17 compiler
- Make
- Git, for CMake FetchContent dependencies
- Optional: Ninja for faster builds
- Optional: clang-format and pre-commit for formatting hooks
- Optional: Docker for Rocky Linux 8 release builds

Catch2 v3 and CLI11 are fetched by CMake.

## Quick Start

For a fresh checkout, install the development hooks, build the debug binary, and
run the test suite:

```sh
pre-commit install
make build
make test
```

Run one testcase with the default optimizer:

```sh
./build/cadd0040 testcases/testcase0 testcases/testcase0/modified_clk_tree.structure
```

Run all testcase directories under `testcases/`:

```sh
make run
```

## Build Commands

Builds the debug executable in `build/`: 
```sh
make build
```

Builds the optimized executable in `build-release/`:
```sh
make release
```

Builds debug and runs Catch2 through CTest:
```sh
make test
```

Runs `scripts/run_all_testcases.sh` with the debug build:
```sh
make run
```

## Rocky Linux 8 Docker Build

Build the Rocky Linux 8 release binary:

```sh
make rocky8
```

The binary is written to `dist/rocky8/cadd0040`.

## Solver CLI

```sh
./build/cadd0040 <testcase_dir> <output_file> [options]
```

Common options:

```sh
--optimizer <name>
--seed <n>
--seconds <n>
--config <file>
--debug
--progress-dir <dir>
--progress-steps <n>
```

Example:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure \
  --optimizer tabu-random \
  --seed 1234 \
  --seconds 60
```

Each testcase directory must contain `clk_tree.structure`, `buf.lib`,
`SS_delay.rpt`, and `FF_delay.rpt`.

The default optimizer is `tabu-random`.

## Batch Runs

Run one optimizer across all `testcases/testcase*/` directories:

```sh
./scripts/run_all_testcases.sh
```

Useful variants:

```sh
./scripts/run_all_testcases.sh --seconds 60
./scripts/run_all_testcases.sh --optimizer tabu-random
./scripts/run_all_testcases.sh --debug
./scripts/run_all_testcases.sh --build-dir build-release
```

## Slurm Optimizer Matrix

Run the default A1-A13 optimizer matrix on Slurm:

```sh
./scripts/slurm_run_all_optimizers.sh
```

On a machine without Slurm, run the same matrix sequentially:

```sh
./scripts/slurm_run_all_optimizers.sh --local
```

Useful variants:

```sh
./scripts/slurm_run_all_optimizers.sh --wait
./scripts/slurm_run_all_optimizers.sh --seed 5000 --seed-runs 10
OPTIMIZERS="A1 A6 A10 A13" ./scripts/slurm_run_all_optimizers.sh --local
SLURM_TIME=00:11:00 ./scripts/slurm_run_all_optimizers.sh
```

Aggregate an existing Slurm run:

```sh
OUTPUT_DIR=slurm_runs/<run-id> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```

Each Slurm optimizer run writes logs, output clock trees, summary TSVs, and
`progress.tsv` files under `slurm_runs/<run-id>/`.

## Reports

Generate report figures, derived TSVs, and audit metadata from a run directory:

```sh
python3 scripts/plot_optimizer_report.py \
  --run-dir slurm_runs/<run-id> \
  --out-dir slurm_runs/<run-id>/report_plots
```

Important outputs:

```text
report_plots/figures/
report_plots/tables/
report_plots/REPORT_SUMMARY.md
report_plots/RUN_AUDIT.json
```

## Documentation

- [`docs/architecture.md`](docs/architecture.md): solver data flow, core types,
  telemetry, and output behavior.
- [`docs/optimizers.md`](docs/optimizers.md): A1-A13 aliases, policies,
  algorithm flows, defaults, and complexity notes.
- [`docs/experiments.md`](docs/experiments.md): batch scripts, Slurm layout,
  TSV meanings, report generation, and config files.


## Development Notes

Install the formatting hook once per checkout:

```sh
pre-commit install
```

- Use git flow for branches.
- Rebase only your own feature branches; do not rebase shared branches.
- Run `make test` before opening a pull request.
- Use short imperative commit messages, and ensure commit messages are precise and includes the reason for the commit.
