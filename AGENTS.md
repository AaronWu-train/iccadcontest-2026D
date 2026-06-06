# Agent Guide (ICCAD 2026 Problem D)

Canonical instructions for Codex, Cursor, and other coding agents.  
`.cursor/rules/*.mdc` files are thin, scoped wrappers that point here.

## Project

C++20 CMake project for ICCAD Contest 2026 Problem D (clock tree optimization).  
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
| `CADD0040_SA_SECONDS` | SA time budget (default 540) |
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
├── optimizer.hpp       # OptimizerContext { baseline_metrics, debug_progress }
├── factory.cpp         # register optimizer aliases
├── sa/                 # simulated annealing
├── greedy/
└── milp/
```

### Module roles

| Module | Role | Mutable during optimization? |
|--------|------|------------------------------|
| `DataPathGraph` | read-only paths and fixed data delays | No |
| `SkewModel` | in-memory incremental timing sandbox | Yes |
| `ClockTree` | real tree; written only at end via `materialize()` | End only |
| `evaluate()` | ground-truth scoring; too slow per step | — |

### Optimizer registration (`factory.cpp`)

Default CLI value: `isa` (`kDefaultOptimizerName` in `factory.hpp`).

| Alias | Class |
|-------|-------|
| `isa` / `sa2` | `IteratedSaOptimizer` |
| `anneal` / `sa` | `AnnealingOptimizer` |
| `greedy` / `detgreedy` | `GreedyOptimizer` |
| `milp` / `ip` | `MilpOptimizer` |
| `dummy` | `DummyOptimizer` (no-op, testing) |

Register new optimizers in `optimizer_registry()` inside `factory.cpp`; expose names via `available_optimizers()`.

### SkewModel invariants

- `affected_path_epoch_.size()` must equal **path count** (`launch_idx_.size()`), not node count.
- Inserted buffers use fanout=1 delay entry; resize must pass `cell_supports_fanout`.
- Moves must be reversible via `undo_move()` for Metropolis rejection.

### Tuning

- `CADD0040_SA_SECONDS` overrides default 540s SA budget.
- Batch run: `./scripts/run_all_testcases.sh`

### Deep architecture

See `docs/annealing-optimizer.md` for data flows, scoring formula, tests, and file list.
