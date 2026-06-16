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
./build/cadd0040 <testcase_dir> <output_file> [--optimizer <name>]
CADD0040_DEBUG_PROGRESS=1 ./build/cadd0040 <testcase_dir> <output>
```

| Variable | Purpose |
|----------|---------|
| `CADD0040_SA_SECONDS` | Optimizer time budget (default 570) |
| `CADD0040_CHECKPOINT_STEPS` | Write best-so-far output every N optimizer steps; `0` disables (default 4096) |
| `CADD0040_REPORT_METRICS` | `1` prints initial/final metrics and scores from `Solver` |
| `CADD0040_PROGRESS_TRACE` | `1` writes lightweight `progress.tsv`; default `0` |
| `CADD0040_PROGRESS_STEPS` | Progress trace logical step interval (default 256) |
| `CADD0040_VISUAL_TRACE` | `1` writes sampled `frames.json`; default `0` |
| `CADD0040_VISUAL_TRACE_STEPS` | Visual frame logical step interval (default 256) |
| `CADD0040_DEBUG_PROGRESS` | `1` enables debug telemetry (debug builds) |
| `CADD0040_DEBUG_PROGRESS_INTERVAL` | Seconds between `Progress` lines (default 30) |

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

- `Solver` creates `DebugProgress::from_environment()` and passes it via `OptimizerContext`.
- **Release** (`NDEBUG`): always silent.
- **Debug**: requires `CADD0040_DEBUG_PROGRESS=1`; interval via `CADD0040_DEBUG_PROGRESS_INTERVAL`.
- `debug.log` — one-off lines (phases, summaries). `report_if_due` — periodic loops (stderr prefix `Progress`).

Allowed direct `std::cerr`: `main.cpp` / `Solver::run()` exceptions; `debug_progress.cpp` internals. Do not add algorithm telemetry in low-level helpers (e.g. `ClockTree::insert_buffer`).

Reference: `annealing_optimizer.cpp`, `iterated_sa_optimizer.cpp`, `greedy_optimizer.cpp`, `milp_optimizer.cpp`, `solver.cpp`.

## Optimization module

Applies when editing `src/optimization/`.

### Layout

```
src/optimization/
├── optimizer.hpp       # OptimizerContext { baseline_metrics, debug_progress, checkpoint writer }
├── factory.cpp         # register optimizer aliases
├── sa/                 # simulated annealing
├── greedy/             # A1/A4/A5 same best-improvement greedy class
├── repair_recover/     # A6 Greedy-RepairRecover
├── randomized_rcl/     # A7 Greedy-RandomizedRCL
├── tabu/               # A8 Tabu
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

Default CLI value: `isa` (`kDefaultOptimizerName` in `factory.hpp`).

| Alias | Class |
|-------|-------|
| `greedy-violation-path` | `GreedyOptimizer(ViolationPath)` |
| `sa` | `AnnealingOptimizer` |
| `isa` | `IteratedSaOptimizer` |
| `greedy-critical-endpoint` | `GreedyOptimizer(CriticalEndpoint)` |
| `greedy-upstream-window` | `GreedyOptimizer(UpstreamWindow)` |
| `greedy-repair-recover` | `GreedyRepairRecoverOptimizer` |
| `greedy-randomized-rcl` | `GreedyRandomizedRclOptimizer` |
| `tabu` | `TabuOptimizer` |
| `milp` | `MilpOptimizer` (legacy runnable; not A1-A8 default) |
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
- Do not add cross-optimizer helper layers for candidate policy or search policy.
- A1/A4/A5 may share `GreedyOptimizer` because they are the same best-improvement greedy and only
  differ by candidate generation. A6/A7/A8 must keep their helpers local to their own folders.
- New optimizer `.cpp` files must be added to `cadd0040_core` in `CMakeLists.txt`, registered in `factory.cpp`, and covered by tests.

### Tuning

- Optimizer defaults live in `src/optimization/optimizer_config.hpp`.
- Environment overrides live in `src/optimization/optimizer_config.cpp`.
- `CADD0040_SA_SECONDS` remains the legacy time-budget override.
- `CADD0040_CHECKPOINT_STEPS` controls best-so-far output checkpoint frequency.
- `CADD0040_PROGRESS_TRACE` / `CADD0040_VISUAL_TRACE` are optional and default off; keep full
  experiments lightweight.
- Batch run: `./scripts/run_all_testcases.sh`

### Deep architecture

See `docs/optimization-architecture.md`, `docs/optimization-algorithms.md`,
`docs/optimization-complexity.md`, and `docs/optimization-experiment-parameters.md`.
