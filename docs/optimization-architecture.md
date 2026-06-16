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

- `greedy-violation-path`: best-improvement greedy using violated path endpoints.
- `sa`: single simulated annealing flow.
- `isa`: iterated simulated annealing flow and default optimizer.
- `greedy-critical-endpoint`: same greedy framework, candidates from top critical endpoints.
- `greedy-upstream-window`: same greedy framework, candidates from upstream endpoint windows.
- `greedy-repair-recover`: timing repair followed by area recovery.
- `greedy-randomized-rcl`: randomized greedy top-k move selection with restarts.
- `tabu`: tabu search with aspiration and memory-based local-optimum escape.
- `milp`: legacy MILP-inspired violation-driven heuristic; not a true MILP solver.
- `visual`: visualization and trace tool, kept out of main architecture comparisons.

Source placement:

- `optimization/greedy/greedy_optimizer.*`: A1/A4/A5. One best-improvement greedy class,
  selected by `GreedyCandidatePolicy`.
- `optimization/repair_recover/repair_recover_optimizer.*`: A6 Greedy-RepairRecover.
- `optimization/randomized_rcl/randomized_rcl_optimizer.*`: A7 Greedy-RandomizedRCL.
- `optimization/tabu/tabu_optimizer.*`: A8 Tabu.

A6/A7/A8 intentionally keep local candidate structs and local apply/undo helpers. Do not add a
shared optimizer helper layer for these policies.

## Parameters

Optimizer constants live in `src/optimization/optimizer_config.hpp`.

Use:

- `GreedyConfig`
- `MilpConfig`
- `SaConfig`
- `IsaConfig`
- `CriticalEndpointConfig`
- `UpstreamWindowConfig`
- `RepairRecoverConfig`
- `RandomizedRclConfig`
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
