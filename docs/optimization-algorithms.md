# Optimization Algorithms

This document summarizes the A1-A8 main experiment optimizers using the same structure. `visual`
is a trace and visualization tool, not a mainline comparison algorithm. `milp` is still runnable
but is not part of the A1-A8 default matrix.

## Shared model

All main optimizers work on a mutable `ClockTree` and a `TimingState` cache. Candidate moves are
tested by applying a reversible `ClockTreeEdit`, asking `TimingState` for the new score, and then
either keeping or undoing the edit.

Legal move types:

- Insert a new buffer on an active edge.
- Resize an active buffer to another legal cell.
- Remove an inserted buffer.

Original contest nodes cannot be removed.

The common score comes from `src/evaluation.cpp`:

```text
Score = 0.5 * SS improvement + 0.25 * FF improvement + 0.25 * area improvement
```

The full `evaluate()` path is used for validation and final scoring. `TimingState` is used for
fast optimizer steps.

## A1 Greedy-ViolationPath

### Core idea

At each step, try a bounded set of local edits and keep the edit with the best positive score
delta.

### Initialization

- Build `TimingState` from the current `ClockTree`, `DataPathGraph`, and `BufferLibrary`.
- Record the baseline tree and score.
- Load `GreedyConfig`.

### Candidate moves

- For worst SS setup violations, try inserting buffers on the capture FF incoming edge.
- For worst FF hold violations, try inserting buffers on the launch FF incoming edge.
- Try removing inserted buffers as cleanup candidates.
- During polish, try resizing active buffers.

### Accept/reject rule

- Accept only the best candidate if it improves score by a positive epsilon.
- Reject all non-improving candidates by undoing their edit.

### Small optimizations

- Violation sample limit avoids scanning every bad path every step.
- Removal candidate limit avoids spending all time on cleanup.
- Resize polish is separated from insertion cleanup.
- The best seen tree is tracked and restored at the end.

### Stop condition

- Stop when no positive move exists.
- Stop when `GreedyConfig::max_steps` or time budget is reached.
- Resize polish has its own step and phase limits.

### Parameters

Alias: `greedy-violation-path`.

See `GreedyConfig` in `src/optimization/optimizer_config.hpp`:

- `time_budget`
- `max_steps`
- `max_resize_polish_steps`
- `max_resize_nodes_per_step`
- `max_polish_phases`
- `violation_sample_limit`
- `removal_candidate_limit`

## A2 SA

### Core idea

Use simulated annealing to escape local optima. Good moves are accepted, and worse moves may be
accepted according to the Metropolis probability.

### Initialization

- Build `TimingState`.
- Record initial current and best state.
- Load `SaConfig`.
- Run greedy warmup with `256` steps.

### Candidate moves

- Random insert.
- Guided insert near a violated timing path.
- Random resize.
- Remove inserted buffer.
- Optional periodic greedy polish exists in the SA runner, but default is disabled.

### Accept/reject rule

- Always accept improving moves.
- Accept worsening moves with `exp(delta / temperature)`.
- Reject by calling `TimingState::undo(edit)` and `ClockTree::undo(edit)`.

### Small optimizations

- Greedy warmup before annealing.
- Guided insert biases some moves toward violated paths.
- Fixed RNG seed keeps experiments reproducible.
- Restart from best when search is stale and below best by `restart_score_gap`.
- Final greedy polish runs after annealing.

### Stop condition

- Stop at `SaConfig::time_budget`.
- Temperature cools from initial temperature toward minimum temperature during the phase.

### Parameters

Alias: `sa`.

See `SaConfig`:

- `time_budget`
- `initial_temperature`
- `min_temperature`
- `cooling_factor`
- `greedy_warmup_iterations`
- `final_greedy_polish_iterations`
- `restart_stale_iterations`
- `restart_score_gap`
- `greedy_polish_interval`

## A3 ISA

### Core idea

Run multiple SA phases and alternate them with greedy batches. This keeps exploration from SA
while repeatedly cleaning up obvious local improvements.

### Initialization

- Build `TimingState`.
- Record initial current and best state.
- Load `IsaConfig`.
- Run the same `256`-step greedy warmup as A2.

### Candidate moves

- Same SA-family random and guided moves as A2.
- Greedy batch between rounds.
- Final greedy polish after all rounds.

### Accept/reject rule

- Same Metropolis rule as A2 during SA phases.
- Greedy batches accept only positive-delta moves.

### Small optimizations

- Greedy warmup.
- SA round split: total time budget is divided across `16` rounds.
- Restart from best during stale phases.
- Round greedy batch after each SA phase.
- Final greedy polish.

### Stop condition

- Stop after `IsaConfig::rounds`.
- Stop early if the global time budget expires.
- Each round uses its own phase deadline.

### Parameters

Alias: `isa`.

See `IsaConfig`:

- `time_budget`
- `initial_temperature`
- `min_temperature`
- `cooling_factor`
- `greedy_warmup_iterations`
- `rounds`
- `greedy_round_iterations`
- `final_greedy_polish_iterations`
- `restart_stale_iterations`
- `restart_score_gap`

## A4 Greedy-CriticalEndpoint

### Core idea

Use best-improvement greedy, but build insert candidates from the most critical FFs instead of only
the endpoints of the worst paths.

### Initialization

- Build `TimingState`.
- Load `CriticalEndpointConfig`.
- Track the best tree.

### Candidate moves

- Accumulate FF criticality from negative setup and hold slacks.
- Select the top `32` critical FFs.
- Try inserting fanout-1 buffers on each selected FF incoming edge.
- Also try removing inserted buffers.
- Resize polish is the same as A1.

### Accept/reject rule

- Accept the best positive score delta.

