# Optimization Complexity

This document records the current complexity after the ClockTree edit API and TimingState
refactor. It describes the implementation as it exists now, not an ideal future version.

## Symbols

- `N`: allocated clock-tree nodes, including dead inserted nodes.
- `Na`: alive clock-tree nodes.
- `E`: allocated clock-tree edges, including dead edges.
- `F(x)`: fanout of node `x`.
- `H`: clock-tree height.
- `S(x)`: alive nodes in the subtree rooted at `x`.
- `Q(x)`: flip-flops inside the subtree rooted at `x`.
- `P`: data path count.
- `R(x)`: unique data paths whose launch or capture FF is inside subtree `x`.
- `B`: alive buffer count.
- `C`: buffer library cell count.

Hash-map lookups by name are average `O(1)`, but worst-case `O(N)`.

## Main improvement

Before this refactor, trial moves often had to rely on full timing evaluation:

```text
full evaluate ~= O(N + P * H)
```

The new optimizer hot path applies a reversible `ClockTreeEdit` and updates `TimingState`
incrementally:

```text
trial edit ~= O(affected subtree + affected data paths)
           ~= O(S + R log P)
```

This is the important reduction. A local edit no longer rescans every timing path unless the
affected subtree reaches most of the tree.

The current `ClockTree` still marks dirty clock-arrival cache entries by walking the affected
subtree, so topology edits are not pure `O(1)`. That is acceptable for now because `TimingState`
is the optimizer timing source, but it is a possible future optimization.

## ClockTree operations

| Operation | Current complexity | Notes |
|-----------|--------------------|-------|
| `node(NodeId)`, `edge(EdgeId)`, `is_alive(NodeId)` | `O(1)` | Direct vector indexing. |
| `node_id(name)`, `contains_name(name)` | Average `O(1)` | Name API is for parser, writer, debug, and compatibility. |
| `edge_between(parent, child)` | `O(E)` | Scans alive edges. Avoid this in tight loops when an `EdgeId` is already known. |
| `active_edge_ids()` | `O(E)` | Allocates a vector of alive edge ids. |
| `buffer_nodes()`, `flip_flop_nodes()` | `O(N)` | Dead inserted nodes are skipped but still scanned. |
| `insert_buffer_on_edge(edge, ...)` | `O(F(parent) + S(child))` amortized | Finds the child slot in the parent fanout, appends node/edges, then marks the new buffer subtree dirty. |
| `resize_buffer(NodeId, ...)` | `O(S(buffer))` | Cell validation is average `O(1)` through `BufferLibrary`; dirty marking walks the buffer subtree. |
| `remove_inserted_buffer(NodeId)` | `O(E + F(parent) + S(child))` | `edge_between()` currently scans edges twice; dirty marking walks the child subtree. |
| `undo(insert)` | `O(S(child))` | Topology restore is `O(1)`; dirty marking walks the child subtree. |
| `undo(resize)` | `O(S(buffer))` | Restores cell type and marks subtree dirty. |
| `undo(remove)` | `O(S(buffer))` | Restores inserted buffer and marks subtree dirty. |
| `preorder_with_depth()` | `O(Na)` | Output traversal skips dead nodes. |
| `area(buffer_library)` | `O(N)` | Scans allocated nodes and sums alive buffers. |
| `clock_delay(name)` | Average `O(H)` after lookup | Lazy path refresh; not an optimizer hot-path API. |
| `clock_skew(launch, capture)` | Average `O(Hlaunch + Hcapture)` after lookups | Used by full `evaluate()`, not per trial step. |

Name-based compatibility wrappers add name lookup and sometimes `edge_between()`:

- `insert_buffer(parent_name, child_name, ...)`: average `O(E + F(parent) + S(child))`.
- `remove_buffer(buffer_name)`: average `O(E + F(parent) + S(child))`.
- `resize_buffer(name, ...)`: average `O(S(buffer))`.

## TimingState operations

