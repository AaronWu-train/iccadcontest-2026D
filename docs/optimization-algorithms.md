# Optimization Algorithms

This document summarizes the A1-A13 main experiment optimizers. `visual` is a trace and
visualization tool, and `milp` remains runnable but is not part of the A1-A13 default matrix.

## Shared Model

All main optimizers use the same local-search skeleton:

```text
current solution -> CandidatePolicy -> candidate actions -> score/objective evaluation
                 -> AcceptPolicy -> accepted action -> updated solution
```

Legal action types are:

- Insert a new buffer on an active edge.
- Resize an active buffer to another legal cell.
- Remove an inserted buffer.

Original contest nodes cannot be removed. Candidate trials use reversible `ClockTreeEdit`
operations and `TimingState` scoring; rejected trials must undo both `TimingState` and
`ClockTree`.

The common score comes from `src/evaluation.cpp`:

```text
Score = 0.5 * SS improvement + 0.25 * FF improvement + 0.25 * area improvement
```

## Candidate Policies

| CandidatePolicy | Meaning |
|-----------------|---------|
| `RandomActionSpace` | Random samples from insert, remove, and resize actions. |
| `ViolationPath` | Insert candidates on the launch/capture endpoint edges of severe violated paths. |
| `UpstreamWindow` | Insert candidates along a short upstream window from violated endpoints. |
| `CriticalEndpoint` | Insert candidates at endpoints ranked by accumulated negative slack. |
| `UnionPool` | `ViolationPath + UpstreamWindow + CriticalEndpoint + Remove + Resize`. |
| `SampledUnionPool` | One sampled proposal from the UnionPool action families. |

## Accept Policies

| AcceptPolicy | Meaning |
|--------------|---------|
| `BestScore` | Evaluate the candidate set and accept the positive move with highest total score. |
| `TwoStepSlackThenScore` | First accept positive slack-objective moves, then positive total-score moves. |
| `Metropolis` | Accept improvements and probabilistically accept worse moves by SA temperature. |
| `IteratedMetropolis` | Metropolis acceptance inside ISA rounds with greedy cleanup between rounds. |
| `TabuBestNonTabu` | Accept the best legal non-tabu move; aspiration allows global-best improvements. |

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

Numeric aliases are uppercase only. Config files should use descriptive aliases as section names.
The short aliases `two-step-optimize`, `sa`, `isa`, and `tabu` remain accepted for A6-A9.

## Algorithm Notes

## A1-A13 Flow Summary

| ID | Main flow |
|----|-----------|
| `A1` | Start from current solution; sample `RandomActionSpace`; evaluate sampled actions; accept the positive best-score action; repeat until `4096` accepted moves, no improving action, or time is up; run bounded resize polish phases. |
| `A2` | Same `BestScore` greedy loop as A1, but candidates come from `ViolationPath` plus inserted-buffer removal; then bounded resize polish. |
| `A3` | Same `BestScore` greedy loop as A1, but candidates come from `UpstreamWindow` plus inserted-buffer removal; then bounded resize polish. |
| `A4` | Same `BestScore` greedy loop as A1, but candidates come from `CriticalEndpoint` plus inserted-buffer removal; then bounded resize polish. |
| `A5` | Same `BestScore` greedy loop as A1, but candidates come from full `UnionPool`; then bounded resize polish. |
| `A6` | Build `UnionPool`; first accept the best positive slack-objective action for up to `2048` accepted moves; then accept the best positive total-score action for up to `2048` more accepted moves; restore the best total-score tree. |
| `A7` | Run bounded `ViolationPath + BestScore` warmup; run time-driven SA with one `SampledUnionPool` proposal per iteration and `Metropolis` acceptance; restore best state; run bounded final greedy polish. |
| `A8` | Run bounded `ViolationPath + BestScore` warmup; split remaining time into ISA rounds; each round runs `SampledUnionPool + IteratedMetropolis`, restores best state, and runs a small greedy batch; finish with bounded final greedy polish. |
| `A9` | Build `UnionPool`; evaluate candidates; accept the best non-tabu candidate, with aspiration for global-best improvements; repeat until `4096` iterations, no legal candidate, or time is up; restore the best seen tree. |
| `A10` | Same two-step objective schedule as A6, but each step scores a random `RandomActionSpace` candidate set. |
| `A11` | Same SA flow as A7, but the SA phase samples `RandomActionSpace` proposals. |
| `A12` | Same ISA flow as A8, but each SA round samples `RandomActionSpace` proposals. |
| `A13` | Same tabu flow as A9, but each step scores a random `RandomActionSpace` candidate set. |

### A1-A5 Greedy

A1-A5 share `GreedyOptimizer`. Each iteration builds candidates from its `CandidatePolicy`,
temporarily applies each candidate, computes score delta, and accepts the best positive move.

Common cleanup remains bounded:

- Insert/remove candidates are evaluated in the main greedy phase.
- Resize polish runs as a separate bounded phase.
- The best seen tree is restored at the end.

A1 uses random action-space samples. A2-A4 use one focused insert policy plus inserted-buffer
removal. A5 uses the full `UnionPool`.

### A6/A10 TwoStepOptimize

A6 uses `UnionPool` for both phases; A10 uses `RandomActionSpace` for both phases:

- Phase 1 accepts the candidate with the best positive slack objective improvement.
- Phase 2 accepts the candidate with the best positive total score improvement.

The final output is the best normal-score tree seen across both phases.

### A7/A11 SA

A7 uses `SampledUnionPool` proposals during the SA phase. A11 uses `RandomActionSpace` proposals.
Both accept improving moves and accept worse moves with `exp(delta / temperature)`. Greedy warmup
and final polish are preserved as bounded BestScore cleanup phases.

### A8/A12 ISA

A8 runs multiple SA rounds with `SampledUnionPool + IteratedMetropolis`; A12 uses
`RandomActionSpace + IteratedMetropolis`. Between rounds it restores the current best state and
applies a small greedy cleanup batch.

### A9/A13 Tabu

A9 evaluates `UnionPool`; A13 evaluates `RandomActionSpace`. Both choose the highest-score
candidate that is not tabu and record accepted moves in tabu memory for `tenure` steps. A tabu move
can be accepted by aspiration if it improves the global best score. Worse moves may be accepted, but
the final output is the best seen tree.

## Progress Trace Names

Numeric event traces include both `candidate_policy` and `accept_policy`. Candidate-level trials are
not recorded; phase starts/ends, best updates, restarts, and final events are recorded.
