# Agent Guide (ICCAD 2026 Problem D)

Canonical instructions for Codex, Cursor, and other coding agents.  
`.cursor/rules/*.mdc` files are thin, scoped wrappers that point here.

## Project

C++17 CMake project for ICCAD Contest 2026 Problem D (clock tree optimization).  
Catch2 v3 tests, CLI11 CLI, clang-format via pre-commit.

## Build and test

```sh
make build          # debug build in build/
make release        # optimized build in build-release/
make test           # Catch2 via CTest
./scripts/run_all_testcases.sh
```

Single testcase:

```sh
./build/cadd0040 <testcase_dir> <output_file> [--optimizer <name>] [--seconds <n>] [--config <file>] [--debug]
```

Optional experiment config file (`--config`): INI `key = value` format. Global keys include
`optimizer`, `seed`, and `time_budget_seconds`. Per-optimizer sections use the optimizer alias as
the section name (for example `[isa-sampled-union-pool]`). When present, config values override environment variables
and the config `optimizer` key overrides `--optimizer`.

| Variable | Purpose |
|----------|---------|
| `CADD0040_SA_SECONDS` | Optimizer time budget (default 570) |

## Code layout and style

- Keep `.hpp` next to `.cpp` under `src/`.
- `src/main.cpp` is CLI entry only; logic lives in `cadd0040_core`.
- Add new `.cpp` to `cadd0040_core` in `CMakeLists.txt`; new tests to `cadd0040_tests` in `tests/CMakeLists.txt`.
- Pre-commit runs `clang-format` on staged C/C++ (`pre-commit install`).

## DebugProgress (algorithm output)

All optimizer and solver **algorithm telemetry** (phases, baseline, warmup, summaries, periodic best score) must use `DebugProgress`. **Do not** use direct `std::cerr` in optimizers.

```cpp
DebugProgress& debug = context.debug_progress;

debug.log([&](std::ostream& os) {
    os << "MyOptimizer: baseline score = " << score << '\n';
});

debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
```

- `AppConfig` owns the `DebugProgress` built from CLI `--debug`; `Solver` passes it via `OptimizerContext`.
- **Release** (`NDEBUG`): always silent.
- **Debug**: requires `--debug`; periodic interval is fixed in `DebugProgress`.
- `debug.log` — one-off human-readable stderr lines (phases, summaries).
  `report_if_due` — periodic human-readable stderr status (stderr prefix `Progress`).
- Solver initial/final metric summaries are stdout build-type output: debug builds print them;
  release builds suppress them.

Allowed direct `std::cerr`: `main.cpp` / `Solver::run()` exceptions; `debug_progress.cpp` internals. Do not add algorithm telemetry in low-level helpers (e.g. `ClockTree::insert_buffer`).

Reference: `annealing_optimizer.cpp`, `iterated_sa_optimizer.cpp`, `greedy_optimizer.cpp`, `milp_optimizer.cpp`, `solver.cpp`.

## Optimization module

Applies when editing `src/optimization/`.

### Layout

```
src/optimization/
├── optimizer.hpp       # OptimizerContext { baseline_metrics, debug_progress, checkpoint writer }
├── factory.cpp         # register optimizer aliases
├── candidate_policy.*  # shared CandidatePolicy action generation/apply/undo
├── sa/                 # simulated annealing
├── greedy/             # A1-A5 same BestScore greedy class
├── two_step/           # A6/A10 TwoStepOptimize
├── tabu/               # A9/A13 Tabu
└── milp/
```

### Module roles

| Module | Role | Mutable during optimization? |
|--------|------|------------------------------|
| `DataPathGraph` | read-only paths and fixed data delays | No |
| `ClockTree` | id-based mutable clock topology | Yes |
| `TimingState` | incremental timing and score cache bound to `ClockTree` + `DataPathGraph` | Yes |
| `evaluate()` | ground-truth scoring; too slow per step | — |

