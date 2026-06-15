# Optimization Algorithms

This document summarizes the four main optimizers using the same structure. `visual` is a
trace and visualization tool, not a mainline comparison algorithm.

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
Score = 0.3 * SS improvement + 0.3 * FF improvement + 0.4 * area improvement
```

The full `evaluate()` path is used for validation and final scoring. `TimingState` is used for
fast optimizer steps.

## GreedyOptimizer

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

See `GreedyConfig` in `src/optimization/optimizer_config.hpp`:

- `time_budget`
- `max_steps`
- `max_resize_polish_steps`
- `max_resize_nodes_per_step`
- `max_polish_phases`
- `violation_sample_limit`
- `removal_candidate_limit`

## MilpOptimizer

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

## AnnealingOptimizer

### Core idea

Use simulated annealing to escape local optima. Good moves are accepted, and worse moves may be
accepted according to the Metropolis probability.

### Initialization

- Build `TimingState`.
- Record initial current and best state.
- Load `SaConfig`.
- Run greedy warmup to start from a better local state.

### Candidate moves

- Random insert.
- Guided insert near a violated timing path.
- Random resize.
- Remove inserted buffer.
- Periodic greedy polish candidates from the shared SA helper.

### Accept/reject rule

- Always accept improving moves.
- Accept worsening moves with:

```text
exp(delta / temperature)
```

- Reject by calling `TimingState::undo(edit)` and `ClockTree::undo(edit)`.

### Small optimizations

- Greedy warmup before annealing.
- Guided insert biases some moves toward violated paths.
- Fixed RNG seed keeps experiments reproducible.
- Restart from best when the search is stale and below best score by a configured gap.
- Periodic greedy polish repairs easy local opportunities during the SA loop.
- Final greedy polish runs after annealing.

### Stop condition

- Stop at `SaConfig::time_budget`.
- Temperature cools from initial temperature toward minimum temperature during the phase.

### Parameters

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

## IteratedSaOptimizer

### Core idea

Run multiple SA phases and alternate them with greedy batches. This keeps exploration from SA
while repeatedly cleaning up obvious local improvements.

### Initialization

- Build `TimingState`.
- Record initial current and best state.
- Load `IsaConfig`.
- Run a larger greedy warmup than `AnnealingOptimizer`.

### Candidate moves

- Same SA-family random and guided moves as `AnnealingOptimizer`.
- Greedy batch between rounds.
- Final greedy polish after all rounds.

### Accept/reject rule

- Same Metropolis rule as `AnnealingOptimizer` during SA phases.
- Greedy batches accept only positive-delta moves.

### Small optimizations

- Greedy warmup.
- SA round split: total time budget is divided across rounds.
- Restart from best during stale phases.
- Round greedy batch after each SA phase.
- Final greedy polish.
- Shared SA phase helper keeps acceptance, restart, and debug reporting consistent.

### Stop condition

- Stop after `IsaConfig::rounds`.
- Stop early if the global time budget expires.
- Each round uses its own phase deadline.

### Parameters

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

## Regression expectations

The refactor is intended to preserve behavior, not to invent new algorithms. Under a fixed short
budget such as `CADD0040_SA_SECONDS=10`, final scores for `greedy`, `milp`, `anneal`, and `isa`
should stay close to the pre-refactor baseline. If a testcase drops by more than 5%, inspect:

- Candidate order.
- Random seed use.
- `ClockTree::undo()`.
- `TimingState` delta propagation.
- Snapshot and restore.
- Inserted-buffer removal behavior.
