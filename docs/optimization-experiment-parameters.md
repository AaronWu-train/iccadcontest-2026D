# Optimization Experiment Parameters

This document defines the default parameter set for fair optimizer experiments.

## Fairness rules

- Greedy, MILP, SA, and ISA use the same wall-clock time budget by default.
- `CADD0040_SA_SECONDS` overrides the time budget for all optimizers.
- SA and ISA warmup, SA phases, round greedy batches, and final polish all count against the same
  time budget.
- Greedy/SA/ISA use the same score function and the same final `evaluate()` path.
- SA and ISA keep fixed RNG seed behavior for reproducibility.

## Recommended experiment budgets

Use the same budget for every optimizer in one comparison table:

| Experiment | Command setting | Purpose |
|------------|-----------------|---------|
| Smoke | `CADD0040_SA_SECONDS=10` | Verify all optimizers run. |
| Short report | `CADD0040_SA_SECONDS=60` | Quick comparison during development. |
| Main report | `CADD0040_SA_SECONDS=300` | Practical quality/runtime tradeoff table. |
| Full contest | `CADD0040_SA_SECONDS=540` | Final comparison under contest-like limit. |

## Default parameter set

| Optimizer | Time budget | Main parameters |
|-----------|-------------|-----------------|
| Greedy | `540s` | `max_steps=4096`, `max_resize_polish_steps=96`, `max_polish_phases=5`, `violation_sample_limit=32`, `removal_candidate_limit=512` |
| MILP-inspired | `540s` | `max_rounds=4096`, `violation_window=96`, `candidate_limit=4096`, `resize_node_limit=4096` |
| SA | `540s` | `greedy_warmup=256`, `final_greedy_polish=32`, `greedy_polish_interval=0`, `restart_stale=2500`, `restart_gap=0.05` |
| ISA | `540s` | `greedy_warmup=256`, `rounds=5`, `round_greedy=16`, `final_greedy_polish=32`, `restart_stale=2500`, `restart_gap=0.05` |

## SA vs ISA policy

SA and ISA intentionally share these settings:

- Same total time budget.
- Same initial temperature: `0.08`.
- Same cooling factor: `0.01`.
- Same minimum temperature: `1e-6`.
- Same greedy warmup limit: `256`.
- Same final greedy polish limit: `32`.
- Same restart rule.

They differ only in search schedule:

- SA uses one long SA phase.
- ISA splits the budget into `5` SA rounds.
- ISA performs a bounded greedy batch after each round.

The old SA periodic greedy polish is disabled by default (`greedy_polish_interval=0`) so the SA and
ISA comparison is easier to explain: SA is single-phase exploration, ISA is repeated
exploration-refinement.

## Why these values

- Equal `540s` defaults remove the previous unfairness where Greedy/MILP defaulted to `60s`.
- `256` warmup steps give SA and ISA the same deterministic starting help.
- `32` final polish steps give SA and ISA the same deterministic cleanup budget.
- ISA keeps `16` greedy steps after each round because round-level refinement is the feature being
  tested.
- All deterministic helper steps are bounded by the same deadline, so short-budget experiments do
  not get hidden extra work.

## Recommended commands

Run all main optimizers locally:

```sh
make release
CADD0040_SA_SECONDS=300 CADD0040_DEBUG_PROGRESS=0 ./scripts/slurm_run_all_optimizers.sh --local
```

Run with Slurm:

```sh
make release
CADD0040_SA_SECONDS=300 ./scripts/slurm_run_all_optimizers.sh
```

Aggregate Slurm results after completion:

```sh
OUTPUT_DIR=<printed-output-dir> ./scripts/slurm_run_all_optimizers.sh --aggregate-only
```