Optimizers should use id-based `ClockTree` APIs in hot loops. Keep name-based APIs for parser,
writer, debug, and compatibility paths.

### Optimizer registration (`factory.cpp`)

Default CLI value: `tabu-random` (`kDefaultOptimizerName` in `factory.hpp`).

| Alias | Class |
|-------|-------|
| `A1`, `greedy-random` | `GreedyOptimizer(RandomActionSpace)` |
| `A2`, `greedy-violation-path` | `GreedyOptimizer(ViolationPath)` |
| `A3`, `greedy-upstream-window` | `GreedyOptimizer(UpstreamWindow)` |
| `A4`, `greedy-critical-endpoint` | `GreedyOptimizer(CriticalEndpoint)` |
| `A5`, `greedy-union-pool` | `GreedyOptimizer(UnionPool)` |
| `A6`, `two-step-union-pool`, `two-step-optimize` | `TwoStepOptimizeOptimizer(UnionPool)` |
| `A7`, `sa-sampled-union-pool`, `sa` | `AnnealingOptimizer(SampledUnionPool)` |
| `A8`, `isa-sampled-union-pool`, `isa` | `IteratedSaOptimizer(SampledUnionPool)` |
| `A9`, `tabu-union-pool`, `tabu` | `TabuOptimizer(UnionPool)` |
| `A10`, `two-step-random` | `TwoStepOptimizeOptimizer(RandomActionSpace)` |
| `A11`, `sa-random` | `AnnealingOptimizer(RandomActionSpace)` |
| `A12`, `isa-random` | `IteratedSaOptimizer(RandomActionSpace)` |
| `A13`, `tabu-random` | `TabuOptimizer(RandomActionSpace)` |
| `milp` | `MilpOptimizer` (legacy runnable; not A1-A13 default) |
| `visual` | `ClockTreeTraceOptimizer` (visualization/trace tool) |
| `dummy` | `DummyOptimizer` (no-op, testing) |

Register new optimizers in `optimizer_registry()` inside `factory.cpp`; expose names via `available_optimizers()`.

### ClockTree / TimingState invariants

- `ClockTree` nodes are separated by `NodeKind`: `ClockSource`, `Buffer`, `FlipFlop`.
- `NodeOrigin::Original` contest nodes cannot be removed.
- `NodeOrigin::Inserted` buffers can be removed; removal marks them dead and splices the tree.
- Output traversal skips dead inserted nodes.
- `TimingState` owns timing/score cache only. Do not put random move generation, greedy policy, or SA Metropolis logic in it.
- Reversible edits must call both `TimingState::undo(edit)` and `ClockTree::undo(edit)` when rejected.
- `candidate_policy.*` is the only shared action-generation/apply/undo layer.
- A1-A5 share `GreedyOptimizer` because they are the same BestScore greedy and only differ by
  `CandidatePolicy`.
- A6/A7/A8/A9 keep their AcceptPolicy/search loops local to their own folders.
- New optimizer `.cpp` files must be added to `cadd0040_core` in `CMakeLists.txt`, registered in `factory.cpp`, and covered by tests.

### Tuning

- Optimizer defaults live in `src/optimization/optimizer_config.hpp`.
- Environment overrides live in `src/optimization/optimizer_config.cpp`.
- Optional experiment config files are loaded via CLI `--config` and parsed in
  `src/optimization/optimizer_config.cpp`.
- `CADD0040_SA_SECONDS` remains the legacy time-budget override when no config file is used.
- Numeric progress TSVs are enabled with `--progress-dir` for direct runs and always on in the
  Slurm optimizer runner; keep visualization frame generation out of Slurm runs.
- Batch run: `./scripts/run_all_testcases.sh`

### Deep architecture

See `docs/optimization-architecture.md`, `docs/optimization-algorithms.md`,
`docs/optimization-complexity.md`, and `docs/optimization-experiment-parameters.md`.
