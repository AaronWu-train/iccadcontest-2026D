# Architecture

This document describes the implementation shape of the solver. It is intended
as the first place to look when changing parser, solver flow, data structures,
telemetry, or output behavior.

## Program Flow

`src/main.cpp` only parses errors at the process boundary. Normal application
setup lives in `src/app.cpp`, and the solve workflow lives in `src/solver.cpp`.

The high-level flow is:

```text
CLI arguments
  -> AppConfig
  -> Solver::load_input()
  -> baseline evaluate()
  -> make_optimizer()
  -> optimizer.run(...)
  -> final output write
```

`AppConfig` owns the selected optimizer name, optional optimizer config file,
debug output state, progress output settings, testcase paths, and output path.

`Solver` is responsible for:

- parsing testcase files into `ClockTree`, `DataPathGraph`, and `BufferLibrary`;
- computing the baseline metrics;
- creating `OptimizerContext`;
- wiring debug, checkpoint, progress, and visualization writers;
- invoking the selected optimizer;
- writing `modified_clk_tree.structure` atomically.

## Main Data Types

`ClockTree` owns the mutable clock topology. It stores clock source, buffer, and
flip-flop nodes with stable `NodeId` and `EdgeId` handles. Original contest
nodes are protected from removal. Inserted buffers can be removed lazily: the
node stays allocated but is marked dead, and output traversal skips it.

`DataPathGraph` owns read-only data-path timing input. It stores launch/capture
flip-flops and SS/FF data delays. It should not be merged into `ClockTree`; the
two structures describe different domains.

`TimingState` is the optimizer timing sandbox. It binds `ClockTree`,
`DataPathGraph`, and `BufferLibrary`, then maintains cached clock arrivals,
setup/hold slack, TNS/WNS, area, and score inputs. Optimizer hot paths should use
`TimingState::apply()`, `undo()`, `snapshot()`, and `restore()` instead of full
`evaluate()` for every trial move.

`evaluate()` remains the ground-truth scoring path for baseline/final checks.
It is intentionally not the per-candidate hot path.

## Reversible Edits

Optimizer edits are represented as reversible `ClockTreeEdit` operations:

- insert a buffer on an active edge;
- resize an active buffer;
- remove an inserted buffer.

Rejected trials must undo both timing and topology:

```cpp
timing_state.undo(edit);
clock_tree.undo(edit);
```

Original contest nodes must never be removed. Name-based `ClockTree` APIs remain
for parser, writer, debug, tests, and compatibility paths, but optimizer hot
loops should use id-based APIs.

## OptimizerContext

`OptimizerContext` is the bridge from `Solver` to optimizers. It carries:

- baseline metrics;
- `DebugProgress`;
- optimizer and testcase names;
- optional config file data;
- checkpoint writer and interval;
- progress writer and interval;
- visualization writer and interval.

Optimizers should use the context writers rather than opening files directly.
Candidate-level trials should not be recorded.

## Telemetry

There are three different output channels:

| Channel | How to enable | Destination | Intended use |
|---------|---------------|-------------|--------------|
| Debug progress | `--debug` | stderr | Human-readable local debugging |
| Numeric progress | `--progress-dir <dir>` | `progress.tsv` | Score/timing curves and reports |
| Visualization frames | `CADD0040_VISUAL_TRACE=1` | `frames.json` | Clock-tree animation/inspection |

`DebugProgress` is silent in release builds and requires `--debug` in debug
builds. Optimizer telemetry must use `DebugProgress`; do not write algorithm
status directly to `std::cerr` from optimizers.

Debug builds print initial/final metrics to stdout. Release builds suppress
those summaries.

Slurm optimizer runs always enable numeric progress and never enable
visualization frames.

## Output Files

Direct solver output is the requested clock-tree structure path. During
optimization, `Solver` also writes checkpoint output to the same destination
using atomic replacement.

With `--progress-dir`, the solver writes:

```text
<progress-dir>/progress.tsv
```

The progress TSV columns are:

```text
optimizer testcase step elapsed_sec phase round event current_score best_score delta_score
tns_ss wns_ss tns_ff wns_ff area accepted_moves rejected_moves candidate_policy accept_policy
```

With visualization enabled, the solver writes:

```text
visual_trace/<optimizer>/<testcase>/frames.json
```

or the directory specified by `CADD0040_VISUAL_TRACE_DIR`.

## Repository Layout

```text
src/
  app.*                         CLI parsing and AppConfig
  solver.*                      top-level solve workflow
  clock_tree.*                  mutable clock topology
  datapath_graph.*              read-only data path timing graph
  evaluation.*                  full scoring path
  optimization/
    optimizer.*                 Optimizer interface and context
    optimizer_config.*          defaults and INI config parsing
    candidate_policy.*          shared action generation/apply/undo layer
    timing_state.*              incremental timing cache
    greedy/                     A1-A5
    two_step/                   A6/A10
    sa/                         A7/A8/A11/A12
    tabu/                       A9/A13
    milp/                       legacy MILP-inspired heuristic
    visual/                     visualization optimizer
tests/
  Catch2 tests
scripts/
  batch, Slurm, report, and visualization helpers
```
