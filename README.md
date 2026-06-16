# ICCAD Contest 2026 Problem D

C++17 solver for ICCAD Contest 2026 Problem D: clock tree optimization. The
`cadd0040` executable reads one testcase directory, runs a selected optimizer,
and writes a modified clock tree structure file.

## Quick Start

```sh
make build
make test

./build/cadd0040 testcases/testcase0 testcases/testcase0/modified_clk_tree.structure
```

For experiment runs, use the release build:

```sh
make release
./scripts/run_all_testcases.sh
```

## Requirements

- CMake 3.20 or newer
- C++17 compiler
- Make
- Git, for CMake FetchContent dependencies
- Optional: Ninja, for faster builds
- Optional: clang-format and pre-commit, for formatting hooks

Catch2 v3 and CLI11 are fetched automatically by CMake. The Makefile uses Ninja
when `ninja` is available, otherwise it falls back to CMake's default generator.

```sh
# macOS
brew install ninja

# Ubuntu/Debian
sudo apt install ninja-build
```

## Build And Test

```sh
make build      # debug build in build/
make release    # optimized build in build-release/
make test       # Catch2 through CTest
make run        # debug build, all testcases, stderr status enabled
```

Manual CMake builds are also supported, but the Makefile is the normal entry
point for this repo.

## CLI

```sh
./build/cadd0040 <testcase_dir> <output_file> [--optimizer <name>] [--config <file>]
```

Example:

```sh
./build/cadd0040 \
  testcases/testcase0 \
  testcases/testcase0/modified_clk_tree.structure \
  --optimizer isa
```

Each testcase directory must contain:

- `clk_tree.structure`
- `buf.lib`
- `SS_delay.rpt`
- `FF_delay.rpt`

The default optimizer is `isa`.

## Optimizers

Main A1-A8 experiment aliases:

| ID | Alias | Role |
|----|-------|------|
| A1 | `greedy-violation-path` | Best-improvement greedy from violated path endpoints |
| A2 | `sa` | Single simulated annealing flow |
| A3 | `isa` | Iterated simulated annealing; default |
| A4 | `greedy-critical-endpoint` | Greedy candidates from top critical endpoints |
| A5 | `greedy-upstream-window` | Greedy candidates from upstream endpoint windows |
| A6 | `greedy-repair-recover` | Timing repair followed by area recovery |
| A7 | `greedy-randomized-rcl` | Randomized greedy top-k move selection with restarts |
| A8 | `tabu` | Tabu search with aspiration |

Additional registered aliases:

- `milp`: legacy MILP-inspired heuristic, not a true MILP solver.
- `visual`: clock-tree trace/visualization tool.
- `dummy`: no-op optimizer used by tests.

Detailed algorithm notes live in
[`docs/optimization-algorithms.md`](docs/optimization-algorithms.md).

## Run All Testcases

`scripts/run_all_testcases.sh` runs one optimizer against every
`testcases/testcase*/` directory and prints a score table.

```sh
make release
./scripts/run_all_testcases.sh
```

Common overrides:

```sh
# Short smoke test
CADD0040_SA_SECONDS=60 ./scripts/run_all_testcases.sh

# Use the debug build
BUILD_DIR=build ./scripts/run_all_testcases.sh

# Select another optimizer
OPTIMIZER=tabu ./scripts/run_all_testcases.sh

# Disable local stderr status output
CADD0040_DEBUG_PROGRESS=0 ./scripts/run_all_testcases.sh
```

## Slurm Experiment Runs

Run the canonical A1-A8 optimizer x testcase matrix:

```sh
make release
./scripts/slurm_run_all_optimizers.sh
```

Typical workflow:

```sh
./scripts/slurm_run_all_optimizers.sh
squeue -j <job_id>
OUTPUT_DIR=<printed-output-dir> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```

Useful modes:

