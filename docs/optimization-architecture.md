# Optimization Architecture

This document describes the optimizer-side architecture after the ClockTree edit API and
TimingState refactor.

## Four layers

```text
ClockTree      = id-based mutable clock topology
DataPathGraph  = read-only data path input
TimingState    = incremental timing and score cache
Optimizer      = independent search policy
```

The main rule is that topology, input timing data, cached timing state, and search policy are
separate. Optimizers may mutate a working `ClockTree`, but they should not use name-based tree
lookups in hot loops.

Complexity details are tracked in [`optimization-complexity.md`](optimization-complexity.md).
Experiment parameters are tracked in
[`optimization-experiment-parameters.md`](optimization-experiment-parameters.md).

## ClockTree

`ClockTree` owns the mutable clock topology.

It is responsible for:

- Root, buffer, and flip-flop nodes.
- Parent, children, edge, fanout, and cell type relationships.
- Stable `NodeId` and `EdgeId` handles.
- Name-based compatibility for parser, writer, and debug paths.
- Protecting original contest nodes from deletion.

Each node has:

- `NodeKind`: `ClockSource`, `Buffer`, or `FlipFlop`.
- `NodeOrigin`: `Original` or `Inserted`.
- `alive`: whether the node is currently part of the active tree.

Inserted buffers are removed lazily: the node remains in the backing vector with
`alive = false`, and the parent/child edge is spliced around it. This keeps existing `NodeId`
values stable.

Optimizer edits should use the id-based API:

```cpp
ClockTreeEdit insert_buffer_on_edge(EdgeId edge, ...);
ClockTreeEdit resize_buffer(NodeId buffer, ...);
ClockTreeEdit remove_inserted_buffer(NodeId buffer);
void undo(const ClockTreeEdit& edit);
```

These edits are reversible. `ClockTreeEdit` records the topology and cell data needed for
`ClockTree::undo()`.

The name-based API still exists for:

- Parsing input clock trees.
- Writing output clock trees.
- Small tests and debug tooling.
- Legacy helpers such as the visualization optimizer.

It should not become the optimizer hot path again.

## Output rules

Output traversal skips dead inserted nodes. Original nodes and active inserted buffers keep a
stable traversal order, so generated structure files remain deterministic.

Original nodes are contest input objects and must not be removed. Inserted buffers may be
resized, removed, and restored by undo.

## DataPathGraph

`DataPathGraph` is the read-only input timing model.

It stores:

- Path name.
- Launch FF name.
- Capture FF name.
- SS and FF data delays.
- Clock period, setup time, and hold time.

It does not store optimizer slack cache and does not change when the clock tree changes.

Do not merge `DataPathGraph` into `ClockTree`. They describe different domains:

- `ClockTree` describes clock topology and clock arrival.
- `DataPathGraph` describes fixed data paths between flip-flops.

The binding happens in `TimingState`.

## TimingState

`TimingState` is the optimizer timing sandbox.

It binds:

- `ClockTree`
- `DataPathGraph`
- `BufferLibrary`

During construction it converts FF names from `DataPathGraph` into `NodeId`s and builds
id-based timing paths:

```cpp
struct TimingPath {
    PathId id;
    NodeId launch_ff;
    NodeId capture_ff;
    double data_delay_ss;
    double data_delay_ff;
};
```

It maintains:

- Clock arrival for SS and FF corners.
- Setup and hold slack.
- TNS and WNS.
- Area.
- Current metrics and score inputs.

It supports:

```cpp
Metrics metrics() const;
double score(const Metrics& baseline) const;
void apply(const ClockTreeEdit& edit);
void undo(const ClockTreeEdit& edit);
TimingSnapshot snapshot() const;
void restore(const TimingSnapshot& snapshot);
```

`TimingState` updates only the affected subtree and affected timing paths for each clock-tree
edit. Full `evaluate()` remains the ground-truth checker, but it is too slow for every optimizer
trial step.

`TimingState` must not contain search policy. Keep these out of it:

- Random move generation.
- Guided insert selection.
- Greedy candidate selection.
- SA Metropolis acceptance.
- Optimizer-specific cleanup or polish strategy.

## Optimizer

Optimizers own the search policy.

They may share:

- `ClockTree`
- `DataPathGraph`
- `TimingState`
- Common SA-family phase utilities

They should not share candidate policy unless the algorithms intentionally belong to the same
family. Greedy, MILP-inspired, SA, and ISA should remain easy to change independently.

Main optimizers:

- `greedy-random`: A1 BestScore greedy over random action-space samples.
- `greedy-violation-path`: A2 BestScore greedy using violated path endpoints.
- `greedy-upstream-window`: A3 BestScore greedy using upstream endpoint windows.
- `greedy-critical-endpoint`: A4 BestScore greedy from top critical endpoints.
- `greedy-union-pool`: A5 BestScore greedy over the UnionPool candidate set.
- `two-step-union-pool`: A6 UnionPool with TwoStepSlackThenScore acceptance.
- `sa-sampled-union-pool`: A7 SampledUnionPool with Metropolis acceptance.
- `isa-sampled-union-pool`: A8 SampledUnionPool with iterated Metropolis rounds.
- `tabu-union-pool`: A9 UnionPool with tabu memory, aspiration, and best non-tabu acceptance.
- `two-step-random`: A10 RandomActionSpace with TwoStepSlackThenScore acceptance.
- `sa-random`: A11 RandomActionSpace with Metropolis acceptance.
- `isa-random`: A12 RandomActionSpace with iterated Metropolis rounds.
- `tabu-random`: A13 RandomActionSpace with tabu memory, aspiration, and best non-tabu acceptance.
- `two-step-optimize`, `sa`, `isa`, and `tabu`: compatibility aliases for A6-A9.
- `milp`: legacy MILP-inspired violation-driven heuristic; not a true MILP solver.
- `visual`: visualization and trace tool, kept out of main architecture comparisons.

Source placement:

- `optimization/candidate_policy.*`: shared CandidatePolicy move generation, dedupe, apply, and
  undo helpers.
- `optimization/greedy/greedy_optimizer.*`: A1-A5. One BestScore greedy class selected by
  `CandidatePolicy`.
- `optimization/two_step/two_step_optimizer.*`: A6/A10 TwoStepOptimize.
- `optimization/sa/*`: A7/A8/A11/A12 SA-family optimizers.
- `optimization/tabu/tabu_optimizer.*`: A9/A13 Tabu.

A6-A13 intentionally keep AcceptPolicy and search-loop logic local. Do not add another shared
optimizer framework above `candidate_policy.*`.

## Parameters

Optimizer constants live in `src/optimization/optimizer_config.hpp`.

Use:

- `GreedyConfig`
- `MilpConfig`
- `SaConfig`
- `IsaConfig`
- `TwoStepConfig`
- `TabuConfig`

Defaults should not be scattered through optimizer bodies. Environment overrides belong in
`optimizer_config.cpp`. The legacy `CADD0040_SA_SECONDS` override is still supported.

## Extension checklist

When adding or changing an optimizer:

- Use id-based `ClockTree` APIs in hot loops.
- Keep original nodes non-removable.
- Keep inserted-buffer edits reversible.
- Put timing cache changes in `TimingState`, not in the optimizer.
- Put search policy in the optimizer, not in `TimingState`.
- Register the optimizer in `src/optimization/factory.cpp`.
- Add new `.cpp` files to `cadd0040_core` in `CMakeLists.txt`.
- Add focused tests under `tests/`.
- Use `DebugProgress` for optimizer telemetry.
