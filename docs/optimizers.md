# Optimizers

This document describes the registered optimizer aliases, the shared local
search model, default parameters, and implementation boundaries.

## Default Optimizer

The default CLI optimizer is `tabu-random`, defined as `kDefaultOptimizerName`
in `src/optimization/factory.hpp`.

Numeric aliases are uppercase only. For example, `A1` is valid and `a1` is not.

## A1-A13 Matrix

| ID | Alias | CandidatePolicy | AcceptPolicy |
|----|-------|-----------------|--------------|
| `A1` | `greedy-random` | `RandomActionSpace` | `BestScore` |
| `A2` | `greedy-violation-path` | `ViolationPath` | `BestScore` |
| `A3` | `greedy-upstream-window` | `UpstreamWindow` | `BestScore` |
| `A4` | `greedy-critical-endpoint` | `CriticalEndpoint` | `BestScore` |
| `A5` | `greedy-union-pool` | `UnionPool` | `BestScore` |
| `A6` | `two-step-union-pool` | `UnionPool` | `TwoStepSlackThenScore` |
| `A7` | `sa-sampled-union-pool` | `SampledUnionPool` | `Metropolis` |
| `A8` | `isa-sampled-union-pool` | `SampledUnionPool` | `IteratedMetropolis` |
| `A9` | `tabu-union-pool` | `UnionPool` | `TabuBestNonTabu` |
| `A10` | `two-step-random` | `RandomActionSpace` | `TwoStepSlackThenScore` |
| `A11` | `sa-random` | `RandomActionSpace` | `Metropolis` |
| `A12` | `isa-random` | `RandomActionSpace` | `IteratedMetropolis` |
| `A13` | `tabu-random` | `RandomActionSpace` | `TabuBestNonTabu` |

Compatibility aliases:

- `two-step-optimize` -> A6 behavior
- `sa` -> A7 behavior
- `isa` -> A8 behavior
- `tabu` -> A9 behavior

Additional registered aliases:

- `milp`: legacy MILP-inspired heuristic, not a true MILP solver and not part
  of the default A1-A13 matrix.
- `visual`: clock-tree visualization/trace tool.
- `dummy`: no-op optimizer for tests.

## Shared Search Model

All main optimizers operate on the same legal edit set:

- insert a buffer on an active edge;
- resize an active buffer to another legal cell;
- remove an inserted buffer.

Original contest nodes cannot be removed. Candidate trials use reversible
`ClockTreeEdit` operations and `TimingState` scoring. Rejected trials must undo
both timing state and clock-tree topology.

The score is computed from `src/evaluation.cpp`:

```text
Score = 0.5 * SS improvement + 0.25 * FF improvement + 0.25 * area improvement
```

## Candidate Policies

| CandidatePolicy | Meaning |
|-----------------|---------|
| `RandomActionSpace` | Random samples from insert, remove, and resize actions. |
| `ViolationPath` | Insert candidates around severe violated launch/capture endpoints. |
| `UpstreamWindow` | Insert candidates along a short upstream window from violated endpoints. |
| `CriticalEndpoint` | Insert candidates at endpoints ranked by accumulated negative slack. |
| `UnionPool` | Combined focused insert policies plus remove and resize candidates. |
| `SampledUnionPool` | One sampled proposal from UnionPool action families. |

`candidate_policy.*` is the only shared action-generation/apply/undo layer.
Avoid adding another generic optimizer framework above it.

## Accept Policies And Flows

A1-A5 share `GreedyOptimizer`. Each iteration builds a candidate set, scores
candidate edits with `TimingState`, accepts the positive move with highest total
score, and finishes with bounded resize polish.

A6/A10 use `TwoStepOptimizeOptimizer`. The first phase accepts positive
slack-objective moves, and the second phase accepts positive total-score moves.
A6 uses `UnionPool`; A10 uses `RandomActionSpace`.

A7/A11 use simulated annealing. Both run bounded greedy warmup, a time-driven
SA phase, restore the best state, and run bounded final polish. A7 samples from
`SampledUnionPool`; A11 samples from `RandomActionSpace`.

A8/A12 use iterated simulated annealing. They split the remaining time into
rounds, restore the best state between rounds, and run small greedy cleanup
batches. A8 uses `SampledUnionPool`; A12 uses `RandomActionSpace`.

A9/A13 use tabu search. They choose the best legal non-tabu candidate, allow
aspiration for global-best improvements, and restore the best seen tree at the
end. A9 uses `UnionPool`; A13 uses `RandomActionSpace`.

## Default Parameters

Defaults live in `src/optimization/optimizer_config.hpp`.

| Family | Important defaults |
|--------|--------------------|
| Greedy A1-A5 | `time_budget=570s`, `max_steps=4096`, `candidate_limit=4096`, `max_resize_polish_steps=96`, `max_polish_phases=64` |
| TwoStep A6/A10 | `time_budget=570s`, `timing_steps=2048`, `score_steps=2048`, `candidate_limit=4096` |
| SA A7/A11 | `time_budget=570s`, `greedy_warmup_iterations=256`, `final_greedy_polish_iterations=32`, `restart_stale_iterations=2500` |
| ISA A8/A12 | `time_budget=570s`, `rounds=16`, `greedy_round_iterations=16`, `final_greedy_polish_iterations=32` |
| Tabu A9/A13 | `time_budget=570s`, `max_steps=4096`, `tenure=128`, `candidate_limit=4096` |

`CADD0040_SA_SECONDS` remains as a legacy global time-budget override when no
config file value overrides it. Prefer CLI `--seconds` or a config file for new
manual runs.

## Complexity Notes

Full evaluation is roughly:

```text
O(N + P * H)
```

where `N` is clock-tree node count, `P` is data-path count, and `H` is clock-tree
height.

Optimizer trials use reversible clock-tree edits plus incremental timing state:

```text
O(affected subtree + affected data paths)
```

The practical trial costs are:

- insert: affected child subtree plus incident paths;
- resize: resized buffer subtree plus incident paths;
- remove: current edge lookup plus affected child subtree and incident paths.

Remaining known hot spots include `edge_between()` scans, vector allocation in
random sampling helpers, and accumulated dead inserted nodes during long runs.

## Extension Checklist

When adding or changing an optimizer:

- use id-based `ClockTree` APIs in hot loops;
- keep original contest nodes non-removable;
- keep inserted-buffer edits reversible;
- put timing cache changes in `TimingState`, not the optimizer;
- put search policy in the optimizer, not `TimingState`;
- register aliases in `src/optimization/factory.cpp`;
- add new `.cpp` files to `cadd0040_core` in `CMakeLists.txt`;
- add focused tests under `tests/`;
- use `DebugProgress` for optimizer telemetry.