```sh
./scripts/slurm_run_all_optimizers.sh --wait      # wait, then aggregate
./scripts/slurm_run_all_optimizers.sh --local     # run sequentially without Slurm
OPTIMIZERS="isa tabu" ./scripts/slurm_run_all_optimizers.sh
SLURM_PARTITION=short SLURM_TIME=00:11:00 ./scripts/slurm_run_all_optimizers.sh
```

The aggregated run directory contains:

- `logs/<optimizer>/<testcase>.log`
- `outputs/<optimizer>/<testcase>/modified_clk_tree.structure`
- `results.tsv`
- `by_optimizer.tsv`
- `best_by_testcase.tsv`
- `summary.txt`

## Config Sweeps

Use `--config <file>` for reproducible parameter sweeps. Config files use INI
`key = value` syntax with optional optimizer sections:

```ini
optimizer = isa
seed = 1234
time_budget_seconds = 60

[isa]
rounds = 8
greedy_round_iterations = 32
initial_temperature = 0.08
```

Config precedence:

1. Struct defaults from `src/optimization/optimizer_config.hpp`
2. `CADD0040_SA_SECONDS`, when no config value overrides it
3. Config global keys: `optimizer`, `seed`, `time_budget_seconds`
4. Config optimizer-section keys

Run every top-level config file under `config/` against every testcase:

```sh
./scripts/slurm_run_all_configs.sh
./scripts/slurm_run_all_configs.sh --local
OUTPUT_DIR=<printed-output-dir> ./scripts/slurm_run_all_configs.sh --aggregate-only
```

Notes:

- `scripts/slurm_run_all_configs.sh` scans only top-level `config/*.conf`,
  `config/*.ini`, and `config/*.cfg` files.
- `CONFIGS="name other.ini"` restricts the run to selected config basenames or
  filenames.
- Config files can override the optimizer, so this runner does not pass
  `--optimizer`.

## Telemetry Outputs

There are three different telemetry paths. Their names are historical, so keep
the mental model below:

| Purpose | Switch | Output | Best For |
|---------|--------|--------|----------|
| Human debug status | `CADD0040_DEBUG_PROGRESS=1` | stderr lines from `DebugProgress` | Watching a local debug run |
| Numeric event trace | `CADD0040_PROGRESS_TRACE=1` | `progress.tsv` | Plotting score/time curves |
| Visual frame trace | `CADD0040_VISUAL_TRACE=1` | `frames.json` | Clock-tree animation and inspection |

`DebugProgress` does not write trace files. It is silent in release builds, and
in debug builds it prints periodic status lines only when
`CADD0040_DEBUG_PROGRESS=1`.

`CADD0040_PROGRESS_TRACE` writes lightweight optimizer events and metrics to a
TSV file. This is the input for `scripts/plot_optimizer_progress.py`.

`CADD0040_VISUAL_TRACE` samples clock-tree snapshots into JSON. This is heavier
than the TSV trace and should be enabled only for selected runs.

The `visual` optimizer alias is separate from `CADD0040_VISUAL_TRACE`: it is a
special visualization optimizer, while the env var can record frames for normal
optimizers.

Most full runs should keep both trace files off. Enable them only for selected
local or small Slurm runs.

```sh
# Numeric event trace for plots
CADD0040_PROGRESS_TRACE=1 CADD0040_PROGRESS_STEPS=256 \
  ./scripts/slurm_run_all_optimizers.sh --local

# Visual frame trace for animation
CADD0040_VISUAL_TRACE=1 CADD0040_VISUAL_TRACE_STEPS=256 \
  OPTIMIZERS="greedy-violation-path" \
  ./scripts/slurm_run_all_optimizers.sh --local
```

Trace file outputs:

- `progress/<optimizer>/<testcase>/progress.tsv`
- `traces/<optimizer>/<testcase>/frames.json`

Plot numeric event traces:

```sh
python3 scripts/plot_optimizer_progress.py \
  --run-dir slurm_runs/20260616_120000 \
  --y best_score \
  --out-dir slurm_runs/20260616_120000/plots
```

Generate an HTML visualization from visual frames:

