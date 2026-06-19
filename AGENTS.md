# Agent Guide (ICCAD 2026 Problem D)

Canonical instructions for Codex, Cursor, and other coding agents.

## Project

C++17 CMake project for ICCAD Contest 2026 Problem D clock-tree optimization.
Tests use Catch2 v3. The CLI uses CLI11.

## Build And Test

```sh
make build
make release
make test
./scripts/run_all_testcases.sh
```

Single testcase:

```sh
./build/cadd0040 <testcase_dir> <output_file> [--optimizer <name>] [--seed <n>] [--seconds <n>] [--config <file>] [--debug]
```

Slurm/local optimizer matrix:

```sh
./scripts/slurm_run_all_optimizers.sh
./scripts/slurm_run_all_optimizers.sh --local
```

## Documentation

- `docs/architecture.md`: solver flow, data structures, telemetry, output behavior.
- `docs/optimizers.md`: optimizer aliases, policies, defaults, complexity, extension rules.
- `docs/experiments.md`: scripts, run directories, TSVs, reports, config files.
- `docs/analyze.md`: analysis notes kept as-is.

## Code Layout

- `src/main.cpp` is CLI boundary only.
- `src/app.*` owns CLI parsing and `AppConfig`.
- `src/solver.*` owns the top-level solve workflow.
- `src/optimization/` owns optimizer implementations and config loading.
- New `.cpp` files must be added to `cadd0040_core` in `CMakeLists.txt`.
- New tests should be added to `tests/CMakeLists.txt`.

## DebugProgress

All optimizer and solver algorithm telemetry must use `DebugProgress`.
Do not write optimizer status directly to `std::cerr`.

```cpp
DebugProgress& debug = context.debug_progress;

debug.log([&](std::ostream& os) {
    os << "MyOptimizer: baseline score = " << score << '\n';
});

debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
```

Rules:

- `AppConfig` builds `DebugProgress` from CLI `--debug`.
- Release builds are silent.
- Debug builds require `--debug` for optimizer status.
- Direct `std::cerr` is allowed for `main.cpp`, `Solver::run()` exceptions, and
  `debug_progress.cpp` internals.
- Do not add algorithm telemetry in low-level helpers such as
  `ClockTree::insert_buffer`.

## Optimizer Rules

Default optimizer: `tabu-random`.

A1-A13 aliases live in `src/optimization/factory.cpp`. Keep numeric aliases
uppercase and preserve existing behavior unless the user explicitly asks for a
matrix change.

Hot-path rules:

- use id-based `ClockTree` APIs;
- keep original contest nodes non-removable;
- only inserted buffers may be removed;
- undo rejected trials through both `TimingState` and `ClockTree`;
- keep timing cache logic in `TimingState`;
- keep search policy in optimizer implementations;
- keep `candidate_policy.*` as the only shared action-generation/apply/undo
  layer.

Optimizer ownership:

- A1-A5: `greedy/`, shared `GreedyOptimizer`, differ by `CandidatePolicy`.
- A6/A10: `two_step/`, local TwoStepSlackThenScore flow.
- A7/A8/A11/A12: `sa/`, local Metropolis and iterated-SA logic.
- A9/A13: `tabu/`, local tabu memory, aspiration, and selection logic.
- `milp`: legacy runnable heuristic, not part of default A1-A13 matrix.
- `visual`: visualization tool, not part of Slurm optimizer output.

## Experiment Controls

Use CLI options for direct runs:

- `--optimizer`
- `--seed`
- `--seconds`
- `--config`
- `--debug`
- `--progress-dir`
- `--progress-steps`

`CADD0040_SA_SECONDS` remains as a legacy global time-budget override. Avoid
adding new environment toggles for normal experiment behavior.

Slurm optimizer runs always produce numeric `progress.tsv` files and do not
produce visualization frames.