### Small optimizations

- FF criticality groups multiple path violations onto the same endpoint.
- Removal and resize cleanup are bounded like A1.

### Stop condition

- Stop when no positive candidate exists or time expires.

### Parameters

Alias: `greedy-critical-endpoint`. See `CriticalEndpointConfig`.

## A5 Greedy-UpstreamWindow

### Core idea

Use best-improvement greedy, but each violated endpoint contributes a short upstream window toward
the root.

### Initialization

- Build `TimingState`.
- Load `UpstreamWindowConfig`.
- Track the best tree.

### Candidate moves

- Rank violated paths by severity.
- For setup violations, start from the capture FF.
- For hold violations, start from the launch FF.
- Walk up to `upstream_window_depth=4` incoming edges toward the root.
- Try fanout-1 buffer insertion on each window edge.
- Also try inserted-buffer removal and resize polish.

### Accept/reject rule

- Accept the best positive score delta.

### Small optimizations

- The root-path window gives more placement choices than A1 while staying bounded.
- Duplicate edge/cell candidates are removed before scoring.

### Stop condition

- Stop when no positive candidate exists or time expires.

### Parameters

Alias: `greedy-upstream-window`. See `UpstreamWindowConfig`.

## A6 Greedy-RepairRecover

### Core idea

Use a two-stage objective schedule: repair timing first, then recover area without losing timing.

### Initialization

- Build `TimingState`.
- Load `RepairRecoverConfig`.
- Track the best tree by the normal score.

### Candidate moves

- Stage 1 uses violation-path and upstream-window insert candidates.
- Stage 2 uses remove and resize candidates.

### Accept/reject rule

- Stage 1 accepts the candidate with the best positive timing objective improvement.
- Stage 2 accepts area-saving moves only if timing metrics do not get worse beyond tolerance and
  score does not drop.

### Small optimizations

- Timing repair ignores area temporarily.
- Area recovery is separated so the report can compare objective scheduling against A1/A5.

### Stop condition

- Stop each stage when no legal improving move exists or time expires.

### Parameters

Alias: `greedy-repair-recover`. See `RepairRecoverConfig`.

## A7 Greedy-RandomizedRCL

### Core idea

Use randomized greedy construction. Each step scores the candidate list, keeps the top-k positive
moves, then randomly selects one move from that restricted candidate list.

### Initialization

- Save the initial tree.
- Load `RandomizedRclConfig`.
- Use fixed seed `2026`.

### Candidate moves

- Violation-path insert candidates.
- Inserted-buffer removal candidates.
- Final resize polish after all restarts.

### Accept/reject rule

- Candidate must have positive score delta.
- Pick uniformly from the top `k=8` positive moves.
- Track the global best tree across restarts.

### Small optimizations

- Multiple restarts explore different local optima.
- Fixed seed keeps the randomized run reproducible.

### Stop condition

- Stop after restart limit, steps-per-restart limit, or time expiration.

### Parameters

Alias: `greedy-randomized-rcl`. See `RandomizedRclConfig`.

## A8 Tabu

### Core idea

Use tabu search on a mixed candidate pool. The optimizer may accept worse moves if they are the
best non-tabu option, which helps escape local optima.

### Initialization

- Build `TimingState`.
- Load `TabuConfig`.
- Track current state and global best state.

### Candidate moves

- Violation-path insert candidates.
- Critical-endpoint insert candidates.
- Upstream-window insert candidates.
- Inserted-buffer removal candidates.
- Resize candidates.

### Accept/reject rule

- Choose the highest-score non-tabu candidate.
- Tabu moves are allowed only by aspiration when they improve the global best score.
- Accepted moves enter tabu memory for `tenure=128` steps.

### Small optimizations

- Candidate pool is capped and deduplicated.
- Tabu memory prevents immediate cycling.
- Worse moves are allowed, but final output is the best seen tree.

### Stop condition

- Stop after `max_steps` or time expiration.
- Stop early if no legal non-tabu candidate exists.

### Parameters

Alias: `tabu`. See `TabuConfig`.

## Legacy MilpOptimizer

### Core idea

This optimizer is MILP-inspired, but it is not a true MILP solver. It uses worst timing
violations to build a candidate window, then applies the best positive local edit.

### Initialization

- Build `TimingState`.
- Record the baseline tree and score.
- Load `MilpConfig`.

### Candidate moves

- Rank bad paths by violation severity.
- For SS setup violations, consider inserting near the capture side.
- For FF hold violations, consider inserting near the launch side.
- When no violation move helps, try inserted-buffer removal and bounded resize candidates.

### Accept/reject rule

- Accept the best positive score delta in the current candidate window.
- Undo all rejected candidate edits.

### Small optimizations

- Candidate window focuses on the worst violations.
- Candidate limit bounds the round cost.
- Resize node limit keeps cleanup rounds bounded.
- The best seen tree is restored after the loop.

### Stop condition

- Stop when no positive candidate remains.
- Stop when `MilpConfig::max_rounds` or time budget is reached.

### Parameters

See `MilpConfig`:

- `time_budget`
- `max_rounds`
- `violation_window`
- `candidate_limit`
- `resize_node_limit`

## Regression expectations

The refactor is intended to preserve A1-A3 behavior while adding formal A4-A8 algorithms. Under a
fixed short budget such as `CADD0040_SA_SECONDS=10`, final scores for `greedy-violation-path`,
`sa`, and `isa` should stay close to the pre-refactor baseline. If a testcase drops by more than
5%, inspect:

- Candidate order.
- Random seed use.
- `ClockTree::undo()`.
- `TimingState` delta propagation.
- Snapshot and restore.
- Inserted-buffer removal behavior.
