# Optimization Experiment Parameters

This document defines the default parameter set for fair optimizer experiments.

## Fairness rules

- Greedy, MILP, SA, and ISA use the same wall-clock time budget by default.
- `CADD0040_SA_SECONDS` overrides the time budget for all optimizers.
- Default optimizer budget is `500s`, leaving safety margin before a `600s` contest kill.
- `CADD0040_CHECKPOINT_STEPS` writes the best-so-far tree to the requested output path every fixed
  number of optimizer steps. The default is `1024`; set it to `0` to disable periodic checkpoints.
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
| Full contest-safe | `CADD0040_SA_SECONDS=500` | Final comparison with margin before a 600s kill. |

## Default parameter set

| Optimizer | Time budget | Main parameters |
|-----------|-------------|-----------------|
| Greedy | `500s` | `max_steps=4096`, `max_resize_polish_steps=96`, `max_polish_phases=64`, `violation_sample_limit=32`, `removal_candidate_limit=512` |
| MILP-inspired | `500s` | `max_rounds=4096`, `violation_window=96`, `candidate_limit=4096`, `resize_node_limit=4096` |
| SA | `500s` | `greedy_warmup=256`, `final_greedy_polish=32`, `greedy_polish_interval=0`, `restart_stale=2500`, `restart_gap=0.05` |
| ISA | `500s` | `greedy_warmup=256`, `rounds=16`, `round_greedy=16`, `final_greedy_polish=32`, `restart_stale=2500`, `restart_gap=0.05` |

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
- ISA splits the budget into `16` SA rounds.
- ISA performs a bounded greedy batch after each round.

The old SA periodic greedy polish is disabled by default (`greedy_polish_interval=0`) so the SA and
ISA comparison is easier to explain: SA is single-phase exploration, ISA is repeated
exploration-refinement.

## Why these values

- Equal `500s` defaults keep optimizer comparisons fair while leaving margin for final
  evaluation, checkpoint output, and process shutdown before a 600s contest limit.
- `256` warmup steps give SA and ISA the same deterministic starting help.
- `32` final polish steps give SA and ISA the same deterministic cleanup budget.
- Greedy uses a high `64` phase cap so insert/remove and resize can alternate until time or local
  convergence, instead of stopping because the phase cap is too tight.
- ISA uses `16` rounds so SA exploration and greedy refinement have more chances to alternate.
- ISA keeps `16` greedy steps after each round because round-level refinement is the feature being
  tested.
- All deterministic helper steps are bounded by the same deadline, so short-budget experiments do
  not get hidden extra work.
- `Solver` writes the original tree before optimization, then checkpoint writes replace that file
  atomically with the current best-so-far tree.

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