| Operation | Current complexity | Notes |
|-----------|--------------------|-------|
| Constructor | `O(C + P + N + B*C + P log P)` | Builds cell/path arrays, computes arrivals, area, slacks, TNS, and WNS. `C` is usually small. |
| `metrics()` | `O(1)` | Returns cached metrics by value. |
| `score(baseline)` | `O(1)` | Uses cached metrics. |
| `cell_index(name)` | `O(C)` | Current implementation scans `cells_`. |
| `cell_supports_fanout`, `cell_area`, `cell_delay_*` | `O(1)` | After caller already has a cell index. |
| `smallest_cell_for_fanout(fanout)` | `O(C)` | Scans cells. |
| `cells_for_fanout_by_area(fanout)` | `O(C log C)` | Scans and sorts legal cells. |
| `apply(insert)` | `O(C + S(child) + R(child) log P)` amortized | Sets new buffer arrival, updates area, then updates affected FF paths. |
| `apply(resize)` | `O(C + S(buffer) + R(buffer) log P)` | Applies delay delta to the resized buffer subtree. |
| `apply(remove)` | `O(C + S(child) + R(child) log P)` | Removes inserted-buffer area and subtracts its delay from the child subtree. |
| `undo(edit)` | Same order as `apply(edit)` | Uses the reverse delay and area delta. |
| `snapshot()` | `O(N + P)` time and memory | Copies arrival arrays, slack arrays, and metrics. |
| `restore(snapshot)` | `O(N + P log P)` | Copies arrays and rebuilds negative-slack multisets. |

`TimingState::apply_arrival_delta()` is the core incremental update:

```text
O(S(root) + R(root) log P)
```

It walks the affected clock subtree, collects affected FFs, deduplicates incident timing paths with
an epoch array, updates those path slacks, and maintains TNS/WNS through multisets.

## Full evaluate

`evaluate(clock_tree, data_path_graph, buffer_library)` is still the ground truth.

Current order:

```text
O(N + P * H)
```

`ClockTree::area()` scans the tree, and each data path asks for launch/capture clock skew through
name-based endpoints and lazy root-to-FF clock-arrival refresh. This is fine for baseline/final
checks, but too expensive for every candidate move.

## Optimizer trial cost

A candidate trial is usually:

```text
ClockTree edit + TimingState apply + score
```

If rejected, it also pays:

```text
TimingState undo + ClockTree undo
```

So the practical per-candidate cost is:

- Insert trial: `O(F(parent) + S(child) + R(child) log P)`.
- Resize trial: `O(S(buffer) + R(buffer) log P)`.
- Remove trial: `O(E + F(parent) + S(child) + R(child) log P)`.

The `E` term for remove comes from the current `edge_between()` implementation. If remove starts
to dominate runtime, store incoming/outgoing edge ids or an edge lookup table.

Best-state updates are more expensive:

```text
ClockTree copy + TimingState snapshot = O(N + E + P)
```

Restoring best state costs:

```text
ClockTree copy + TimingState restore = O(N + E + P log P)
```

This is acceptable because it happens on improvement, restart, or phase boundaries, not for every
candidate.

## Algorithm-level notes

- Greedy and MILP-inspired optimizers are dominated by how many candidates they try per step.
- SA and ISA are dominated by candidate trial count plus occasional snapshot/restore.
- `active_edge_ids()` and `buffer_nodes()` allocate vectors; random move generation pays `O(E)` or
  `O(N)` when it asks for all active edges or buffers.
- `edge_between()` is the remaining obvious non-local topology lookup in optimizer helpers.
- Dead inserted nodes and dead edges remain allocated, so `N` and `E` can grow during long SA runs.

## Remaining improvement targets

These are not required for the current architecture, but they are the next places to look if
runtime becomes a problem:

- Replace subtree dirty marking in `ClockTree` with a cheaper dirty-root scheme for optimizer
  edits.
- Add edge lookup or per-node incoming edge ids to remove the `O(E)` scan in `edge_between()`.
- Add a cell-name-to-index map in `TimingState` to make `cell_index(name)` `O(1)`.
- Avoid allocating full vectors in `active_edge_ids()` and `buffer_nodes()` for random sampling.
- Compact dead inserted nodes/edges only outside optimizer hot loops if memory growth becomes a
  problem.