```sh
python3 scripts/visualize_clock_tree_trace.py <trace-dir-containing-frames-json>
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BUILD_DIR` | `build-release` | CMake build directory containing `cadd0040` |
| `OPTIMIZER` | `isa` | Optimizer used by `run_all_testcases.sh` |
| `OPTIMIZERS` | A1-A8 list | Optimizers used by `slurm_run_all_optimizers.sh` |
| `CONFIG_DIR` | `config/` | Config root for `slurm_run_all_configs.sh` |
| `CONFIGS` | all | Space-separated config basenames or filenames |
| `TESTCASES_DIR` | `testcases/` | Testcase root for Slurm scripts |
| `OUTPUT_DIR` | timestamped | Run output directory |
| `CADD0040_SA_SECONDS` | `570` | Legacy wall-clock budget override |
| `CADD0040_CHECKPOINT_STEPS` | `4096` | Best-so-far output checkpoint interval; `0` disables |
| `CADD0040_REPORT_METRICS` | script-specific | Print initial/final score lines |
| `CADD0040_DEBUG_PROGRESS` | script-specific | Enable human-readable stderr status in debug builds |
| `CADD0040_DEBUG_PROGRESS_INTERVAL` | script-specific | Seconds between stderr status reports |
| `CADD0040_PROGRESS_TRACE` | `0` | Write numeric event trace rows to `progress.tsv` |
| `CADD0040_PROGRESS_STEPS` | `256` | Logical step interval for numeric event trace rows |
| `CADD0040_VISUAL_TRACE` | `0` | Write sampled clock-tree snapshots to `frames.json` |
| `CADD0040_VISUAL_TRACE_STEPS` | `256` | Logical step interval for visual frame snapshots |
| `SLURM_PARTITION` | unset | Optional Slurm partition |
| `SLURM_ACCOUNT` | unset | Optional Slurm account |
| `SLURM_TIME` | `00:11:00` | Slurm task time limit |
| `SLURM_MEM` | `4G` | Slurm memory per task |
| `SLURM_CPUS` | `1` | Slurm CPUs per task |

## Project Layout

```text
src/
  main.cpp                  CLI entry point
  solver.*                  top-level solve flow
  clock_tree.*              mutable clock topology
  datapath_graph.*          parsed data-path timing graph
  evaluation.*              ground-truth scoring
  debug_progress.*          optimizer telemetry helper
  optimization/             optimizer implementations and config
  parser.*                  testcase parsers
tests/                      Catch2 tests
scripts/                    local, Slurm, plotting, and visualization tools
docs/                       architecture and experiment notes
```

When adding source files, put `.hpp` next to `.cpp` under `src/`, add new `.cpp`
files to `cadd0040_core` in `CMakeLists.txt`, and add new tests to
`tests/CMakeLists.txt`.

## Development Notes

Install the formatting hook once per checkout:

```sh
pre-commit install
```

Useful references:

- [`AGENTS.md`](AGENTS.md): canonical coding-agent and project rules.
- [`docs/optimization-architecture.md`](docs/optimization-architecture.md):
  optimizer-side architecture.
- [`docs/optimization-algorithms.md`](docs/optimization-algorithms.md):
  A1-A8 algorithm descriptions.
- [`docs/optimization-complexity.md`](docs/optimization-complexity.md):
  complexity and hot-path notes.
- [`docs/optimization-experiment-parameters.md`](docs/optimization-experiment-parameters.md):
  experiment defaults, traces, and recommended commands.

Keep reusable logic out of `src/main.cpp`; it should stay as CLI wiring. Use
`DebugProgress` for optimizer telemetry instead of direct `std::cerr` in
optimizers.

## Contribution Notes

- Keep `main` stable and use `dev` as the integration branch.
- Create feature branches from `dev`, such as `feat/clock-tree-parser`.
- Rebase only your own feature branches; do not rebase shared branches.
- Run `make test` before opening a pull request.
- Use short imperative commit messages, for example `Add clock tree parser`.
